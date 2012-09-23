/*
 * event.h - MainMemory event loop.
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

#ifndef EVENT_H
#define	EVENT_H

#include <stdint.h>

typedef enum mm_event {
	MM_EVENT_READ = 1,
	MM_EVENT_WRITE = 2,
} mm_event;

/* Event handler identifier. */
typedef uint8_t mm_event_id;

/* Event handler routine (callback). */
typedef void (*mm_event_cb)(mm_event event, uintptr_t ident, uint32_t data);

void mm_event_init(void);
void mm_event_free(void);

void mm_event_loop(void);
void mm_event_stop(void);

mm_event_id mm_event_register_cb(mm_event_cb cb);

/* Return values of mm_event_verify_fd() */
#define MM_FD_VALID	(0)
#define MM_FD_INVALID	(-1)
#define MM_FD_TOO_BIG	(-2)

int mm_event_verify_fd(int fd);

void mm_event_set_fd_data(int fd, uint32_t data);

void mm_event_register_fd(int fd, mm_event_id read_id, mm_event_id write_id);
void mm_event_unregister_fd(int fd);

#endif	/* EVENT_H */

