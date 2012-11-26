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
#define EVENT_H

#include "common.h"

/* Forward declaration. */
struct mm_port;

/**********************************************************************
 * Common event routines.
 **********************************************************************/

void mm_event_init(void);
void mm_event_free(void);

void mm_event_loop(void);
void mm_event_stop(void);

/**********************************************************************
 * I/O Event Support.
 **********************************************************************/

/* Return values of mm_event_verify_fd() */
#define MM_FD_VALID	(0)
#define MM_FD_INVALID	(-1)
#define MM_FD_TOO_BIG	(-2)

/* I/O handler identifier. */
typedef uint8_t mm_io_handler;

mm_io_handler mm_event_add_io_handler(struct mm_port *read_ready_port,
				      struct mm_port *write_ready_port);

int mm_event_verify_fd(int fd);

void mm_event_register_fd(int fd, mm_io_handler handler, uint32_t data);

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

