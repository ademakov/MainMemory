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

/* Check if a kevent filter is either read or write filter. */
#define MM_IS_IOF(f) ((f) == EVFILT_READ || (f) == EVFILT_WRITE)

/**********************************************************************
 * I/O handler table.
 **********************************************************************/

/* I/0 handler table size. */
#define MM_IO_MAX 32

/* I/O handler. */
struct mm_io
{
	struct mm_port *read_ready;
	struct mm_port *write_ready;
};

/* I/O handler table. */
static struct mm_io mm_io_table[MM_IO_MAX];

/* The number of registered I/O handlers. */
static int mm_io_table_size;

/* Initialize the event handler table. */
static void
mm_event_init_io_handlers(void)
{
	ENTER();
	ASSERT(MM_IO_MAX < (1ul << (8 * sizeof(mm_io_handler))));

	/* Register a dummy I/O handler with zero id. */
	ASSERT(mm_io_table_size == 0);
	(void) mm_event_add_io_handler(NULL, NULL);
	ASSERT(mm_io_table_size == 1);

	LEAVE();
}

/* Register an I/O handler in the table. */
mm_io_handler
mm_event_add_io_handler(struct mm_port *read_ready_port,
			struct mm_port *write_ready_port)
{
	ENTER();

	ASSERT(mm_io_table_size < MM_IO_MAX);

	mm_io_handler id = mm_io_table_size++;
	mm_io_table[id].read_ready = read_ready_port;
	mm_io_table[id].write_ready = write_ready_port;

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
 * List of changed file descriptors.
 **********************************************************************/

/* File descriptor list. */
static int *mm_change_list;

/* Current size of the list. */
static int mm_change_list_size = 0;

/* Maximum size of the list. To accommodate malloc overhead make it
 * equal to (2^n - 2). */
static int mm_change_list_size_max = 510;

static void
mm_event_init_change_list(void)
{
	ENTER();

	mm_print("fd change list size: %d", mm_change_list_size_max);
	mm_change_list = mm_alloc(mm_change_list_size_max * sizeof(int));

	LEAVE();
}

static void
mm_event_free_change_list(void)
{
	ENTER();

	mm_free(mm_change_list);

	LEAVE();
}

static void
mm_event_grow_change_list(void)
{
	ENTER();

	/* Double the size and keep it equal to (2^n - 2). */
	mm_change_list_size_max *= 2;
	mm_change_list_size_max += 2;

	/* Reallocate the list. */
	mm_print("grow fd change list to size: %d", mm_change_list_size_max);
	mm_change_list = mm_realloc(mm_change_list,
				    mm_change_list_size_max * sizeof(int));

	LEAVE();
}

static void
mm_event_note_fd_change(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	if (unlikely(mm_change_list_size == mm_change_list_size_max))
		mm_event_grow_change_list();

	mm_change_list[mm_change_list_size++] = fd;

	LEAVE();
}

/**********************************************************************
 * File descriptor table.
 **********************************************************************/

/* The maximum allowed number of open file descriptors. */
#define MM_FD_MAX (256 * 1024)

/* File descriptor table entry. */
struct mm_fd
{
	/* event handler data */
	uint32_t data;
	/* file descriptor flags */
	uint16_t flags;
	/* current I/O handler id */
	mm_io_handler io;
	/* requested I/O handler id */
	mm_io_handler new_io;
};

/* File descriptor table. */
static struct mm_fd *mm_fd_table;

/* File descriptor table size. */
static int mm_fd_table_size = _POSIX_OPEN_MAX;

/* Initialize the file descriptor table. */
static void
mm_event_init_fd_table(void)
{
	ENTER();

	/* Determine the table size. */
	int max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd > mm_fd_table_size) {
		mm_fd_table_size = max_fd;
	}
	if (mm_fd_table_size > MM_FD_MAX) {
		mm_print("truncating too high fd limit: %d", mm_fd_table_size);
		mm_fd_table_size = MM_FD_MAX;
	}
	mm_print("fd table size: %d", mm_fd_table_size);

	/* Allocate the table. */
	mm_fd_table = mm_calloc(mm_fd_table_size, sizeof(struct mm_fd));

	LEAVE();
}

static void
mm_event_free_fd_table(void)
{
	ENTER();

	mm_free(mm_fd_table);

	LEAVE();
}

/* Verify if the file descriptor fits into the table. */
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

void
mm_event_register_fd(int fd, mm_io_handler io, uint32_t data)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);
	ASSERT(io != 0);
	ASSERT(io < mm_io_table_size);

	struct mm_fd *mm_fd = &mm_fd_table[fd];

	/* Add the fd to the change list if needed. */
	if (likely(mm_fd->io == 0 && mm_fd->new_io == 0)) {
		mm_event_note_fd_change(fd);
	}

	/* Store new handlers. */
	mm_fd->new_io = io;
	mm_fd->data = data;

	LEAVE();
}

void
mm_event_unregister_fd(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	struct mm_fd *mm_fd = &mm_fd_table[fd];

	if (likely(mm_fd->io != 0 && mm_fd->new_io != 0)) {
		mm_event_note_fd_change(fd);
	}

	mm_fd->new_io = 0;
	mm_fd->data = 0;

	LEAVE();
}

/**********************************************************************
 * epoll support.
 **********************************************************************/

#ifdef HAVE_SYS_EPOLL_H

static void
mm_event_init_epoll(void)
{
	ENTER();

	LEAVE();
}

static void
mm_event_free_epoll(void)
{
	ENTER();


	LEAVE();
}

static void
mm_event_dispatch(void)
{
}

#endif // HAVE_SYS_EPOLL_H

/**********************************************************************
 * kqueue/kevent support.
 **********************************************************************/

#ifdef HAVE_SYS_EVENT_H

/* The kqueue descriptor. */
static int mm_kq;

/* The kevent list. */
struct kevent *mm_kevents;

/* Current size of the kevent list. */
int mm_nkevents = 0;

/* Maximum size of the kevent list. To accommodate malloc overhead make it
 * equal to (2^n - 1). */
int mm_max_nkevents = 511;

static struct mm_kevent_list mm_ev;

static void
mm_event_init_kqueue(void)
{
	ENTER();

	mm_kq = kqueue();
	if (mm_kq == -1) {
		mm_fatal(errno, "Failed to create kqueue");
	}

 	mm_print("kevent list size: %d", mm_max_nkevents);
	mm_kevents = mm_alloc(mm_max_nkevents * sizeof(struct kevent));

	LEAVE();
}

static void
mm_event_free_kqueue(void)
{
	ENTER();

	if (mm_kq >= 0) {
		close(mm_kq);
	}

	mm_free(mm_kevents);

	LEAVE();
}

static void
mm_event_grow_kevents(void)
{
	ENTER();

	/* Double the size and keep it equal to (2^n - 1). */
	mm_max_nkevents *= 2;
	mm_max_nkevents += 1;

	/* Reallocate the list. */
 	mm_print("grow kevent list to size: %d", mm_max_nkevents);
	mm_kevents = mm_realloc(mm_kevents,
				mm_max_nkevents * sizeof(struct kevent));

	LEAVE();
}

static void
mm_event_ensure_kevents(void)
{
	ENTER();

	if (unlikely(mm_nkevents == mm_max_nkevents))
		mm_event_grow_kevents();

	LEAVE();
}

static void
mm_event_dispatch(void)
{
	ENTER();

	/* Form the kevent change list. */
	mm_nkevents = 0;
	for (int i = 0; i < mm_change_list_size; i++) {
		int fd = mm_change_list[i];
		struct mm_fd *mm_fd = &mm_fd_table[fd];
		int a, b;

		/* Change a read event registration if needed. */
		a = (mm_fd->io && mm_io_table[mm_fd->io].read_ready);
		b = (mm_fd->new_io && mm_io_table[mm_fd->new_io].read_ready);
		if (likely(a != b)) {
			int flags;
			if (b) {
				TRACE("register fd %d for read events", fd);
				flags = EV_ADD | EV_CLEAR;
			} else {
				TRACE("unregister fd %d for read events", fd);
				flags = EV_DELETE;
			}

			mm_event_ensure_kevents();
			struct kevent *kp = &mm_kevents[mm_nkevents++];
			EV_SET(kp, fd, EVFILT_READ, flags, 0, 0, 0);
		}

		/* Change a write event registration if needed. */
		a = (mm_fd->io && mm_io_table[mm_fd->io].write_ready);
		b = (mm_fd->new_io && mm_io_table[mm_fd->new_io].write_ready);
		if (likely(a != b)) {
			int flags;
			if (b) {
				DEBUG("register fd %d for write events", fd);
				flags = EV_ADD | EV_CLEAR;
			} else {
				DEBUG("unregister fd %d for write events", fd);
				flags = EV_DELETE;
			}

			mm_event_ensure_kevents();
			struct kevent *kp = &mm_kevents[mm_nkevents++];
			EV_SET(kp, fd, EVFILT_WRITE, flags, 0, 0, 0);
		}

		/* Store the requested I/O handler. */
		mm_fd->io = mm_fd->new_io;
	}

	DEBUG("event change count: %d", mm_nkevents);

	/* Poll the system for events. */
	mm_change_list_size = 0;
	mm_nkevents = kevent(mm_kq,
			     mm_kevents, mm_nkevents,
			     mm_kevents, mm_max_nkevents,
			     NULL);
	if (unlikely(mm_nkevents < 0)) {
		mm_error(errno, "kevent");
		goto done;
	}

	DEBUG("event count: %d", mm_nkevents);

	/* Process received system events. */
	for (int i = 0; i < mm_nkevents; i++) {
		if ((mm_kevents[i].flags & EV_ERROR) != 0) {

			int fd = mm_kevents[i].ident;
			DEBUG("error event on fd %d", fd);

		} else if (mm_kevents[i].filter == EVFILT_READ) {

			int fd = mm_kevents[i].ident;
			DEBUG("read event on fd %d", fd);

			mm_io_handler io = mm_fd_table[fd].io;
			ASSERT(io < mm_io_table_size);

			struct mm_io *handlers = &mm_io_table[io];
			if (likely(handlers->read_ready)) {
				mm_port_send_blocking(
					handlers->read_ready,
					&mm_fd_table[fd].data, 1);
			}
		} else if (mm_kevents[i].filter == EVFILT_WRITE) {

			int fd = mm_kevents[i].ident;
			DEBUG("write event on fd %d", fd);

			mm_io_handler io = mm_fd_table[fd].io;
			ASSERT(io < mm_io_table_size);

			struct mm_io *handlers = &mm_io_table[io];
			if (likely(handlers->write_ready)) {
				mm_port_send_blocking(
					handlers->write_ready,
					&mm_fd_table[fd].data, 1);
			}
		}
	}

	/* If the whole list was used up more space may be needed. */
	mm_event_ensure_kevents();

done:
	LEAVE();
}

#endif // HAVE_SYS_EVENT_H

/**********************************************************************
 * Event loop control.
 **********************************************************************/

/* Event loop exit flag. */
static volatile int mm_exit_loop = 0;

static struct mm_task *mm_event_task;

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

	// Initialize generic data.
	mm_event_init_io_handlers();
	mm_event_init_handlers();
	mm_event_init_change_list();
	mm_event_init_fd_table();

	// Initialize system specific data.
#ifdef HAVE_SYS_EPOLL_H
	mm_event_init_epoll();
#endif
#ifdef HAVE_SYS_EVENT_H
	mm_event_init_kqueue();
#endif

	// Create the event loop task.
	mm_event_task = mm_task_create("event-loop", 0, mm_event_loop, 0);

	LEAVE();
}

void
mm_event_term(void)
{
	ENTER();

	// Release the event loop task.
	mm_task_destroy(mm_event_task);

	// Release generic data.
	mm_event_free_change_list();
	mm_event_free_fd_table();

	// Release system specific data.
#ifdef HAVE_SYS_EPOLL_H
	mm_event_free_epoll();
#endif
#ifdef HAVE_SYS_EVENT_H
	mm_event_free_kqueue();
#endif

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
