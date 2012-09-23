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

#include <event.h>

#include <util.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

/* Check if a kevent filter is either read or write filter. */
#define MM_IS_IOF(f) ((f) == EVFILT_READ || (f) == EVFILT_WRITE)

/* Event loop exit flag. */
static volatile int mm_exit_loop = 0;

/**********************************************************************
 * Event handler table.
 **********************************************************************/

/* Event handler table size. */
#define MM_CB_MAX 64

/* Event handler table. */
static mm_event_cb mm_cb_table[MM_CB_MAX];

/* The number of registered event handlers. */
static int mm_cb_table_size;

/* A dummy event handler. */
static void
mm_event_dummy(mm_event event __attribute__((unused)),
	       uintptr_t ident __attribute__((unused)),
	       uint32_t data __attribute__((unused)))
{
	DEBUG("hmm, dummy event handler invoked.");
}

/* Initialize the event handler table. */
static void
mm_event_init_cb_table(void)
{
	ENTER();
	ASSERT(MM_CB_MAX < 256);

	/* Register dummy handler with zero id. */
	ASSERT(mm_cb_table_size == 0);
	(void) mm_event_register_cb(mm_event_dummy);
	ASSERT(mm_cb_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_event_id
mm_event_register_cb(mm_event_cb cb)
{
	ENTER();

	ASSERT(cb != NULL);
	ASSERT(mm_cb_table_size < MM_CB_MAX);

	mm_event_id id = mm_cb_table_size++;
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

/* Maximal size of the list. To accommodate malloc overhead make it
 * equal to (2^n - 2). */
static int mm_change_list_max_size = 510;

static void
mm_event_init_change_list(void)
{
	ENTER();

	mm_print("fd change list size: %d", mm_change_list_max_size);
	mm_change_list = mm_alloc(mm_change_list_max_size * sizeof(int));

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
	mm_change_list_max_size *= 2;
	mm_change_list_max_size += 2;

	/* Reallocate the list. */
	mm_print("grow fd change list to size: %d",
		 mm_change_list_max_size);
	mm_change_list = mm_realloc(mm_change_list,
				    mm_change_list_max_size * sizeof(int));

	LEAVE();
}

static void
mm_event_note_fd_change(int fd)
{
	ENTER();
	TRACE("fd: %d", fd);

	if (unlikely(mm_change_list_size == mm_change_list_max_size))
		mm_event_grow_change_list();

	mm_change_list[mm_change_list_size++] = fd;

	LEAVE();
}

/**********************************************************************
 * File descriptor table.
 **********************************************************************/

/* The maximal allowed number of open file descriptors. */
#define MM_FD_MAX (256 * 1024)

/* File descriptor table entry. */
struct mm_fd
{
	/* event handler data */
	uint32_t data;
	/* current read event handler id */
	mm_event_id read_id;
	/* current write event handler id */
	mm_event_id write_id;
	/* requested read event handler id */
	mm_event_id new_read_id;
	/* requested write event handler id */
	mm_event_id new_write_id;
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
	if (fd < 0) {
		/* The fd is invalid. */
		return -1;
	} else if (fd < mm_fd_table_size) {
		/* The fd is okay. */
		return 0;
	} else {
		/* The fd exceeds the table capacity. */
		return -2;
	}
}

void
mm_event_set_fd_data(int fd, uint32_t data)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	mm_fd_table[fd].data = data;

	LEAVE();
}

void
mm_event_register_fd(int fd, mm_event_id read_id, mm_event_id write_id)
{
	ENTER();
	TRACE("fd: %d", fd);

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);
	ASSERT(read_id < mm_cb_table_size);
	ASSERT(write_id < mm_cb_table_size);

	struct mm_fd *mm_fd = &mm_fd_table[fd];

	/* Add the fd to the change list if needed. */
	int a = (mm_fd->read_id != 0);
	int b = (mm_fd->new_read_id != 0);
	int c = (read_id != 0);
	if (likely(((a ^ c) & (b ^ c)))) {
		mm_event_note_fd_change(fd);
	} else {
		a = (mm_fd->write_id != 0);
		b = (mm_fd->new_write_id != 0);
		c = (write_id != 0);
		if (likely(((a ^ c) & (b ^ c)))) {
			mm_event_note_fd_change(fd);
		}
	}

	/* Store new handlers. */
	mm_fd->new_read_id = read_id;
	mm_fd->new_write_id = write_id;

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

	if (likely(mm_fd->read_id != 0 && mm_fd->new_read_id != 0)
	    || likely(mm_fd->write_id != 0 && mm_fd->new_write_id != 0))
		mm_event_note_fd_change(fd);

	mm_fd->new_read_id = 0;
	mm_fd->new_write_id = 0;
	mm_fd->data = 0;

	LEAVE();
}

/**********************************************************************
 * kqueue/kevent support.
 **********************************************************************/

/* The kqueue descriptor. */
static int mm_kq;

/* The kevent list. */
struct kevent *mm_kevents;

/* Current size of the kevent list. */
int mm_nkevents = 0;

/* Maximal size of the kevent list. To accommodate malloc overhead make it
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
	for (int i = 0; i < mm_change_list_size; i++) {
		int fd = mm_change_list[i];
		struct mm_fd *mm_fd = &mm_fd_table[fd];
		int a, b;

		/* Change a read event registration if needed. */
		a = (mm_fd->read_id != 0);
		b = (mm_fd->new_read_id != 0);
		mm_fd->read_id = mm_fd->new_read_id;
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
		a = (mm_fd->write_id != 0);
		b = (mm_fd->new_write_id != 0);
		mm_fd->write_id = mm_fd->new_write_id;
		if (likely(a != b)) {
			int flags;
			if (b) {
				TRACE("register fd %d for write events", fd);
				flags = EV_ADD | EV_CLEAR;
			} else {
				TRACE("unregister fd %d for write events", fd);
				flags = EV_DELETE;
			}

			mm_event_ensure_kevents();
			struct kevent *kp = &mm_kevents[mm_nkevents++];
			EV_SET(kp, fd, EVFILT_WRITE, flags, 0, 0, 0);
		}
	}

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

	/* Process received system events. */
	for (int i = 0; i < mm_nkevents; i++) {
		if ((mm_kevents[i].flags & EV_ERROR) != 0) {

		} else if (mm_kevents[i].filter == EVFILT_READ) {

			int fd = mm_kevents[i].ident;
			int id = mm_fd_table[fd].read_id;
			ASSERT(id < mm_cb_table_size);

			mm_event_cb cb = mm_cb_table[id];
			cb(MM_EVENT_READ, fd, mm_fd_table[fd].data);

		} else if (mm_kevents[i].filter == EVFILT_WRITE) {

			int fd = mm_kevents[i].ident;
			int id = mm_fd_table[fd].write_id;
			ASSERT(id < mm_cb_table_size);

			mm_event_cb cb = mm_cb_table[id];
			cb(MM_EVENT_WRITE, fd, mm_fd_table[fd].data);
		}
	}

	/* If the whole list was used up more space may be needed. */
	mm_event_ensure_kevents();

done:
	LEAVE();
}

/**********************************************************************
 * Event loop control.
 **********************************************************************/

void
mm_event_init(void)
{
	ENTER();

	/* Initialize generic data. */
	mm_event_init_cb_table();
	mm_event_init_change_list();
	mm_event_init_fd_table();

	/* Initialize system specific data. */
	mm_event_init_kqueue();

	LEAVE();
}

void
mm_event_free(void)
{
	ENTER();

	/* Release generic data. */
	mm_event_free_change_list();
	mm_event_free_fd_table();

	/* Release system specific data. */
	mm_event_free_kqueue();

	LEAVE();
}

void
mm_event_loop(void)
{
	ENTER();

	while (!mm_exit_loop) {
		mm_event_dispatch();
	}

	LEAVE();
}

void
mm_event_stop(void)
{
	ENTER();

	mm_exit_loop = 1;

	LEAVE();
}
