/*
 * event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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
#include "trace.h"

#if defined(HAVE_SYS_EPOLL_H)
# undef MM_ONESHOT_HANDLERS
#else
# define MM_ONESHOT_HANDLERS 1
#endif

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_REGISTER,
	MM_EVENT_UNREGISTER,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
} mm_event_t;

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, uint32_t data);

/**********************************************************************
 * Common event routines.
 **********************************************************************/

void mm_event_init(void);
void mm_event_term(void);

bool mm_event_collect(void);
bool mm_event_poll(mm_timeout_t timeout);
void mm_event_dispatch(void);

void mm_event_notify(void);
bool mm_event_dampen(void);

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * I/O Events Support.
 **********************************************************************/

/* Return values of mm_event_verify_fd() */
typedef enum {
	MM_EVENT_FD_VALID = 0,
	MM_EVENT_FD_INVALID = -1,
	MM_EVENT_FD_TOO_BIG = -2,
} mm_event_verify_t;

/* Event message flags. */
#define MM_EVENT_MSG_ONESHOT_INPUT	1
#define MM_EVENT_MSG_ONESHOT_OUTPUT	2

mm_event_verify_t mm_event_verify_fd(int fd);

void mm_event_send(int fd, uint32_t code, uint32_t data);

/* Check to see if there is at least one regular handler provided. */
static inline bool
mm_event_verify_handlers(mm_event_hid_t input_handler, bool input_oneshot,
			 mm_event_hid_t output_handler, bool output_oneshot,
			 mm_event_hid_t control_handler)
{
	if (input_handler && !input_oneshot)
		return true;
	if (output_handler && !output_oneshot)
		return true;
	if (control_handler)
		return true;
	return false;
}

static inline void
mm_event_register_fd(int fd, uint32_t data,
		     mm_event_hid_t input_handler, bool input_oneshot,
		     mm_event_hid_t output_handler, bool output_oneshot,
		     mm_event_hid_t control_handler)
{
	ENTER();

	ASSERT(mm_event_verify_handlers(input_handler, input_oneshot,
				        output_handler, output_oneshot,
					control_handler));

	uint32_t code = ((input_handler << 24)
			 | (output_handler << 16)
			 | (control_handler << 8));
#if MM_ONESHOT_HANDLERS
	if (input_handler && input_oneshot)
		code |= MM_EVENT_MSG_ONESHOT_INPUT;
	if (output_handler && output_oneshot)
		code |= MM_EVENT_MSG_ONESHOT_OUTPUT;
#else
	(void) input_oneshot;
	(void) output_oneshot;
#endif
	mm_event_send(fd, code, data);

	LEAVE();
}

static inline void
mm_event_unregister_fd(int fd)
{
	ENTER();

	mm_event_send(fd, 0, 0);

	LEAVE();
}

#if MM_ONESHOT_HANDLERS
static inline void
mm_event_trigger_input(int fd, mm_event_hid_t input_handler)
{
	ENTER();

	uint32_t code = (input_handler << 24) | MM_EVENT_MSG_ONESHOT_INPUT;
	mm_event_send(fd, code, 0);

	LEAVE();
}
#endif

#if MM_ONESHOT_HANDLERS
static inline void
mm_event_trigger_output(int fd, mm_event_hid_t output_handler)
{
	ENTER();

	uint32_t code = (output_handler << 16) | MM_EVENT_MSG_ONESHOT_OUTPUT;
	mm_event_send(fd, code, 0);

	LEAVE();
}
#endif

#endif /* EVENT_H */
