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
#include "base/fiber/work.h"

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_event_listener;

/* Event types. */
typedef enum {
	MM_EVENT_INPUT = 0,
	MM_EVENT_IN_ERR = 1,
	MM_EVENT_OUTPUT = 2,
	MM_EVENT_OUT_ERR = 3,
} mm_event_t;

/* I/O event repeat modes. */
typedef enum {
	/* No event are expected. */
	MM_EVENT_IGNORED,
	/* Repeated events are expected. */
	MM_EVENT_REGULAR,
	/* Single event is expected. To get another one it required
	   to call a corresponding trigger function. */
	MM_EVENT_ONESHOT,
} mm_event_capacity_t;

/*
 * NB: Oneshot event sinks have some restrictions.
 *
 * Every oneshot sink maintains a bit of internal state that tells if the
 * relevant event is expected. This bit is modified without acquiring any
 * locks by the thread the sink is bound to.
 *
 * Therefore it is forbidden to create oneshot sinks with stray affinity.
 * If it were allowed there would be race conditions modifying the oneshot
 * state.
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
#define MM_EVENT_INPUT_READY	(1u << MM_EVENT_INPUT)
#define MM_EVENT_OUTPUT_READY	(1u << MM_EVENT_OUTPUT)
#define MM_EVENT_INPUT_ERROR	(1u << MM_EVENT_IN_ERR)
#define MM_EVENT_OUTPUT_ERROR	(1u << MM_EVENT_OUT_ERR)

/* Fiber activity flags. */
#define MM_EVENT_INPUT_STARTED	0x000010
#define MM_EVENT_OUTPUT_STARTED	0x000020
#define MM_EVENT_INPUT_PENDING	0x000040
#define MM_EVENT_OUTPUT_PENDING	0x000080

/* I/O event sink close flags. */
#define MM_EVENT_CLOSED		0x000100
#define MM_EVENT_INPUT_CLOSED	0x000200
#define MM_EVENT_OUTPUT_CLOSED	0x000400
#define MM_EVENT_BROKEN		0x000800

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

struct mm_event_fd
{
	/* Listener (along with associated thread) that owns the sink. */
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

	/* Pending events for sinks in the dispatch queue. */
	uint8_t queued_events;

	/* Fibers bound to perform I/O. */
	struct mm_fiber *input_fiber;
	struct mm_fiber *output_fiber;
	/* Work entries to perform I/O. */
	struct mm_work input_work;
	struct mm_work output_work;
	/* Work entry for sink memory reclamation. */
	struct mm_work reclaim_work;

	/* Reclaim queue link. */
	union {
		struct mm_qlink retire_link;
		struct mm_slink reclaim_link;
	};

	/* Sink destruction routine. */
	void (*destroy)(struct mm_event_fd *);
};

/**********************************************************************
 * Event sink status.
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

/**********************************************************************
 * Event sink activity.
 **********************************************************************/

/* Start asynchronous processing of an event as it is delivered to
   the target thread. */
void NONNULL(1)
mm_event_handle_input(struct mm_event_fd *sink, uint32_t flags);
void NONNULL(1)
mm_event_handle_output(struct mm_event_fd *sink, uint32_t flags);

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd,
		    mm_work_routine_t input_routine,
		    mm_work_routine_t output_routine,
		    mm_event_capacity_t input, mm_event_capacity_t output,
		    bool fixed_listener);

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
mm_event_start_input_work(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_start_output_work(struct mm_event_fd *sink);

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

void NONNULL(1)
mm_event_listen(struct mm_event_listener *listener, mm_timeout_t timeout);

void NONNULL(1)
mm_event_notify(struct mm_event_listener *listener, mm_stamp_t stamp);

void NONNULL(1)
mm_event_wakeup(struct mm_event_listener *listener);

void NONNULL(1)
mm_event_wakeup_any(struct mm_event_dispatch *dispatch);

/**********************************************************************
 * Asynchronous procedure call basic declarations.
 **********************************************************************/

/* The maximum number of arguments for post requests.
   It must be equal to (MM_RING_MPMC_DATA_SIZE - 1). */
#define MM_EVENT_ASYNC_MAX	(6)

/* Request routines. */
typedef void (*mm_event_async_routine_t)(uintptr_t arguments[MM_EVENT_ASYNC_MAX]);

/**********************************************************************
 * Asynchronous procedure call execution.
 **********************************************************************/

void NONNULL(1)
mm_event_handle_calls(struct mm_event_listener *listener);

bool NONNULL(1)
mm_event_handle_posts(struct mm_event_listener *listener);

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

void NONNULL(1, 2)
mm_event_post_0(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r);

bool NONNULL(1, 2)
mm_event_trypost_0(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r);

void NONNULL(1, 2)
mm_event_post_1(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1);

bool NONNULL(1, 2)
mm_event_trypost_1(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1);

void NONNULL(1, 2)
mm_event_post_2(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2);

bool NONNULL(1, 2)
mm_event_trypost_2(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2);

void NONNULL(1, 2)
mm_event_post_3(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3);

bool NONNULL(1, 2)
mm_event_trypost_3(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3);

void NONNULL(1, 2)
mm_event_post_4(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

bool NONNULL(1, 2)
mm_event_trypost_4(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

void NONNULL(1, 2)
mm_event_post_5(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

bool NONNULL(1, 2)
mm_event_trypost_5(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

void NONNULL(1, 2)
mm_event_post_6(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

bool NONNULL(1, 2)
mm_event_trypost_6(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

#endif /* BASE_EVENT_EVENT_H */
