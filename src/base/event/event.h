/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#ifndef BASE_EVENT_EVENT_H
#define BASE_EVENT_EVENT_H

#include "common.h"
#include "base/list.h"

#if defined(HAVE_SYS_EPOLL_H)
# undef MM_ONESHOT_HANDLERS
#else
# define MM_ONESHOT_HANDLERS	1
#endif

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_ATTACH,
	MM_EVENT_DETACH,
	MM_EVENT_REGISTER,
	MM_EVENT_UNREGISTER,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
} mm_event_t;

typedef enum {
	MM_EVENT_IGNORED,
	MM_EVENT_REGULAR,
	MM_EVENT_ONESHOT,
} mm_event_mode_t;

typedef enum {
	MM_EVENT_TARGET_LOOSE,
	MM_EVENT_TARGET_BOUND,
	MM_EVENT_TARGET_AGILE,
} mm_event_target_t;

/* Event details. */
struct mm_event
{
	mm_event_t event;
	struct mm_event_fd *ev_fd;
};

/**********************************************************************
 * Event subsystem initialization.
 **********************************************************************/

void mm_event_init(void);

/**********************************************************************
 * Event subsystem statistics.
 **********************************************************************/

void mm_event_stats(void);

/**********************************************************************
 * Event handlers.
 **********************************************************************/

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, void *data);

/* Event handler descriptor. */
struct mm_event_hdesc
{
	mm_event_handler_t handler;
};

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

/* Event sink. */
struct mm_event_fd
{
	/* The file descriptor to watch. */
	int fd;

	/* The thread the owns the sink. */
	mm_thread_t target;

	/* Event handers. */
	mm_event_hid_t handler;

	/* Immutable flags. */
	unsigned loose_target : 1;
	unsigned bound_target : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned regular_output : 1;
	unsigned oneshot_output : 1;

	/* The stamp set the poller thread. */
	uint32_t arrival_stamp;

	/* Flags used by the poller thread. */
	unsigned changed : 1;
	unsigned oneshot_input_trigger : 1;
	unsigned oneshot_output_trigger : 1;

	/* The stamp set by the owner thread to tell if it is eligible
	   to transfer the sink ownership to another thread. */
	uint32_t dispatch_stamp;

	/* Flags used by the owner thread. */
	uint8_t attached;
	uint8_t pending_detach;

	/* Pending detach list link. */
	struct mm_link detach_link;
};

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_hid_t handler,
		    mm_event_mode_t input_mode, mm_event_mode_t output_mode,
		    mm_event_target_t target);

void NONNULL(1)
mm_event_handle(struct mm_event_fd *sink, mm_event_t event);

void NONNULL(1)
mm_event_detach(struct mm_event_fd *sink);

static inline mm_thread_t NONNULL(1)
mm_event_target(const struct mm_event_fd *sink)
{
	return sink->target;
}

static inline bool NONNULL(1)
mm_event_attached(const struct mm_event_fd *sink)
{
	return sink->attached;
}

#endif /* BASE_EVENT_EVENT_H */
