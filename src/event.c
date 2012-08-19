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

#include <sys/event.h>
#include <sys/types.h>

static volatile int mm_stop_loop = 0;

static int mm_kqueue;

void
mm_event_init(void)
{
	ENTER();

	mm_kqueue = kqueue();
	if (mm_kqueue == -1) {
		mm_fatal("Failed to create kqueue");
	}

	LEAVE();
}

void
mm_event_free(void)
{
	ENTER();
	LEAVE();
}

void
mm_event_loop(void)
{
	ENTER();

	while (!mm_stop_loop) {
		ABORT();
	}

	LEAVE();
}

void
mm_event_stop(void)
{
	ENTER();
	mm_stop_loop = 1;
	LEAVE();
}

void
mm_event_set_fd(int fd, int flags, mm_event_iocb iocb, intptr_t data)
{
}
