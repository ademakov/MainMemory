/*
 * event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "event.h"

#include "alloc.h"
#include "core.h"
#include "exit.h"
#include "log.h"
#include "net.h"
#include "port.h"
#include "selfpipe.h"
#include "task.h"
#include "timer.h"
#include "trace.h"

#include <string.h>
#if HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif
#if HAVE_SYS_EVENT_H
# include <sys/event.h>
#endif
#include <sys/types.h>
#include <unistd.h>

/**********************************************************************
 * Event Handler Table.
 **********************************************************************/

// Event handler table size.
#define MM_EVENT_HANDLER_MAX	(255)

// Event handler descriptor.
struct mm_event_hd
{
	mm_event_handler_t handler;
};

// Event handler table.
static struct mm_event_hd mm_event_hd_table[MM_EVENT_HANDLER_MAX];

// The number of registered event handlers.
static int mm_event_hd_table_size;

// A dummy event handler.
static void
mm_event_dummy(mm_event_t event __attribute__((unused)),
	       uint32_t data __attribute__((unused)))
{
	DEBUG("hmm, dummy event handler invoked.");
}

// Initialize the event handler table.
static void
mm_event_init_handlers(void)
{
	ENTER();
	ASSERT(MM_EVENT_HANDLER_MAX < 256);

	// Register dummy handler with zero id.
	ASSERT(mm_event_hd_table_size == 0);
	(void) mm_event_register_handler(mm_event_dummy);
	ASSERT(mm_event_hd_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_event_hid_t
mm_event_register_handler(mm_event_handler_t handler)
{
	ENTER();

	ASSERT(handler != NULL);
	ASSERT(mm_event_hd_table_size < MM_EVENT_HANDLER_MAX);

	mm_event_hid_t id = mm_event_hd_table_size++;
	mm_event_hd_table[id].handler = handler;

	DEBUG("registered event handler %d", id);

	LEAVE();
	return id;
}

/**********************************************************************
 * File Descriptor Table.
 **********************************************************************/

// The maximum allowed number of open file descriptors.
#define MM_EVENT_FD_MAX (1024 * 1024)

// File descriptor table entry.
struct mm_event_fd
{
	// client data
	uint32_t data;

	// event handers
	mm_event_hid_t input_handler;
	mm_event_hid_t output_handler;
	mm_event_hid_t control_handler;

	// flags
#if MM_ONESHOT_HANDLERS
	unsigned changed : 1;
	unsigned oneshot_input : 1;
	unsigned oneshot_output : 1;
#else
	uint8_t changed;
#endif
};

// File descriptor table.
static struct mm_event_fd *mm_event_fd_table;

// File descriptor table size.
static int mm_event_fd_table_size = _POSIX_OPEN_MAX;

// Initialize the file descriptor table.
static void
mm_event_init_fd_table(void)
{
	ENTER();

	// Determine the table size.
	int max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd > mm_event_fd_table_size)
		mm_event_fd_table_size = max_fd;

	if (mm_event_fd_table_size > MM_EVENT_FD_MAX) {
		mm_brief("truncating too high fd limit: %d", mm_event_fd_table_size);
		mm_event_fd_table_size = MM_EVENT_FD_MAX;
	}
	mm_brief("fd table size: %d", mm_event_fd_table_size);

	// Allocate the table.
	mm_event_fd_table = mm_global_calloc(mm_event_fd_table_size, sizeof(struct mm_event_fd));

	LEAVE();
}

// Free the file descriptor table.
static void
mm_event_free_fd_table(void)
{
	ENTER();

	mm_global_free(mm_event_fd_table);

	LEAVE();
}

// Verify if the file descriptor fits into the table.
int
mm_event_verify_fd(int fd)
{
	if (unlikely(fd < 0)) {
		/* The fd is invalid. */
		return MM_EVENT_FD_INVALID;
	} else if (likely(fd < mm_event_fd_table_size)) {
		/* The fd is okay. */
		return MM_EVENT_FD_VALID;
	} else {
		/* The fd exceeds the table capacity. */
		return MM_EVENT_FD_TOO_BIG;
	}
}

static inline void
mm_event_input(struct mm_event_fd *fd)
{
	ENTER();

	mm_event_hid_t id = fd->input_handler;
	ASSERT(id < mm_event_hd_table_size);

#if MM_ONESHOT_HANDLERS
	if (fd->oneshot_input)
		fd->input_handler = 0;
#endif

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(MM_EVENT_INPUT, fd->data);

	LEAVE();
}

static inline void
mm_event_output(struct mm_event_fd *fd)
{
	ENTER();

	mm_event_hid_t id = fd->output_handler;
	ASSERT(id < mm_event_hd_table_size);

#if MM_ONESHOT_HANDLERS
	if (fd->oneshot_output)
		fd->output_handler = 0;
#endif

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(MM_EVENT_OUTPUT, fd->data);

	LEAVE();
}

static inline void
mm_event_control(struct mm_event_fd *fd, mm_event_t event)
{
	ENTER();

	mm_event_hid_t id = fd->control_handler;
	ASSERT(id < mm_event_hd_table_size);

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(event, fd->data);

	LEAVE();
}

/**********************************************************************
 * epoll support.
 **********************************************************************/

#ifdef HAVE_SYS_EPOLL_H

#define MM_EPOLL_MAX 512

struct mm_event_table
{
	struct mm_port *port;
	struct mm_selfpipe selfpipe;

	// The epoll descriptor.
	int epoll_fd;

	// The epoll list size.
	int nevents;

	// The epoll list.
	struct epoll_event events[MM_EPOLL_MAX];
};

static void
mm_event_init_sys(struct mm_event_table *events)
{
	ENTER();

	events->epoll_fd = epoll_create(511);
	if (events->epoll_fd < 0)
		mm_fatal(errno, "Failed to create epoll fd");

	events->nevents = 0;

	LEAVE();
}

static void
mm_event_free_sys(struct mm_event_table *events)
{
	ENTER();

	if (events->epoll_fd >= 0)
		close(events->epoll_fd);

	LEAVE();
}

bool
mm_event_collect(struct mm_event_table *events)
{
	ENTER();

	// Indicate if there were any control events processed.
	bool control_events = false;

	// Make changes to the fd set watched by epoll.
	uint32_t msg[3];
	while (mm_port_receive(events->port, msg, 3) == 0) {
		int fd = msg[0];
		uint32_t code = msg[1];
		uint32_t data = msg[2];

		mm_event_hid_t input_handler = (code >> 24) & 0xff;
		mm_event_hid_t output_handler = (code >> 16) & 0xff;
		mm_event_hid_t control_handler = (code >> 8) & 0xff;

		struct mm_event_fd *mm_fd = &mm_event_fd_table[fd];
		uint32_t old_events = 0, new_events = 0;
		int a, b;

		// Check if a read event registration if needed.
		a = (mm_fd->input_handler != 0);
		b = (input_handler != 0);
		if (a) {
			old_events |= EPOLLIN;
		}
		if (b) {
			new_events |= EPOLLIN;
		}
#if ENABLE_DEBUG
		if (likely(a != b)) {
			if (b) {
				DEBUG("register fd %d for read events", fd);
			} else {
				DEBUG("unregister fd %d for read events", fd);
			}
		}
#endif

		// Check if a write event registration if needed.
		a = (mm_fd->output_handler != 0);
		b = (output_handler != 0);
		if (a) {
			old_events |= EPOLLOUT;
		}
		if (b) {
			new_events |= EPOLLOUT;
		}
#if ENABLE_DEBUG
		if (likely(a != b)) {
			if (b) {
				DEBUG("register fd %d for write events", fd);
			} else {
				DEBUG("unregister fd %d for write events", fd);
			}
		}
#endif

		if (likely(old_events != new_events)) {
			int op;
			if (new_events == 0)
				op = EPOLL_CTL_DEL;
			else if (old_events == 0)
				op = EPOLL_CTL_ADD;
			else
				op = EPOLL_CTL_MOD;

			struct epoll_event ev;
			ev.events = new_events | EPOLLET | EPOLLRDHUP;
			ev.data.fd = fd;

			int rc = epoll_ctl(events->epoll_fd, op, fd, &ev);
			if (unlikely(rc < 0)) {
				mm_error(errno, "epoll_ctl");
			}
		}

		// Invoke the old handler if any.
		if (mm_fd->control_handler) {
			mm_event_control(mm_fd, MM_EVENT_UNREGISTER);
			control_events = true;
		}

		// Store the requested I/O handler.
		mm_fd->data = data;
		mm_fd->input_handler = input_handler;
		mm_fd->output_handler = output_handler;
		mm_fd->control_handler = control_handler;

		// Invoke the new handler if any.
		if (mm_fd->control_handler) {
			mm_event_control(mm_fd, MM_EVENT_REGISTER);
			control_events = true;
		}
	}

	LEAVE();
	return control_events;
}

bool
mm_event_poll(struct mm_event_table *events, mm_timeout_t timeout)
{
	ENTER();

	// Flush the log before possible sleep.
	mm_flush();

	// Find the event wait timeout.
	timeout /= 1000;

	// Poll the system for events.
	int n = epoll_wait(events->epoll_fd, events->events, MM_EPOLL_MAX, timeout);

	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		events->nevents = 0;
	} else {
		events->nevents = n;
	}

	LEAVE();
	return (events->nevents != 0);
}

void
mm_event_dispatch(struct mm_event_table *events)
{
	ENTER();

	// Process the received system events.
	for (int i = 0; i < events->nevents; i++) {
		int fd = events->events[i].data.fd;
		if ((events->events[i].events & EPOLLIN) != 0) {
			DEBUG("input event on fd %d", fd);
			mm_event_input(&mm_event_fd_table[fd]);
		}
		if ((events->events[i].events & EPOLLOUT) != 0) {
			DEBUG("output event on fd %d", fd);
			mm_event_output(&mm_event_fd_table[fd]);
		}

		if ((events->events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			DEBUG("input error event on fd %d", fd);
			mm_event_control(&mm_event_fd_table[fd], MM_EVENT_INPUT_ERROR);
		}
		if ((events->events[i].events & (EPOLLERR | EPOLLHUP)) != 0) {
			DEBUG("output error event on fd %d", fd);
			mm_event_control(&mm_event_fd_table[fd], MM_EVENT_OUTPUT_ERROR);
		}
	}

	LEAVE();
}

#endif // HAVE_SYS_EPOLL_H

/**********************************************************************
 * kqueue/kevent support.
 **********************************************************************/

#ifdef HAVE_SYS_EVENT_H

#define MM_EVENT_NKEVENTS_MAX	1024
#define MM_EVENT_NCHANGES_MAX	512

// File descriptor change record.
struct mm_kevent_change_rec
{
	int fd;
	mm_event_t event;
};

struct mm_event_table
{
	struct mm_port *port;
	struct mm_selfpipe selfpipe;

	// The kqueue descriptor.
	int kqueue_fd;

	// The kevent list size.
	int nkevents;

	// The change list size.
	int nchanges;

	// The kevent list.
	struct kevent kevents[MM_EVENT_NKEVENTS_MAX];

	// File descriptor change list.
	struct mm_kevent_change_rec changes[MM_EVENT_NCHANGES_MAX];

	// A delayed file descriptor change record.
	uint32_t delayed_msg[3];
	bool has_delayed_msg;
};

static void
mm_event_init_sys(struct mm_event_table *events)
{
	ENTER();

	events->kqueue_fd = kqueue();
	if (events->kqueue_fd == -1)
		mm_fatal(errno, "Failed to create kqueue");

	events->nkevents = 0;
	events->nchanges = 0;
	events->has_delayed_msg = false;

	LEAVE();
}

static void
mm_event_free_sys(struct mm_event_table *events)
{
	ENTER();

	if (events->kqueue_fd >= 0)
		close(events->kqueue_fd);

	LEAVE();
}

static void
mm_event_process_msg(struct mm_event_table *events, uint32_t *msg)
{
	// Parse the message.
	int fd = msg[0];
	uint32_t code = msg[1];

	// Get the requested fd.
	struct mm_event_fd *ev_fd = &mm_event_fd_table[fd];
	ASSERT(!ev_fd->changed);

	if (code == 0) {
		// Handle an UNREG message.

		ASSERT(msg[2] == 0);

		if (ev_fd->input_handler) {
			ev_fd->input_handler = 0;

			DEBUG("unregister fd %d for read events", fd);
			ASSERT(mm_nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &events->kevents[events->nkevents++];
			EV_SET(kp, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
		}

		if (ev_fd->output_handler) {
			ev_fd->output_handler = 0;

			DEBUG("unregister fd %d for write events", fd);
			ASSERT(mm_nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &events->kevents[events->nkevents++];
			EV_SET(kp, fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
		}

		if (ev_fd->control_handler) {
			ev_fd->changed = 1;

			events->changes[events->nchanges].fd = fd;
			events->changes[events->nchanges].event = MM_EVENT_UNREGISTER;
			events->nchanges++;
		}

	} else {
		// Handle a REG or TRIGGER message.

		mm_event_hid_t input_handler = (code >> 24) & 0xff;
		mm_event_hid_t output_handler = (code >> 16) & 0xff;
		mm_event_hid_t control_handler = (code >> 8) & 0xff;
		ASSERT(input_handler < mm_event_hd_table_size);
		ASSERT(output_handler < mm_event_hd_table_size);
		ASSERT(control_handler < mm_event_hd_table_size);
		ASSERT(input_handler || output_handler);

#if MM_ONESHOT_HANDLERS
		uint8_t flags = code & 0xff;
		bool oneshot_input = (flags & MM_EVENT_MSG_ONESHOT_INPUT) != 0;
		bool oneshot_output = (flags & MM_EVENT_MSG_ONESHOT_OUTPUT) != 0;
		// Set the data field only for a REG message, not for a TRIGGER.
		if (mm_event_verify_handlers(input_handler, oneshot_input,
					     output_handler, oneshot_output,
					     control_handler))
			ev_fd->data = msg[2];
#else
		ev_fd->data = msg[2];
#endif

		if (input_handler) {
			ev_fd->input_handler = input_handler;

			int ev_flags;
#if MM_ONESHOT_HANDLERS
			if (oneshot_input) {
				ev_fd->oneshot_input = 1;
				ev_flags = EV_ADD | EV_ONESHOT;
			} else {
				ev_fd->oneshot_input = 0;
				ev_flags = EV_ADD | EV_CLEAR;
			}
#else
			ev_flags = EV_ADD | EV_CLEAR;
#endif

			DEBUG("register fd %d for read events", fd);
			ASSERT(mm_nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &events->kevents[events->nkevents++];
			EV_SET(kp, fd, EVFILT_READ, ev_flags, 0, 0, 0);
		}

		if (output_handler) {
			ev_fd->output_handler = output_handler;

			int ev_flags;
#if MM_ONESHOT_HANDLERS
			if (oneshot_output) {
				ev_fd->oneshot_output = 1;
				ev_flags = EV_ADD | EV_ONESHOT;
			} else {
				ev_fd->oneshot_output = 0;
				ev_flags = EV_ADD | EV_CLEAR;
			}
#else
			ev_flags = EV_ADD | EV_CLEAR;
#endif

			DEBUG("register fd %d for write events", fd);
			ASSERT(mm_nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &events->kevents[events->nkevents++];
			EV_SET(kp, fd, EVFILT_WRITE, ev_flags, 0, 0, 0);
		}

		if (control_handler) {
			ev_fd->changed = 1;
			ev_fd->control_handler = control_handler;

			events->changes[events->nchanges].fd = fd;
			events->changes[events->nchanges].event = MM_EVENT_REGISTER;
			events->nchanges++;
		}
	}
}

bool
mm_event_collect(struct mm_event_table *events)
{
	ENTER();

	events->nkevents = 0;
	events->nchanges = 0;

	// Pick a delayed change if any.
	if (unlikely(events->has_delayed_msg)) {
		events->has_delayed_msg = false;
		mm_event_process_msg(events, events->delayed_msg);
	}

	// Fill the change list.
	while (likely(events->nchanges < MM_EVENT_NCHANGES_MAX)) {

		// Get the message.
		uint32_t msg[3];
		if (mm_port_receive(events->port, msg, 3) < 0)
			break;

		// To simplify logic handle only one event related to a
		// particular fd per cycle.
		int fd = msg[0];
		struct mm_event_fd *ev_fd = &mm_event_fd_table[fd];
		if (unlikely(ev_fd->changed)) {
			events->delayed_msg[0] = msg[0];
			events->delayed_msg[1] = msg[1];
			events->delayed_msg[2] = msg[2];
			events->has_delayed_msg = true;
			break;
		}

		mm_event_process_msg(events, msg);
	}

	LEAVE();
	return (events->nkevents != 0);
}

bool
mm_event_poll(struct mm_event_table *events, mm_timeout_t timeout)
{
	ENTER();

	// Flush the log before a possible sleep.
	mm_flush();

	// Start listening the selfpipe if needed and find the event wait
	// timeout.
	struct timespec ts;
	DEBUG("timeout: %lu", (unsigned long) timeout);
	ts.tv_sec = timeout / 1000000;
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Poll the system for events.
	int n = kevent(events->kqueue_fd,
		       events->kevents, events->nkevents,
		       events->kevents, MM_EVENT_NKEVENTS_MAX,
		       &ts);

	DEBUG("kevent changed: %d, received: %d", events->nkevents, n);
	if (n < 0) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
		events->nkevents = 0;
	} else {
		events->nkevents = n;
	}

	LEAVE();
	return events->nchanges || events->nkevents;
}

void
mm_event_dispatch(struct mm_event_table *events)
{
	ENTER();

	// Issue REG/UNREG events.
	for (int i = 0; i < events->nchanges; i++) {
		int fd = events->changes[i].fd;

		// Reset the change flag.
		struct mm_event_fd *ev_fd = &mm_event_fd_table[fd];
		ev_fd->changed = 0;

		// Invoke the control handler with pertinent event.
		ASSERT(ev_fd->control_handler);
		if (events->changes[i].event == MM_EVENT_REGISTER) {
			mm_event_control(ev_fd, MM_EVENT_REGISTER);
		} else {
			mm_event_control(ev_fd, MM_EVENT_UNREGISTER);
			// Perform the final cleanup.
			ev_fd->control_handler = 0;
			ev_fd->data = 0;
		}
	}

	// Process the received system events.
	for (int i = 0; i < events->nkevents; i++) {
		if (events->kevents[i].filter == EVFILT_READ) {
			int fd = events->kevents[i].ident;
			DEBUG("input event on fd %d", fd);
			mm_event_input(&mm_event_fd_table[fd]);

			if ((events->kevents[i].flags & (EV_ERROR | EV_EOF)) != 0) {
				DEBUG("input error event on fd %d", fd);
				mm_event_control(&mm_event_fd_table[fd],
						 MM_EVENT_INPUT_ERROR);
			}
		} else if (events->kevents[i].filter == EVFILT_WRITE) {
			int fd = events->kevents[i].ident;
			DEBUG("output event on fd %d", fd);
			mm_event_output(&mm_event_fd_table[fd]);

			if ((events->kevents[i].flags & (EV_ERROR | EV_EOF)) != 0) {
				DEBUG("output error event on fd %d", fd);
				mm_event_control(&mm_event_fd_table[fd],
						 MM_EVENT_OUTPUT_ERROR);
			}
		}
	}

	LEAVE();
}

#endif // HAVE_SYS_EVENT_H

/**********************************************************************
 * Self-pipe support.
 **********************************************************************/

// Self pipe for event loop wakeup.
static mm_event_hid_t mm_event_selfpipe_handler;

static void
mm_event_selfpipe_ready(mm_event_t event __attribute__((unused)),
			uint32_t data __attribute__((unused)))
{
	struct mm_event_table *events = mm_core->events;
	mm_selfpipe_set_ready(&events->selfpipe);
}

static void
mm_event_init_selfpipe(void)
{
	ENTER();

	// Register the self-pipe event handler.
	mm_event_selfpipe_handler = mm_event_register_handler(mm_event_selfpipe_ready);

	LEAVE();
}

static void
mm_event_start_selfpipe(struct mm_event_table *events)
{
	ENTER();

	// Register the self-pipe read end in the event loop.
	mm_event_register_fd(events, events->selfpipe.read_fd, 0,
			     mm_event_selfpipe_handler, false,
			     0, false, 0);

	LEAVE();
}

/**********************************************************************
 * Event poll routines.
 **********************************************************************/

struct mm_event_table *
mm_event_create_table(void)
{
	ENTER();

	struct mm_event_table *events = mm_global_alloc(sizeof(struct mm_event_table));

	events->port = NULL;

	// Open the event-loop self-pipe.
	mm_selfpipe_prepare(&events->selfpipe);

	// Initialize system specific resources.
	mm_event_init_sys(events);

	LEAVE();
	return events;
}

void
mm_event_destroy_table(struct mm_event_table *events)
{
	ENTER();

	// TODO: destroy event port

	// Close the event-loop self-pipe.
	mm_selfpipe_cleanup(&events->selfpipe);

	// Release system specific resources.
	mm_event_free_sys(events);

	mm_global_free(events);

	LEAVE();
}

void
mm_event_start(struct mm_event_table *events)
{
	ENTER();

	// Create the event loop port.
	events->port = mm_port_create(mm_core->dealer);

	// Start serving the event loop self-pipe.
	mm_event_start_selfpipe(events);

	LEAVE();
}

void
mm_event_send(struct mm_event_table *events, int fd, uint32_t code, uint32_t data)
{
	TRACE("fd: %d", fd);
	ASSERT(fd >= 0);
	ASSERT(fd < mm_event_fd_table_size);

	uint32_t msg[3];
	msg[0] = (uint32_t) fd;
	msg[1] = code;
	msg[2] = data;

	mm_port_send_blocking(events->port, msg, 3);
}

void
mm_event_notify(struct mm_event_table *events)
{
	ENTER();

	mm_selfpipe_write(&events->selfpipe);

	LEAVE();
}

bool
mm_event_dampen(struct mm_event_table *events)
{
	ENTER();

	// Drain the stale selfpipe notifications if any.
	bool rc = mm_selfpipe_drain(&events->selfpipe);

	LEAVE();
	return rc;
}

/**********************************************************************
 * Event subsystem initialization and termination.
 **********************************************************************/

void
mm_event_init(void)
{
	ENTER();

	// Initialize generic data.
	mm_event_init_handlers();
	mm_event_init_fd_table();
	mm_event_init_selfpipe();

	LEAVE();
}

void
mm_event_term(void)
{
	ENTER();

	// Release generic data.
	mm_event_free_fd_table();

	LEAVE();
}
