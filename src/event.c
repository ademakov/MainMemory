/*
 * event.c - MainMemory event loop.
 *
 * Copyright (C) 2012  Aleksey Demakov
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
#include "sched.h"
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

// Default event loop timeout - 10 seconds 
#define MM_EVENT_TIMEOUT ((mm_timeout_t) 10000000)

// Event loop task.
static struct mm_task *mm_event_task;

// Event loop port.
static struct mm_port *mm_event_port;

/**********************************************************************
 * Event Handler Table.
 **********************************************************************/

// Event handler table size.
#define MM_EVENT_HANDLER_MAX	(255)

// Event handler descriptor.
struct mm_event_hd
{
	mm_event_handler_t handler;
	uintptr_t handler_data;
};

// Event handler table.
static struct mm_event_hd mm_event_hd_table[MM_EVENT_HANDLER_MAX];

// The number of registered event handlers.
static int mm_event_hd_table_size;

// A dummy event handler.
static void
mm_event_dummy(mm_event_t event __attribute__((unused)),
	       uintptr_t handler_data __attribute__((unused)),
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
	(void) mm_event_register_handler(mm_event_dummy, 0);
	ASSERT(mm_event_hd_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_event_hid_t
mm_event_register_handler(mm_event_handler_t handler, uintptr_t handler_data)
{
	ENTER();

	ASSERT(handler != NULL);
	ASSERT(mm_event_hd_table_size < MM_EVENT_HANDLER_MAX);

	mm_event_hid_t id = mm_event_hd_table_size++;
	mm_event_hd_table[id].handler = handler;
	mm_event_hd_table[id].handler_data = handler_data;

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

	// change flag
	uint8_t changed;
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
	if (max_fd > mm_event_fd_table_size) {
		mm_event_fd_table_size = max_fd;
	}
	if (mm_event_fd_table_size > MM_EVENT_FD_MAX) {
		mm_brief("truncating too high fd limit: %d", mm_event_fd_table_size);
		mm_event_fd_table_size = MM_EVENT_FD_MAX;
	}
	mm_brief("fd table size: %d", mm_event_fd_table_size);

	// Allocate the table.
	mm_event_fd_table = mm_calloc(mm_event_fd_table_size, sizeof(struct mm_event_fd));

	LEAVE();
}

// Free the file descriptor table.
static void
mm_event_free_fd_table(void)
{
	ENTER();

	mm_free(mm_event_fd_table);

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

void
mm_event_input(struct mm_event_fd *fd)
{
	mm_event_hid_t id = fd->input_handler;
	ASSERT(id < mm_event_hd_table_size);

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(MM_EVENT_INPUT, hd->handler_data, fd->data);
}

void
mm_event_output(struct mm_event_fd *fd)
{
	mm_event_hid_t id = fd->output_handler;
	ASSERT(id < mm_event_hd_table_size);

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(MM_EVENT_OUTPUT, hd->handler_data, fd->data);
}

void
mm_event_control(struct mm_event_fd *fd, mm_event_t event)
{
	mm_event_hid_t id = fd->control_handler;
	ASSERT(id < mm_event_hd_table_size);

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	hd->handler(event, hd->handler_data, fd->data);
}

/**********************************************************************
 * Self-pipe support.
 **********************************************************************/

// Self pipe for event loop wakeup.
static struct mm_selfpipe mm_event_selfpipe;
static mm_event_hid_t mm_event_selfpipe_handler;

static void
mm_event_selfpipe_ready(mm_event_t event __attribute__((unused)),
			uintptr_t handler_data __attribute__((unused)),
			uint32_t data __attribute__((unused)))
{
	mm_selfpipe_set_ready(&mm_event_selfpipe);
}

static void
mm_event_init_selfpipe(void)
{
	ENTER();

	// Open the event-loop self-pipe.
	mm_selfpipe_prepare(&mm_event_selfpipe);

	// Register the self-pipe event handler.
	mm_event_selfpipe_handler = mm_event_register_handler(mm_event_selfpipe_ready, 0);

	LEAVE();
}

static void
mm_event_start_selfpipe(void)
{
	ENTER();

	// Register the self-pipe read end in the event loop.
	mm_event_register_fd(mm_event_selfpipe.read_fd, 0, mm_event_selfpipe_handler, 0, 0);

	LEAVE();
}

static void
mm_event_term_selfpipe(void)
{
	ENTER();

	// Close the event-loop self-pipe.
	mm_selfpipe_cleanup(&mm_event_selfpipe);

	LEAVE();
}

void
mm_event_notify(void)
{
	ENTER();

	mm_selfpipe_notify(&mm_event_selfpipe);

	LEAVE();
}

/**********************************************************************
 * epoll support.
 **********************************************************************/

#ifdef HAVE_SYS_EPOLL_H

#define MM_EPOLL_MAX 512

static int mm_epoll_fd;

static struct epoll_event mm_epoll_events[MM_EPOLL_MAX];

static void
mm_event_init_sys(void)
{
	ENTER();

	mm_epoll_fd = epoll_create(511);
	if (mm_epoll_fd < 0)
		mm_fatal(errno, "Failed to create epoll fd");

	LEAVE();
}

static void
mm_event_free_sys(void)
{
	ENTER();

	if (mm_epoll_fd >= 0)
		close(mm_epoll_fd);

	LEAVE();
}

static void
mm_event_dispatch(void)
{
	ENTER();

	// Indicate if there were any events sent before the poll call.
	bool sent_msgs = false;

	// Make changes to the fd set watched by epoll.
	uint32_t msg[3];
	while (mm_port_receive(mm_event_port, msg, 3) == 0) {
		int fd = msg[0];
		uint32_t data = msg[2];
		mm_event_hid_t handler = msg[1];

		struct mm_event_fd *mm_fd = &mm_event_fd_table[fd];
		uint32_t events = 0, new_events = 0;
		int a, b;

		// Check if a read event registration if needed.
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_READ);
		b = (mm_io_table[handler].flags & MM_EVENT_READ);
		if (a) {
			events |= EPOLLIN;
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
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_WRITE);
		b = (mm_io_table[handler].flags & MM_EVENT_WRITE);
		if (a) {
			events |= EPOLLOUT;
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

		if (likely(events != new_events)) {
			int op;
			if (new_events == 0)
				op = EPOLL_CTL_DEL;
			else if (events == 0)
				op = EPOLL_CTL_ADD;
			else
				op = EPOLL_CTL_MOD;

			struct epoll_event ev;
			ev.events = new_events | EPOLLET | EPOLLRDHUP;
			ev.data.fd = fd;

			int rc = epoll_ctl(mm_epoll_fd, op, fd, &ev);
			if (unlikely(rc < 0)) {
				mm_error(errno, "epoll_ctl");
			}
		}

		if (mm_fd->handler) {
			uint32_t msg[2] = { MM_NET_MSG_UNREGISTER, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
			sent_msgs = true;
		}

		// Store the requested I/O handler.
		mm_fd->data = data;
		mm_fd->handler = handler;

		if (mm_fd->handler) {
			uint32_t msg[2] = { MM_NET_MSG_REGISTER, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
			sent_msgs = true;
		}
	}

	// Flush the log before possible sleep.
	mm_flush();

	// Find the event wait timeout.
	mm_timeval_t timeout; 
	if (mm_selfpipe_listen(&mm_event_selfpipe) || sent_msgs) {
		// If event system changes have been requested it is needed to
		// notify the interested parties on their completion so do not
		// wait for more events.
		timeout = 0;
	} else {
		timeout = mm_timer_next();
		if (timeout > MM_EVENT_TIMEOUT) {
			timeout = MM_EVENT_TIMEOUT;
		}
		timeout /= 1000;
	}

	// Poll the system for events.
	int nevents = epoll_wait(mm_epoll_fd, mm_epoll_events, MM_EPOLL_MAX,
				 timeout);

	mm_selfpipe_divert(&mm_event_selfpipe);

	if (unlikely(nevents < 0)) {
		mm_error(errno, "epoll_wait");
		goto done;
	}

	// Process the received system events.
	for (int i = 0; i < nevents; i++) {
		if ((mm_epoll_events[i].events & EPOLLIN) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			mm_event_hid_t io = mm_event_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);
			struct mm_event_io_handler *handler = &mm_io_table[io];

			DEBUG("read event on fd %d", fd);
			uint32_t msg[2] = { MM_NET_MSG_READ_READY, mm_event_fd_table[fd].data };
			mm_port_send_blocking( handler->port, msg, 2);
		}
		if ((mm_epoll_events[i].events & EPOLLOUT) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			mm_event_hid_t io = mm_event_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);
			struct mm_event_io_handler *handler = &mm_io_table[io];

			DEBUG("write event on fd %d", fd);
			uint32_t msg[2] = { MM_NET_MSG_WRITE_READY, mm_event_fd_table[fd].data };
			mm_port_send_blocking( handler->port, msg, 2);
		}

		if ((mm_epoll_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			mm_event_hid_t io = mm_event_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);
			struct mm_event_io_handler *handler = &mm_io_table[io];

			DEBUG("error event on fd %d", fd);
			uint32_t msg[2] = { MM_NET_MSG_READ_ERROR, mm_event_fd_table[fd].data };
			mm_port_send_blocking( handler->port, msg, 2);
		}
		if ((mm_epoll_events[i].events & (EPOLLERR | EPOLLHUP)) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			mm_event_hid_t io = mm_event_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);
			struct mm_event_io_handler *handler = &mm_io_table[io];

			DEBUG("error event on fd %d", fd);
			uint32_t msg[2] = { MM_NET_MSG_WRITE_ERROR, mm_event_fd_table[fd].data };
			mm_port_send_blocking( handler->port, msg, 2);
		}
	}

done:
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
struct mm_event_fd_change_rec
{
	int fd;
	uint32_t data;
	mm_event_hid_t input_handler;
	mm_event_hid_t output_handler;
	mm_event_hid_t control_handler;
};

// The kqueue descriptor.
static int mm_event_kq;

// The kevent list.
static struct kevent mm_kevents[MM_EVENT_NKEVENTS_MAX];

// File descriptor change list.
static struct mm_event_fd_change_rec mm_event_fd_changes[MM_EVENT_NCHANGES_MAX];

// A delayed file descriptor change record.
static struct mm_event_fd_change_rec mm_event_fd_delayed;
static bool mm_event_fd_delayed_is_set = false;

static void
mm_event_init_sys(void)
{
	ENTER();

	mm_event_kq = kqueue();
	if (mm_event_kq == -1) {
		mm_fatal(errno, "Failed to create kqueue");
	}

	LEAVE();
}

static void
mm_event_free_sys(void)
{
	ENTER();

	if (mm_event_kq >= 0) {
		close(mm_event_kq);
	}

	LEAVE();
}

static void __attribute__((noinline))
mm_event_dispatch(void)
{
	ENTER();

	// The change list size.
	int nchanges = 0;

	// The kevent list size.
	int nkevents = 0;

	// Pick the delayed change if any.
	if (mm_event_fd_delayed_is_set) {
		mm_event_fd_delayed_is_set = false;
		mm_event_fd_changes[nchanges++] = mm_event_fd_delayed;
	}

	// Fill the change list.
	uint32_t msg[3];
	while (likely(nchanges < MM_EVENT_NCHANGES_MAX) 
	       && mm_port_receive(mm_event_port, msg, 3) == 0) {

		int fd = msg[0];
		uint32_t data = msg[1];
		mm_event_hid_t input_handler = (msg[2] >> 16) & 0xff;
		mm_event_hid_t output_handler = (msg[2] >> 8) & 0xff;
		mm_event_hid_t control_handler = msg[2] & 0xff;

		struct mm_event_fd *mm_fd = &mm_event_fd_table[fd];
		if (unlikely(mm_fd->changed)) {
			mm_event_fd_delayed.fd = fd;
			mm_event_fd_delayed.data = data;
			mm_event_fd_delayed.input_handler = input_handler;
			mm_event_fd_delayed.output_handler = output_handler;
			mm_event_fd_delayed.control_handler = control_handler;
			mm_event_fd_delayed_is_set = true;
			break;
		}

		mm_fd->changed = 1;

		mm_event_fd_changes[nchanges].fd = fd;
		mm_event_fd_changes[nchanges].data = data;
		mm_event_fd_changes[nchanges].input_handler = input_handler;
		mm_event_fd_changes[nchanges].output_handler = output_handler;
		mm_event_fd_changes[nchanges].control_handler = control_handler;
		++nchanges;

		int a, b;

		// Change a read event registration if needed.
		a = (mm_fd->input_handler != 0);
		b = (input_handler != 0);
		if (likely(a != b)) {
			int flags;
			if (b) {
				DEBUG("register fd %d for read events", fd);
				flags = EV_ADD | EV_CLEAR;
			} else {
				DEBUG("unregister fd %d for read events", fd);
				flags = EV_DELETE;
			}

			ASSERT(nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &mm_kevents[nkevents++];
			EV_SET(kp, fd, EVFILT_READ, flags, 0, 0, 0);
		}

		// Change a write event registration if needed.
		a = (mm_fd->output_handler != 0);
		b = (output_handler != 0);
		if (likely(a != b)) {
			int flags;
			if (b) {
				DEBUG("register fd %d for write events", fd);
				flags = EV_ADD | EV_CLEAR;
			} else {
				DEBUG("unregister fd %d for write events", fd);
				flags = EV_DELETE;
			}

			ASSERT(nkevents < MM_EVENT_NKEVENTS_MAX);
			struct kevent *kp = &mm_kevents[nkevents++];
			EV_SET(kp, fd, EVFILT_WRITE, flags, 0, 0, 0);
		}
	}

	// Flush the log before possible sleep.
	mm_flush();

	// Find the event wait timeout.
	struct timespec ts;
	if (mm_selfpipe_listen(&mm_event_selfpipe) || nkevents) {
		// If event system changes have been requested it is needed to
		// notify the interested parties on their completion so do not
		// wait for more events.
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	} else {
		mm_timeval_t timeout = mm_timer_next();
		if (timeout > MM_EVENT_TIMEOUT) {
			timeout = MM_EVENT_TIMEOUT;
		}

		ts.tv_sec = timeout / 1000000;
		ts.tv_nsec = (timeout % 1000000) * 1000;
	}

	// Poll the system for events.
	int n = kevent(mm_event_kq,
		       mm_kevents, nkevents,
		       mm_kevents, MM_EVENT_NKEVENTS_MAX,
		       &ts);

	mm_selfpipe_divert(&mm_event_selfpipe);

	DEBUG("kevent changed: %d, received: %d", nkevents, n);

	// Send REG/UNREG messages.
	for (int i = 0; i < nchanges; i++) {
		int fd = mm_event_fd_changes[i].fd;
		struct mm_event_fd *mm_fd = &mm_event_fd_table[fd];
		mm_fd->changed = 0;

		// Invoke the old handler if any.
		mm_event_control(mm_fd, MM_EVENT_UNREGISTER);

		// Store the requested I/O handler.
		mm_fd->data = mm_event_fd_changes[i].data;
		mm_fd->input_handler = mm_event_fd_changes[i].input_handler;
		mm_fd->output_handler = mm_event_fd_changes[i].output_handler;
		mm_fd->control_handler = mm_event_fd_changes[i].control_handler;

		// Invoke the new handler if any.
		mm_event_control(mm_fd, MM_EVENT_REGISTER);
	}

	if (unlikely(n < 0)) {
		if (errno != EINTR)
			mm_error(errno, "kevent");
		goto leave;
	}

	// Process the received system events.
	for (int i = 0; i < n; i++) {
		if (mm_kevents[i].filter == EVFILT_READ) {
			int fd = mm_kevents[i].ident;
			DEBUG("input event on fd %d", fd);
			mm_event_input(&mm_event_fd_table[fd]);

			if ((mm_kevents[i].flags & (EV_ERROR | EV_EOF)) != 0) {
				DEBUG("input error event on fd %d", fd);
				mm_event_control(&mm_event_fd_table[fd],
						 MM_EVENT_INPUT_ERROR);
			}
		} else if (mm_kevents[i].filter == EVFILT_WRITE) {
			int fd = mm_kevents[i].ident;
			DEBUG("output event on fd %d", fd);
			mm_event_output(&mm_event_fd_table[fd]);

			if ((mm_kevents[i].flags & (EV_ERROR | EV_EOF)) != 0) {
				DEBUG("output error event on fd %d", fd);
				mm_event_control(&mm_event_fd_table[fd],
						 MM_EVENT_OUTPUT_ERROR);
			}
		}
	}

leave:
	LEAVE();
}

#endif // HAVE_SYS_EVENT_H

/**********************************************************************
 * Event loop control.
 **********************************************************************/

static mm_result_t
mm_event_loop(uintptr_t arg __attribute__((unused)))
{
	ENTER();

	while (!mm_exit_test()) {
		mm_event_dispatch();
		mm_timer_tick();
		mm_sched_yield();
	}

	LEAVE();
	return 0;
}

static void
mm_event_start(void)
{
	ENTER();

	// Create the event loop task and port.
	mm_event_task = mm_task_create("event", 0, mm_event_loop, 0);
	mm_event_port = mm_port_create(mm_event_task);

	// Set the lowest priority for event loop.
	mm_event_task->priority = MM_PRIO_LOWEST;

	// Schedule the task.
	mm_sched_run(mm_event_task);

	// Start serving the event loop self-pipe.
	mm_event_start_selfpipe();

	LEAVE();
}

void
mm_event_init(void)
{
	ENTER();

	// Initialize system specific resources.
	mm_event_init_sys();

	// Initialize generic data.
	mm_event_init_handlers();
	mm_event_init_fd_table();
	mm_event_init_selfpipe();

	// Delayed event loop task start.
	mm_core_hook_start(mm_event_start);

	LEAVE();
}

void
mm_event_term(void)
{
	ENTER();

	// Release the event loop task.
	mm_task_destroy(mm_event_task);

	// Release generic data.
	mm_event_term_selfpipe();
	mm_event_free_fd_table();

	// Release system specific resources.
	mm_event_free_sys();

	LEAVE();
}

void
mm_event_register_fd(int fd,
		     uint32_t data,
		     mm_event_hid_t input_handler,
		     mm_event_hid_t output_handler,
		     mm_event_hid_t control_handler)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_event_fd_table_size);
	ASSERT(input_handler < mm_event_hd_table_size);
	ASSERT(output_handler < mm_event_hd_table_size);
	ASSERT(control_handler < mm_event_hd_table_size);

	uint32_t msg[3];
	msg[0] = (uint32_t) fd;
	msg[1] = data;
	msg[2] = (input_handler << 16) | (output_handler << 8) | control_handler;

	mm_port_send_blocking(mm_event_port, msg, 3);

	LEAVE();
}

void
mm_event_unregister_fd(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_event_fd_table_size);

	uint32_t msg[3];
	msg[0] = (uint32_t) fd;
	msg[1] = 0;
	msg[2] = 0;

	mm_port_send_blocking(mm_event_port, msg, 3);

	LEAVE();
}
