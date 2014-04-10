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

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, void *data);

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* File descriptor event entry. */
struct mm_event_fd
{
	/* The file descriptor to watch. */
	int fd;

	/* Event handers. */
	mm_event_hid_t input_handler;
	mm_event_hid_t output_handler;
	mm_event_hid_t control_handler;

	/* Event flags */
	unsigned changed : 1;
	unsigned oneshot_input : 1;
	unsigned oneshot_input_trigger : 1;
	unsigned oneshot_output : 1;
	unsigned oneshot_output_trigger : 1;
};

/* Event poll data container. */
struct mm_event_table;

/**********************************************************************
 * Event subsystem initialization and termination.
 **********************************************************************/

void mm_event_init(void);
void mm_event_term(void);

void mm_event_stats(void);

/**********************************************************************
 * Event handler registration.
 **********************************************************************/

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * Event poll routines.
 **********************************************************************/

struct mm_event_table * mm_event_create_table(void);

void mm_event_destroy_table(struct mm_event_table *events)
	__attribute__((nonnull(1)));

bool mm_event_collect(struct mm_event_table *events)
	__attribute__((nonnull(1)));

bool mm_event_poll(struct mm_event_table *events, mm_timeout_t timeout)
	__attribute__((nonnull(1)));

void mm_event_dispatch(struct mm_event_table *events)
	__attribute__((nonnull(1)));

void mm_event_notify(struct mm_event_table *events)
	__attribute__((nonnull(1)));

void mm_event_dampen(struct mm_event_table *events)
	__attribute__((nonnull(1)));

/**********************************************************************
 * I/O events support.
 **********************************************************************/

bool mm_event_prepare_fd(struct mm_event_fd *ev_fd,
			 mm_event_hid_t input_handler, bool input_oneshot,
			 mm_event_hid_t output_handler, bool output_oneshot,
			 mm_event_hid_t control_handler)
	__attribute__((nonnull(1)));

void mm_event_register_fd(struct mm_event_table *events, int fd,
			  struct mm_event_fd *ev_fd)
	__attribute__((nonnull(1, 3)));

void mm_event_unregister_fd(struct mm_event_table *events, int fd,
			    struct mm_event_fd *ev_fd)
	__attribute__((nonnull(1, 3)));

void mm_event_trigger_input(struct mm_event_table *events, int fd,
			    struct mm_event_fd *ev_fd)
	__attribute__((nonnull(1, 3)));

void mm_event_trigger_output(struct mm_event_table *events, int fd,
			     struct mm_event_fd *ev_fd)
	__attribute__((nonnull(1, 3)));

#endif /* EVENT_H */
