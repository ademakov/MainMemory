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

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_REGISTER,
	MM_EVENT_UNREGISTER,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
} mm_event_t;

/* Return values of mm_event_verify_fd() */
typedef enum {
	MM_EVENT_FD_VALID = 0,
	MM_EVENT_FD_INVALID = -1,
	MM_EVENT_FD_TOO_BIG = -2,
} mm_event_verify_t;

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, uint32_t data);

/**********************************************************************
 * Common event routines.
 **********************************************************************/

void mm_event_init(void);
void mm_event_term(void);

void mm_event_notify(void);

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * I/O Events Support.
 **********************************************************************/

mm_event_verify_t mm_event_verify_fd(int fd);

void mm_event_register_fd(int fd,
			  uint32_t data,
			  mm_event_hid_t input_handler,
			  mm_event_hid_t output_handler,
			  mm_event_hid_t control_handler);

void mm_event_unregister_fd(int fd);

#endif /* EVENT_H */

