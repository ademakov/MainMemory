/*
 * event.c - MainMemory event loop.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "event.h"

#include "port.h"
#include "sched.h"
#include "task.h"
#include "util.h"

#include <string.h>
#if HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif
#if HAVE_SYS_EVENT_H
# include <sys/event.h>
#endif
#include <sys/types.h>
#include <unistd.h>

// Event loop task.
static struct mm_task *mm_event_task;

// Event loop port.
static struct mm_port *mm_event_port;

/**********************************************************************
 * I/O handler table.
 **********************************************************************/

// I/0 handler table size.
#define MM_IO_MAX 32

// I/O handler descriptor.
struct mm_event_io_handler
{
	int flags;
	struct mm_port *port;
};

// I/O handler table.
static struct mm_event_io_handler mm_io_table[MM_IO_MAX];

// The number of registered I/O handlers.
static int mm_io_table_size;

// Initialize the event handler table.
static void
mm_event_init_io_handlers(void)
{
	ENTER();
	ASSERT(MM_IO_MAX < (1ul << (8 * sizeof(mm_event_handler_t))));

	// Register a dummy I/O handler with zero id.
	ASSERT(mm_io_table_size == 0);
	(void) mm_event_add_io_handler(0, NULL);
	ASSERT(mm_io_table_size == 1);

	LEAVE();
}

// Register an I/O handler in the table.
mm_event_handler_t
mm_event_add_io_handler(int flags, struct mm_port *port)
{
	ENTER();

	ASSERT(mm_io_table_size < MM_IO_MAX);

	mm_event_handler_t id = mm_io_table_size++;
	mm_io_table[id].flags = flags;
	mm_io_table[id].port = port;

	DEBUG("registered I/O handler %d", id);

	LEAVE();
	return id;
}

/**********************************************************************
 * Event handler table.
 **********************************************************************/

/* Event handler table size. */
#define MM_CB_MAX 64

/* Event handler table. */
static mm_handler mm_cb_table[MM_CB_MAX];

/* The number of registered event handlers. */
static int mm_cb_table_size;

/* A dummy event handler. */
static void
mm_event_dummy(uintptr_t ident __attribute__((unused)),
	       uint32_t data __attribute__((unused)))
{
	DEBUG("hmm, dummy event handler invoked.");
}

/* Initialize the event handler table. */
static void
mm_event_init_handlers(void)
{
	ENTER();
	ASSERT(MM_CB_MAX < 256);

	/* Register dummy handler with zero id. */
	ASSERT(mm_cb_table_size == 0);
	(void) mm_event_install_handler(mm_event_dummy);
	ASSERT(mm_cb_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_handler_id
mm_event_install_handler(mm_handler cb)
{
	ENTER();

	ASSERT(cb != NULL);
	ASSERT(mm_cb_table_size < MM_CB_MAX);

	mm_handler_id id = mm_cb_table_size++;
	mm_cb_table[id] = cb;

	DEBUG("registered event handler %d", id);

	LEAVE();
	return id;
}

/**********************************************************************
 * File descriptor table.
 **********************************************************************/

// The maximum allowed number of open file descriptors.
#define MM_EVENT_FD_MAX (1024 * 1024)

// File descriptor table entry.
struct mm_event_fd
{
	uint32_t data;			// handler data
	mm_event_handler_t handler;	// handler id
	uint8_t changed;		// change flag
	uint16_t reserved;
};

// File descriptor table.
static struct mm_event_fd *mm_fd_table;

// File descriptor table size.
static int mm_fd_table_size = _POSIX_OPEN_MAX;

// Initialize the file descriptor table.
static void
mm_event_init_fd_table(void)
{
	ENTER();

	// Determine the table size.
	int max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd > mm_fd_table_size) {
		mm_fd_table_size = max_fd;
	}
	if (mm_fd_table_size > MM_EVENT_FD_MAX) {
		mm_print("truncating too high fd limit: %d", mm_fd_table_size);
		mm_fd_table_size = MM_EVENT_FD_MAX;
	}
	mm_print("fd table size: %d", mm_fd_table_size);

	// Allocate the table.
	mm_fd_table = mm_calloc(mm_fd_table_size, sizeof(struct mm_event_fd));

	LEAVE();
}

// Free the file descriptor table.
static void
mm_event_free_fd_table(void)
{
	ENTER();

	mm_free(mm_fd_table);

	LEAVE();
}

// Verify if the file descriptor fits into the table.
int
mm_event_verify_fd(int fd)
{
	if (unlikely(fd < 0)) {
		/* The fd is invalid. */
		return MM_FD_INVALID;
	} else if (likely(fd < mm_fd_table_size)) {
		/* The fd is okay. */
		return MM_FD_VALID;
	} else {
		/* The fd exceeds the table capacity. */
		return MM_FD_TOO_BIG;
	}
}

#if 0
void
mm_event_register_fd(int fd, mm_event_handler_t io, uint32_t data)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);
	ASSERT(io != 0);
	ASSERT(io < mm_io_table_size);

	struct mm_event_fd *mm_event_fd = &mm_fd_table[fd];

	if (likely(mm_event_fd->handler != io)) {

		// Add the fd to the change list if needed.
		if (likely((mm_event_fd->flags & MM_EVENT_FD_CHANGE) == 0)) {
			mm_event_fd->flags |= MM_EVENT_FD_CHANGE;
			mm_event_note_fd_change(fd);
		}

		// Store the handler.
		mm_event_fd->new_io = io;
	}

	// Store the handler data.
	mm_event_fd->data = data;

	LEAVE();
}

void
mm_event_unregister_fd(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	struct mm_event_fd *mm_event_fd = &mm_fd_table[fd];

	if (likely(mm_event_fd->handler != 0)) {

		// Add the fd to the change list if needed.
		if (likely((mm_event_fd->flags & MM_EVENT_FD_CHANGE) == 0)) {
			mm_event_fd->flags |= MM_EVENT_FD_CHANGE;
			mm_event_note_fd_change(fd);
		}

		// Clear the handler.
		mm_event_fd->new_io = 0;
	}

	LEAVE();
}
#endif

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
		mm_event_handler_t handler = msg[1];

		struct mm_event_fd *mm_fd = &mm_fd_table[fd];
		uint32_t events = 0, new_events = 0;
		int a, b;

		// Check if a read event registration if needed.
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_IO_READ);
		b = (mm_io_table[handler].flags & MM_EVENT_IO_READ);
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
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_IO_WRITE);
		b = (mm_io_table[handler].flags & MM_EVENT_IO_WRITE);
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
			uint32_t msg[2] = { MM_EVENT_IO_UNREG, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
			sent_msgs = true;
		}

		// Store the requested I/O handler.
		mm_fd->data = data;
		mm_fd->handler = handler;

		if (mm_fd->handler) {
			uint32_t msg[2] = { MM_EVENT_IO_REG, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
			sent_msgs = true;
		}
	}

	// Poll the system for events.
	int nevents = epoll_wait(mm_epoll_fd, mm_epoll_events, MM_EPOLL_MAX,
				 sent_msgs ? 0 : 10 * 1000000);
	if (unlikely(nevents < 0)) {
		mm_error(errno, "epoll_wait");
		goto done;
	}

	// Process the received system events.
	for (int i = 0; i < nevents; i++) {
		if ((mm_epoll_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			mm_error(0, "error event on fd %d", fd);
		}
		if ((mm_epoll_events[i].events & EPOLLIN) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			DEBUG("read event on fd %d", fd);

			mm_event_handler_t io = mm_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);

			struct mm_event_io_handler *handler = &mm_io_table[io];
			uint32_t data[2] = { MM_EVENT_IO_READ, mm_fd_table[fd].data };
			mm_port_send_blocking( handler->port, data, 2);
		}
		if ((mm_epoll_events[i].events & EPOLLOUT) != 0) {
			int fd = mm_epoll_events[i].data.fd;
			DEBUG("write event on fd %d", fd);

			mm_event_handler_t io = mm_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);

			struct mm_event_io_handler *handler = &mm_io_table[io];
			uint32_t data[2] = { MM_EVENT_IO_WRITE, mm_fd_table[fd].data };
			mm_port_send_blocking( handler->port, data, 2);
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
	mm_event_handler_t handler;
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

	// Indicate if there were any events sent before the poll call.
	bool sent_msgs = false;

	// Pick the delayed change if any.
	if (mm_event_fd_delayed_is_set) {
		mm_event_fd_delayed_is_set = false;
		mm_event_fd_changes[nchanges++] = mm_event_fd_delayed;
	}

	// Fill the change list.
	uint32_t msg[3];
	while (mm_port_receive(mm_event_port, msg, 3) == 0) {
		int fd = msg[0];
		uint32_t data = msg[2];
		mm_event_handler_t handler = msg[1];

		struct mm_event_fd *mm_fd = &mm_fd_table[fd];
		if (unlikely(mm_fd->changed)) {
			mm_event_fd_delayed.fd = fd;
			mm_event_fd_delayed.data = data;
			mm_event_fd_delayed.handler = handler;
			mm_event_fd_delayed_is_set = true;
			break;
		}
		mm_fd->changed = 1;

		mm_event_fd_changes[nchanges].fd = fd;
		mm_event_fd_changes[nchanges].data = data;
		mm_event_fd_changes[nchanges].handler = handler;
		++nchanges;

		if (unlikely(nchanges == MM_EVENT_NCHANGES_MAX)) {
			break;
		}

		int a, b;

		// Change a read event registration if needed.
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_IO_READ);
		b = (mm_io_table[handler].flags & MM_EVENT_IO_READ);
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
		a = (mm_io_table[mm_fd->handler].flags & MM_EVENT_IO_WRITE);
		b = (mm_io_table[handler].flags & MM_EVENT_IO_WRITE);
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

	DEBUG("event change count: %d", nkevents);

	// Poll the system for events.
	struct timespec timeout;
	timeout.tv_sec = nkevents ? 0 : 10;
	timeout.tv_nsec = 0;
	nkevents = kevent(mm_event_kq,
			  mm_kevents, nkevents,
			  mm_kevents, MM_EVENT_NKEVENTS_MAX,
			  &timeout);

	// Send REG/UNREG messages.
	for (int i = 0; i < nchanges; i++) {
		int fd = mm_event_fd_changes[i].fd;
		uint32_t data = mm_event_fd_changes[i].data;
		mm_event_handler_t handler = mm_event_fd_changes[i].handler;

		struct mm_event_fd *mm_fd = &mm_fd_table[fd];
		mm_fd->changed = 0;

		if (mm_fd->handler) {
			uint32_t msg[2] = { MM_EVENT_IO_UNREG, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
		}

		// Store the requested I/O handler.
		mm_fd->data = mm_event_fd_changes[i].data;
		mm_fd->handler = mm_event_fd_changes[i].handler;

		if (mm_fd->handler) {
			uint32_t msg[2] = { MM_EVENT_IO_REG, mm_fd->data };
			struct mm_event_io_handler *io = &mm_io_table[mm_fd->handler];
			mm_port_send_blocking(io->port, msg, 2);
		}
	}

	if (unlikely(nkevents < 0)) {
		if (errno != EINTR)
			mm_error(errno, "kevent");
		goto done;
	}

	DEBUG("event count: %d", nkevents);

	// Process the received system events.
	for (int i = 0; i < nkevents; i++) {
		if ((mm_kevents[i].flags & EV_ERROR) != 0) {

			int fd = mm_kevents[i].ident;
			mm_error(mm_kevents[i].data, "error event on fd %d", fd);

		} else if (mm_kevents[i].filter == EVFILT_READ) {
			int fd = mm_kevents[i].ident;
			DEBUG("read event on fd %d", fd);

			mm_event_handler_t io = mm_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);

			struct mm_event_io_handler *handler = &mm_io_table[io];
			uint32_t data[2] = { MM_EVENT_IO_READ, mm_fd_table[fd].data };
			mm_port_send_blocking( handler->port, data, 2);
		} else if (mm_kevents[i].filter == EVFILT_WRITE) {
			int fd = mm_kevents[i].ident;
			DEBUG("write event on fd %d", fd);

			mm_event_handler_t io = mm_fd_table[fd].handler;
			ASSERT(io < mm_io_table_size);

			struct mm_event_io_handler *handler = &mm_io_table[io];
			uint32_t data[2] = { MM_EVENT_IO_WRITE, mm_fd_table[fd].data };
			mm_port_send_blocking( handler->port, data, 2);
		}
	}

done:
	LEAVE();
}

#endif // HAVE_SYS_EVENT_H

/**********************************************************************
 * Event loop control.
 **********************************************************************/

// Event loop exit flag.
static volatile int mm_exit_loop = 0;

static void
mm_event_loop(uintptr_t arg __attribute__((unused)))
{
	ENTER();

	while (!mm_exit_loop) {
		mm_event_dispatch();
		mm_sched_yield();
	}

	LEAVE();
}

void
mm_event_init(void)
{
	ENTER();

	// Initialize system specific resources.
	mm_event_init_sys();

	// Initialize generic data.
	mm_event_init_io_handlers();
	mm_event_init_handlers();
	mm_event_init_fd_table();

	// Create the event loop task and port.
	mm_event_task = mm_task_create("event-loop", 0, mm_event_loop, 0);
	mm_event_port = mm_port_create(mm_event_task);

	// Set the lowest priority for event loop.
	mm_event_task->priority = MM_PRIO_LOWEST;

	LEAVE();
}

void
mm_event_term(void)
{
	ENTER();

	// Release the event loop task.
	mm_task_destroy(mm_event_task);

	// Release generic data.
	mm_event_free_fd_table();

	// Release system specific resources.
	mm_event_free_sys();

	LEAVE();
}

void
mm_event_start(void)
{
	ENTER();

	mm_sched_run(mm_event_task);

	LEAVE();
}

void
mm_event_stop(void)
{
	ENTER();

	mm_exit_loop = 1;

	LEAVE();
}

void
mm_event_register_fd(int fd, mm_event_handler_t handler, uint32_t data)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);
	ASSERT(handler != 0);
	ASSERT(handler < mm_io_table_size);

	uint32_t msg[3];
	msg[0] = (uint32_t) fd;
	msg[1] = handler;
	msg[2] = data;

	mm_port_send_blocking(mm_event_port, msg, 3);

	LEAVE();
}

void
mm_event_unregister_fd(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	uint32_t msg[3];
	msg[0] = (uint32_t) fd;
	msg[1] = 0;
	msg[2] = 0;

	mm_port_send_blocking(mm_event_port, msg, 3);

	LEAVE();
}
