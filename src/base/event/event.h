/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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
#include "base/task.h"
#include "base/timeq.h"

/* Forward declarations. */
struct mm_context;

/* I/O event IDs. */
typedef enum {
	MM_EVENT_INDEX_INPUT = 0,
	MM_EVENT_INDEX_INPUT_ERROR = 1,
	MM_EVENT_INDEX_OUTPUT = 2,
	MM_EVENT_INDEX_OUTPUT_ERROR = 3,
} mm_event_index_t;

/*
 * NB: Oneshot event sinks have some restrictions.
 *
 * Every oneshot sink maintains a bit of internal state that tells if the
 * relevant event is expected. This bit is modified without acquiring any
 * locks by the thread the sink is bound to.
 *
 * Additionally for oneshot sinks the epoll backend needs to modify (with
 * epoll_ctl) the corresponding file descriptor after each I/O event. So
 * if the event handler does the same thing directly then the backend can
 * get confused (because of certain implementation issues).
 *
 * Therefore event handler routines for oneshot sinks may process events
 * only asynchronously rather than directly. That is the event handler may
 * only remember which sinks and events need to be processed. The actual
 * processing takes place after return from the listen routine.
 */

/* I/O status flags. */
#define MM_EVENT_INPUT_READY	(1u << MM_EVENT_INDEX_INPUT)
#define MM_EVENT_OUTPUT_READY	(1u << MM_EVENT_INDEX_OUTPUT)
#define MM_EVENT_INPUT_ERROR	(1u << MM_EVENT_INDEX_INPUT_ERROR)
#define MM_EVENT_OUTPUT_ERROR	(1u << MM_EVENT_INDEX_OUTPUT_ERROR)

/* I/O event sink close flags. */
#define MM_EVENT_CLOSED		0x00000010
#define MM_EVENT_INPUT_CLOSED	0x00000020
#define MM_EVENT_OUTPUT_CLOSED	0x00000040
#define MM_EVENT_BROKEN		0x00000080

/* Fiber activity flags. */
#define MM_EVENT_INPUT_STARTED	0x00000100
#define MM_EVENT_OUTPUT_STARTED	0x00000200
#define MM_EVENT_INPUT_RESTART	0x00000400
#define MM_EVENT_OUTPUT_RESTART	0x00000800

/* Polling mode for I/O events. */
#define MM_EVENT_REGULAR_INPUT	0x00001000
#define MM_EVENT_REGULAR_OUTPUT	0x00002000
#define MM_EVENT_ONESHOT_INPUT	0x00004000
#define MM_EVENT_ONESHOT_OUTPUT	0x00008000

/* Event sink registered with a local poller. */
#define MM_EVENT_LOCAL_ADDED	0x00010000
/* Event sink polling mode needs to be modified. */
#define MM_EVENT_LOCAL_MODIFY	0x00020000
/* Event sink registered with the common poller. */
#define MM_EVENT_COMMON_ADDED	0x00040000
/* Event sink currently works with the common poller. */
#define MM_EVENT_COMMON_ENABLED	0x00080000

/* Event sink pinned to a fixed local poller. */
#define MM_EVENT_LOCAL_ONLY	0x00100000

/* A sink has a pending I/O event change. */
#define MM_EVENT_CHANGE		0x00200000

/* Per-sink event counter. */
typedef uint16_t mm_event_stamp_t;

/**********************************************************************
 * I/O event sink.
 **********************************************************************/

/* Task entries to perform I/O on a sink. */
struct mm_event_io
{
	struct mm_task input;
	struct mm_task output;
};

/* I/O event sink. */
struct mm_event_fd
{
	/* File descriptor to watch. */
	int fd;
	/* State flags. */
	uint32_t flags;

	/* Task entries to perform I/O. */
	const struct mm_event_io *io;

	/* The context that owns the sink. */
	struct mm_context *context;

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

	/* Fibers bound to perform I/O. */
	struct mm_fiber *input_fiber;
	struct mm_fiber *output_fiber;

	/* Reclaim queue link. */
	struct mm_qlink retire_link;

	/* Sink destruction routine. */
	void (*destroy)(struct mm_event_fd *);
};

/**********************************************************************
 * Timer event sink.
 **********************************************************************/

/* Timer event sink. */
struct mm_event_timer
{
	/* A timer queue node. */
	struct mm_timeq_entry entry;

	/* A fiber to wake up. */
	struct mm_fiber *fiber;

	/* A task entry to fire. */
	const struct mm_task *task;
};

/**********************************************************************
 * Event handling entry point.
 **********************************************************************/

void NONNULL(1)
mm_event_listen(struct mm_context *context, mm_timeout_t timeout);

void NONNULL(1)
mm_event_notify(struct mm_context *context, mm_stamp_t stamp);

/**********************************************************************
 * I/O event sink status.
 **********************************************************************/

static inline bool NONNULL(1)
mm_event_closed(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_CLOSED)) != 0;
}

static inline bool NONNULL(1)
mm_event_input_closed(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_CLOSED | MM_EVENT_INPUT_CLOSED)) != 0;
}

static inline bool NONNULL(1)
mm_event_output_closed(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_CLOSED | MM_EVENT_OUTPUT_CLOSED)) != 0;
}

static inline bool NONNULL(1)
mm_event_input_ready(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_INPUT_READY | MM_EVENT_INPUT_ERROR)) != 0;
}

static inline bool NONNULL(1)
mm_event_output_ready(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_OUTPUT_READY | MM_EVENT_OUTPUT_ERROR)) != 0;
}

static inline bool NONNULL(1)
mm_event_input_in_progress(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_INPUT_RESTART | MM_EVENT_INPUT_READY | MM_EVENT_INPUT_ERROR)) != 0;
}

static inline bool NONNULL(1)
mm_event_output_in_progress(struct mm_event_fd *sink)
{
	return (sink->flags & (MM_EVENT_OUTPUT_RESTART | MM_EVENT_OUTPUT_READY | MM_EVENT_OUTPUT_ERROR)) != 0;
}

static inline void NONNULL(1)
mm_event_set_closed(struct mm_event_fd *sink)
{
	sink->flags |= MM_EVENT_CLOSED;
}

static inline void NONNULL(1)
mm_event_set_broken(struct mm_event_fd *sink)
{
	sink->flags |= MM_EVENT_CLOSED | MM_EVENT_BROKEN;
}

static inline void NONNULL(1)
mm_event_set_input_closed(struct mm_event_fd *sink)
{
	sink->flags |= MM_EVENT_INPUT_CLOSED;
}

static inline void NONNULL(1)
mm_event_set_output_closed(struct mm_event_fd *sink)
{
	sink->flags |= MM_EVENT_OUTPUT_CLOSED;
}

static inline void NONNULL(1)
mm_event_reset_input_ready(struct mm_event_fd *sink)
{
	sink->flags &= ~MM_EVENT_INPUT_READY;
}

static inline void NONNULL(1)
mm_event_reset_output_ready(struct mm_event_fd *sink)
{
	sink->flags &= ~MM_EVENT_OUTPUT_READY;
}

/**********************************************************************
 * I/O event sink control.
 **********************************************************************/

/* Prepare I/O tasks for an I/O event sink. */
void NONNULL(1)
mm_event_prepare_io(struct mm_event_io *tasks, mm_task_execute_t input, mm_task_execute_t output);

/* Get a stub for I/O tasks that is used in some special cases. */
struct mm_event_io *
mm_event_instant_io(void);

/* Prepare an I/O event sink. */
void NONNULL(1, 3)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, const struct mm_event_io *io, uint32_t flags);

void NONNULL(1, 2)
mm_event_register_fd(struct mm_context *context, struct mm_event_fd *sink);

void NONNULL(1)
mm_event_close_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_close_broken_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_trigger_input(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_trigger_output(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_submit_input(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_submit_output(struct mm_event_fd *sink);

/**********************************************************************
 * Timer event sink control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_prepare_task_timer(struct mm_event_timer *sink, const struct mm_task *task);

void NONNULL(1, 2)
mm_event_prepare_fiber_timer(struct mm_event_timer *sink, struct mm_fiber *fiber);

static inline bool NONNULL(1)
mm_event_timer_armed(struct mm_event_timer *sink)
{
	return mm_timeq_entry_queued(&sink->entry);
}

void NONNULL(1, 2)
mm_event_arm_timer(struct mm_context *context, struct mm_event_timer *sink, mm_timeout_t timeout);

void NONNULL(1, 2)
mm_event_disarm_timer(struct mm_context *context, struct mm_event_timer *sink);

/**********************************************************************
 * Internal event processing for I/O event sinks: start asynchronous
   handling of an event as it is delivered to the target thread.
 **********************************************************************/

void NONNULL(1)
mm_event_handle_input(struct mm_event_fd *sink, uint32_t flags);

void NONNULL(1)
mm_event_handle_output(struct mm_event_fd *sink, uint32_t flags);

#endif /* BASE_EVENT_EVENT_H */
