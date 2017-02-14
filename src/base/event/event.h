/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
	MM_EVENT_DISABLE,
	MM_EVENT_RECLAIM,
} mm_event_t;

typedef enum {
	MM_EVENT_IGNORED,
	MM_EVENT_REGULAR,
	MM_EVENT_ONESHOT,
} mm_event_occurrence_t;

typedef enum {
	MM_EVENT_LOOSE,
	MM_EVENT_BOUND,
	MM_EVENT_AGILE,
} mm_event_affinity_t;

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

typedef uint16_t mm_event_stamp_t;

/* Event sink. */
struct mm_event_fd
{
	/* The file descriptor to watch. */
	int fd;

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

	/* Pending events for sinks in the dispatch queue. */
	uint8_t queued_events;

	/* Event handers. */
	mm_event_hid_t handler;

	/* Flags used by poller threads. */
	bool oneshot_input_trigger;
	bool oneshot_output_trigger;
	bool changed;

	/* Immutable flags. */
	unsigned loose_target : 1;
	unsigned bound_target : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned regular_output : 1;
	unsigned oneshot_output : 1;

	/* Reclaim queue link. */
	struct mm_slink reclaim_link;
};

static inline mm_thread_t NONNULL(1)
mm_event_target(const struct mm_event_fd *sink)
{
	return sink->target;
}

static inline void
mm_event_update_receive_stamp(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	sink->receive_stamp++;
#endif
}

static inline void
mm_event_update_dispatch_stamp(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	sink->dispatch_stamp++;
#endif
}

static inline void
mm_event_update_complete_stamp(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	// TODO: release memory fence
	mm_memory_store(sink->complete_stamp, sink->dispatch_stamp);
#endif
}

static inline bool NONNULL(1)
mm_event_active(const struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	// TODO: acquire memory fence
	mm_event_stamp_t stamp = mm_memory_load(sink->complete_stamp);
	return sink->receive_stamp != stamp;
#else
	return true;
#endif
}

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_hid_t handler,
		    mm_event_occurrence_t input_mode, mm_event_occurrence_t output_mode,
		    mm_event_affinity_t target);

void NONNULL(1, 2)
mm_event_register_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch);

void NONNULL(1, 2)
mm_event_unregister_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch);

void NONNULL(1, 2)
mm_event_unregister_faulty_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch);

void NONNULL(1, 2)
mm_event_trigger_input(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch);

void NONNULL(1, 2)
mm_event_trigger_output(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch);

void NONNULL(1)
mm_event_convey(struct mm_event_fd *sink, mm_event_t event);

void NONNULL(1)
mm_event_complete(struct mm_event_fd *sink);

#endif /* BASE_EVENT_EVENT_H */
