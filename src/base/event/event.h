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
	MM_EVENT_INPUT,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT,
	MM_EVENT_OUTPUT_ERROR,
	MM_EVENT_RETIRE,
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

/* Event sink thread affinity. */
typedef enum {
	/* Bound to a single thread forever. */
	MM_EVENT_BOUND,
	/* Bound to a single thread while active. */
	MM_EVENT_LOOSE,
	/* Might handle events concurrently on random threads. */
	MM_EVENT_STRAY,
} mm_event_affinity_t;

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

/* Event sink status. */
enum {
	MM_EVENT_INVALID = -2,
	MM_EVENT_DROPPED = -1,
	MM_EVENT_INITIAL = 0,
	MM_EVENT_ENABLED = 1,
	MM_EVENT_CHANGED = 2,
};

/* Per-sink event counter. */
typedef uint16_t mm_event_stamp_t;

/* Event handler routine. */
struct mm_event_fd;
typedef void (*mm_event_handler_t)(mm_event_t event, struct mm_event_fd *sink);

/**********************************************************************
 * I/O event sink.
 **********************************************************************/

struct mm_event_fd
{
	/* Event handler routine. */
	mm_event_handler_t handler;

	/* Listener (along with associated thread) that owns the sink. */
	struct mm_event_listener *listener;

	/* File descriptor to watch. */
	int fd;

	/* State flags. */
	uint32_t flags;

	/* Current event sink status. */
	int16_t status;

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
	unsigned stray_target : 1;
	unsigned bound_target : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned regular_output : 1;
	unsigned oneshot_output : 1;

	/* Pending events for sinks in the dispatch queue. */
	uint8_t queued_events;

	/* Fibers bound to perform I/O. */
	struct mm_fiber *reader;
	struct mm_fiber *writer;
	/* Work entries to perform I/O. */
	struct mm_work reader_work;
	struct mm_work writer_work;
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
 * Event sink activity.
 **********************************************************************/

/* Mark a sink as having an incoming event received from the system. */
static inline void
mm_event_update(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	sink->receive_stamp++;
#endif
}

/* Mark a sink as having completed the processing of all the events
   delivered to the target thread so far. */
static inline void NONNULL(1)
mm_event_handle_complete(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	/* TODO: release memory fence */
	mm_memory_store(sink->complete_stamp, sink->dispatch_stamp);
#endif
}

/* Start asynchronous processing of an event as it is delivered to
   the target thread. */
static inline void NONNULL(1)
mm_event_handle(struct mm_event_fd *sink, mm_event_t event)
{
#if ENABLE_SMP
	/* Count the delivered event. */
	sink->dispatch_stamp++;
#endif
	/* Initiate its processing. */
	(sink->handler)(event, sink);
}

/* Check if a sink has some not yet fully processed events. */
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

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 3)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_handler_t handler,
		    mm_event_capacity_t input, mm_event_capacity_t output,
		    mm_event_affinity_t target);

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_unregister_fd(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_unregister_invalid_fd(struct mm_event_fd *sink);

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
