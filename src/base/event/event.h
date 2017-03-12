/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_event_listener;

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT,
	MM_EVENT_OUTPUT_ERROR,
	MM_EVENT_RETIRE,
	MM_EVENT_RECLAIM,
} mm_event_t;

/* I/O event repeat modes. */
typedef enum {
	MM_EVENT_IGNORED,
	MM_EVENT_REGULAR,
	MM_EVENT_ONESHOT,
} mm_event_sequence_t;

/* Event sink thread affinity. */
typedef enum {
	MM_EVENT_LOOSE,
	MM_EVENT_BOUND,
	MM_EVENT_AGILE,
} mm_event_affinity_t;

/* Event sink status. */
typedef enum {
	MM_EVENT_INVALID = -2,
	MM_EVENT_DROPPED = -1,
	MM_EVENT_INITIAL = 0,
	MM_EVENT_ENABLED = 1,
	MM_EVENT_CHANGED = 2,
} mm_event_status_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, void *data);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

typedef uint16_t mm_event_stamp_t;

/* Event sink. */
struct mm_event_fd
{
	/* Event handler routine. */
	mm_event_handler_t handler;

	/* File descriptor to watch. */
	int fd;

	/* Current event sink status. */
	mm_event_status_t status;

	/* The thread the owns the sink. */
	mm_thread_t target;

#if ENABLE_SMP
	/* The stamp updated by poller threads on every next event received
	   from the system. */
	mm_event_stamp_t receive_stamp;
	/* The stamp updated by the target thread on every next event
	   delivered to it from poller threads. */
	mm_event_stamp_t dispatch_stamp;
	/* The stamp updated by the target thread when completing all
	   the events delivered so far. When it is equal to the current
	   receive_stamp value it will not change until another event
	   is received and dispatched. So for a poller thread that sees
	   such a condition it is safe to switch the sink's target thread. */
	mm_event_stamp_t complete_stamp;
#endif

	/* Flags used by poller threads. */
	bool oneshot_input_trigger;
	bool oneshot_output_trigger;

	/* Immutable flags. */
	unsigned loose_target : 1;
	unsigned bound_target : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned regular_output : 1;
	unsigned oneshot_output : 1;

	/* Pending events for sinks in the dispatch queue. */
	uint8_t queued_events;

	/* Reclaim queue link. */
	union {
		struct mm_qlink retire_link;
		struct mm_slink reclaim_link;
	};
};

static inline mm_thread_t NONNULL(1)
mm_event_target(const struct mm_event_fd *sink)
{
	return sink->target;
}

/* Mark a sink as having completed the processing of all the events
   delivered so far. */
static inline void NONNULL(1)
mm_event_handle_complete(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	/* TODO: release memory fence */
	mm_memory_store(sink->complete_stamp, sink->dispatch_stamp);
#endif
}

/**********************************************************************
 * I/O events control.
 **********************************************************************/

void NONNULL(1, 3)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_handler_t handler,
		    mm_event_sequence_t input, mm_event_sequence_t output,
		    mm_event_affinity_t target);

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_unregister_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_unregister_faulty_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_trigger_input(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_trigger_output(struct mm_event_fd *sink);

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

void NONNULL(1)
mm_event_listen(struct mm_event_listener *listener, mm_timeout_t timeout);

void NONNULL(1)
mm_event_notify(struct mm_event_listener *listener, mm_stamp_t stamp);

void NONNULL(1)
mm_event_notify_any(struct mm_event_dispatch *dispatch);

#endif /* BASE_EVENT_EVENT_H */
