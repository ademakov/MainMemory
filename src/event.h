/*
 * event.h - MainMemory event loop.
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

#ifndef EVENT_H
#define EVENT_H

#include "common.h"

/* Forward declaration. */
struct mm_port;

/* Event handler identifier. */
typedef uint8_t mm_event_handler_t;

/**********************************************************************
 * Common event routines.
 **********************************************************************/

void mm_event_init(void);
void mm_event_term(void);

void mm_event_start(void);
void mm_event_stop(void);

/**********************************************************************
 * Net I/O Event Support.
 **********************************************************************/

/* Net I/O events. */
#define MM_EVENT_NET_READ	1
#define MM_EVENT_NET_WRITE	2
#define MM_EVENT_NET_READ_WRITE	(MM_EVENT_NET_READ | MM_EVENT_NET_WRITE)

/* Return values of mm_event_verify_fd() */
#define MM_FD_VALID	(0)
#define MM_FD_INVALID	(-1)
#define MM_FD_TOO_BIG	(-2)

mm_event_handler_t mm_event_add_io_handler(int flags, struct mm_port *port)
	__attribute__((nonnull(2)));

int mm_event_verify_fd(int fd);

void mm_event_register_fd(int fd, mm_event_handler_t handler, uint32_t data);

void mm_event_unregister_fd(int fd);

/**********************************************************************
 * Other Event Support.
 **********************************************************************/

/* Event handler identifier. */
typedef uint8_t mm_handler_id;

/* Event handler routine (callback). */
typedef void (*mm_handler)(uintptr_t ident, uint32_t data);

mm_handler_id mm_event_install_handler(mm_handler cb);

#endif /* EVENT_H */

