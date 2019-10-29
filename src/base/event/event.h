/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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
struct mm_event_dispatch;
struct mm_event_listener;

/* I/O event IDs. */
typedef enum {
	MM_EVENT_INDEX_INPUT = 0,
	MM_EVENT_INDEX_INPUT_ERROR = 1,
	MM_EVENT_INDEX_OUTPUT = 2,
	MM_EVENT_INDEX_OUTPUT_ERROR = 3,
} mm_event_index_t;

/* I/O event repeat modes. */
typedef enum {
	/* No event are expected. */
	MM_EVENT_IGNORED,
	/* Repeated events are expected. */
	MM_EVENT_REGULAR,
	/* Single event is expected. To get another one it required
	   to call a corresponding trigger function. */
	MM_EVENT_ONESHOT,
} mm_event_mode_t;

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
#define MM_EVENT_CLOSED		0x000010
#define MM_EVENT_INPUT_CLOSED	0x000020
#define MM_EVENT_OUTPUT_CLOSED	0x000040
#define MM_EVENT_BROKEN		0x000080

/* Fiber activity flags. */
#define MM_EVENT_INPUT_STARTED	0x000100
#define MM_EVENT_OUTPUT_STARTED	0x000200
#define MM_EVENT_INPUT_RESTART	0x000400
#define MM_EVENT_OUTPUT_RESTART	0x000800

/* Polling mode for I/O events. */
#define MM_EVENT_REGULAR_INPUT	0x001000
#define MM_EVENT_REGULAR_OUTPUT	0x002000
#define MM_EVENT_ONESHOT_INPUT	0x004000
#define MM_EVENT_ONESHOT_OUTPUT	0x008000
#define MM_EVENT_INPUT_TRIGGER	0x010000
#define MM_EVENT_OUTPUT_TRIGGER	0x020000

/* Internal notification fd (selfpipe or eventfd). */
#define MM_EVENT_NOTIFY_FD	0x040000
/* Event dispatch is bound to a single listener. */
#define MM_EVENT_FIXED_LISTENER	0x080000

/* A sink has a pending I/O event change. */
#define MM_EVENT_CHANGE		0x100000

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
	/* Task entries to perform I/O. */
	struct mm_event_io *io;

	/* The listener that owns the sink. */
	struct mm_event_listener *listener;

	/* File descriptor to watch. */
	int fd;
	/* State flags. */
	uint32_t flags;

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
	struct mm_task *task;
};

/**********************************************************************
 * Event handling entry point.
 **********************************************************************/

void NONNULL(1)
mm_event_listen(struct mm_context *context, mm_timeout_t timeout);

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
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, struct mm_event_io *io,
		    mm_event_mode_t input, mm_event_mode_t output, uint32_t flags);

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink);

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
 * Event time.
 **********************************************************************/

mm_timeval_t NONNULL(1)
mm_event_gettime(struct mm_context *context);

mm_timeval_t NONNULL(1)
mm_event_getrealtime(struct mm_context *context);

/**********************************************************************
 * Timer event sink control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_prepare_task_timer(struct mm_event_timer *sink, struct mm_task *task);

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

/**********************************************************************
 * Asynchronous procedure call basic declarations.
 **********************************************************************/

/* The maximum number of arguments for post requests.
   It must be equal to (MM_RING_MPMC_DATA_SIZE - 1). */
#define MM_EVENT_ASYNC_MAX	(6)

/* Request routines. */
typedef void (*mm_event_async_routine_t)(struct mm_event_listener *listener, uintptr_t arguments[MM_EVENT_ASYNC_MAX]);

/**********************************************************************
 * Asynchronous procedure call execution.
 **********************************************************************/

void NONNULL(1)
mm_event_handle_calls(struct mm_event_listener *listener);

/**********************************************************************
 * Asynchronous procedure calls targeting a single listener.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_call_0(struct mm_event_listener *listener, mm_event_async_routine_t r);

bool NONNULL(1, 2)
mm_event_trycall_0(struct mm_event_listener *listener, mm_event_async_routine_t r);

void NONNULL(1, 2)
mm_event_call_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1);

bool NONNULL(1, 2)
mm_event_trycall_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1);

void NONNULL(1, 2)
mm_event_call_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2);

bool NONNULL(1, 2)
mm_event_trycall_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2);

void NONNULL(1, 2)
mm_event_call_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3);

bool NONNULL(1, 2)
mm_event_trycall_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3);

void NONNULL(1, 2)
mm_event_call_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

bool NONNULL(1, 2)
mm_event_trycall_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

void NONNULL(1, 2)
mm_event_call_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

bool NONNULL(1, 2)
mm_event_trycall_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

void NONNULL(1, 2)
mm_event_call_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

bool NONNULL(1, 2)
mm_event_trycall_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

/**********************************************************************
 * Asynchronous procedure calls targeting any listener of a dispatcher.
 **********************************************************************/

void NONNULL(1)
mm_event_post_0(mm_event_async_routine_t r);

void NONNULL(1)
mm_event_post_1(mm_event_async_routine_t r, uintptr_t a1);

void NONNULL(1)
mm_event_post_2(mm_event_async_routine_t r, uintptr_t a1, uintptr_t a2);

void NONNULL(1)
mm_event_post_3(mm_event_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3);

void NONNULL(1)
mm_event_post_4(mm_event_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

void NONNULL(1)
mm_event_post_5(mm_event_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

void NONNULL(1)
mm_event_post_6(mm_event_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

/**********************************************************************
 * Asynchronous task scheduling.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_add_task(struct mm_event_listener *listener, mm_task_t task, mm_value_t arg);

void NONNULL(1, 2)
mm_event_send_task(struct mm_event_listener *listener, mm_task_t task, mm_value_t arg);

void NONNULL(1)
mm_event_post_task(mm_task_t task, mm_value_t arg);

#endif /* BASE_EVENT_EVENT_H */
