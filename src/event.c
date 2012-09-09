/*
 * event.c - MainMemory events.
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

/* The maximum allowed number of open file descriptors. */
#define MM_FD_MAX (256 * 1024)

/* Check if a kevent filter is either read or write filter. */
#define MM_IS_IOF(f) ((f) == EVFILT_READ || (f) == EVFILT_WRITE)

/* File descriptor table entry. */
struct mm_fd
{
	/* callback */
	mm_event_iocb iocb;
	/* callback data */
	intptr_t data;
	/* current flags */
	uint8_t cflags;
	/* requested flags */
	uint8_t rflags;
};

/* File descriptor table. */
static struct mm_fd *mm_fd_table;
static int mm_fd_table_size = _POSIX_OPEN_MAX;

/* Event loop exit flag. */
static volatile int mm_exit_loop = 0;

struct mm_kevents
{
	int nevents;
	int max_nevents;
	struct kevent *kevents;
};

static int mm_kq;
static struct mm_kevents mm_ev;
static struct mm_kevents mm_ch;

static void
mm_event_init_kevents(struct mm_kevents *kevents)
{
	kevents->nevents = 0;
	kevents->max_nevents = 255;
	kevents->kevents = mm_calloc(255, sizeof(struct kevent));
}

static void
mm_event_grow_kevents(struct mm_kevents *kevents)
{
	int max_nevents = kevents->max_nevents;
	kevents->max_nevents = max_nevents * 2 + 1;
	kevents->kevents = mm_crealloc(kevents->kevents,
				       max_nevents, kevents->max_nevents,
				       sizeof(struct kevent));
}

static void
mm_event_delete_kevent(struct mm_kevents *kevents, int n)
{
	ASSERT(n < kevents->nevents);

	--kevents->nevents;
	if (n < kevents->nevents) {
		memmove(kevents->kevents + n * sizeof(struct kevent),
			kevents->kevents + (n + 1) * sizeof(struct kevent),
			(kevents->nevents - n) * sizeof(struct kevent));
	}
}

static void
mm_event_note_fd(int fd, int flags)
{
	struct mm_fd *mm_fd = &mm_fd_table[fd];
	if (unlikely(flags == mm_fd->rflags)) {
		/* A duplicate request. */
		return;
	}

	if (unlikely(mm_fd->rflags != mm_fd->cflags)) {
		/* A different request for the same file descriptor has not yet
		 * been put into effect as we already changed our mind. This
		 * should not happen with a sound code. */
		mm_print("hmm, fd abuse detected.");

		int i = 0;
		while (i < mm_ch.nevents) {
			if (unlikely(mm_ch.kevents[i].ident == fd)
			    && likely(MM_IS_IOF(mm_ch.kevents[i].filter))) {
				mm_event_delete_kevent(&mm_ch, i);
			} else {
				++i;
			}
		}
	}

	/* Find the current and wanted flags difference. */
	int changes = flags ^ mm_fd->cflags;
	/* Count the number of changes. */
	int nchanges = (!!(changes & MM_EVENT_READ) + !!(changes & MM_EVENT_WRITE));

	/* Ensure the change array fits the requested change records. */
	if (unlikely((mm_ch.nevents + nchanges) > mm_ch.max_nevents)) {
		mm_event_grow_kevents(&mm_ch);
	}

	/* It is expected that most of the fds are going to be used
	 * both for reading and writing. */
	if (likely(changes & MM_EVENT_READ) != 0) {
		int flags;
		if (mm_fd->rflags & MM_EVENT_READ) {
			TRACE("register fd %d for read events", fd);
			flags = EV_ADD | EV_CLEAR;
		} else {
			TRACE("unregister fd %d for read events", fd);
			flags = EV_DELETE;
		}

		struct kevent *kp = &mm_ch.kevents[mm_ch.nevents++];
		EV_SET(kp, fd, EVFILT_READ, flags, 0, 0, 0);
	}
	if (likely(changes & MM_EVENT_WRITE) != 0) {
		int flags;
		if (mm_fd->rflags & MM_EVENT_WRITE) {
			TRACE("register fd %d for write events", fd);
			flags = EV_ADD | EV_CLEAR;
		} else {
			TRACE("unregister fd %d for write events", fd);
			flags = EV_DELETE;
		}

		struct kevent *kp = &mm_ch.kevents[mm_ch.nevents++];
		EV_SET(kp, fd, EVFILT_WRITE, flags, 0, 0, 0);
	}

	mm_fd->rflags = flags;
}

static void
mm_event_dispatch(void)
{
	ENTER();

	int n = kevent(mm_kq,
		       mm_ch.kevents, mm_ch.nevents,
		       mm_ev.kevents, mm_ev.max_nevents,
		       NULL);
	if (n < 0) {
		mm_error(errno, "kevent");
		goto done;
	}

	for (int i = 0; i < n; i++) {
		if ((mm_ev.kevents[i].flags & EV_ERROR) != 0) {
		}
		else if (mm_ev.kevents[i].filter == EVFILT_READ) {
			int fd = mm_ev.kevents[i].ident;
			mm_event_iocb iocb = mm_fd_table[fd].iocb;
			if (iocb != NULL) {
				iocb(fd, MM_EVENT_READ, mm_fd_table[fd].data);
			}
		}
		else if (mm_ev.kevents[i].filter == EVFILT_WRITE) {
			int fd = mm_ev.kevents[i].ident;
			mm_event_iocb iocb = mm_fd_table[fd].iocb;
			if (iocb != NULL) {
				iocb(fd, MM_EVENT_WRITE, mm_fd_table[fd].data);
			}
		}
	}

	if (n == mm_ev.max_nevents) {
		/* Looks like more space may be needed. */
		mm_event_grow_kevents(&mm_ev);
	}
done:
	LEAVE();
}

void
mm_event_init(void)
{
	ENTER();

	int max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd > mm_fd_table_size) {
		mm_fd_table_size = max_fd;
	}
	if (max_fd > MM_FD_MAX) {
		mm_print("truncating too high file descriptor limit: %d\n", mm_fd_table_size);
		max_fd = MM_FD_MAX;
	}
	mm_print("file descriptor table size: %d\n", mm_fd_table_size);

	mm_fd_table = mm_calloc(mm_fd_table_size, sizeof(struct mm_fd));

	mm_kq = kqueue();
	if (mm_kq == -1) {
		mm_fatal(errno, "Failed to create kqueue");
	}

	mm_event_init_kevents(&mm_ev);
	mm_event_init_kevents(&mm_ch);

	LEAVE();
}

void
mm_event_free(void)
{
	ENTER();

	mm_free(mm_fd_table);
	mm_fd_table = NULL;

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


int
mm_event_verify_fd(int fd)
{
	ASSERT(fd >= 0);
	if (fd >= mm_fd_table_size) {
		return -1;
	}
	return 0;
}

void
mm_event_register_fd(int fd, int flags, mm_event_iocb iocb, intptr_t data)
{
	ENTER();

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	mm_fd_table[fd].iocb = iocb;
	mm_fd_table[fd].data = data;
	mm_event_note_fd(fd, flags);

	LEAVE();
}

void
mm_event_unregister_fd(int fd)
{
	ENTER();

	ASSERT(fd >= 0);
	ASSERT(fd < mm_fd_table_size);

	mm_fd_table[fd].iocb = NULL;
	mm_fd_table[fd].data = 0;
	mm_event_note_fd(fd, 0);

	LEAVE();
}
