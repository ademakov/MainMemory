/*
 * base/event/listener.h - MainMemory event listener.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#ifndef BASE_EVENT_LISTENER_H
#define BASE_EVENT_LISTENER_H

#include "common.h"
#include "base/ring.h"
#include "base/event/backend.h"
#include "base/event/epoch.h"
#include "base/event/forward.h"

#if HAVE_LINUX_FUTEX_H
# define ENABLE_LINUX_FUTEX	1
#elif HAVE_MACH_SEMAPHORE_H
# define ENABLE_MACH_SEMAPHORE	1
#endif

#if ENABLE_LINUX_FUTEX
# include "base/syscall.h"
# include <linux/futex.h>
# include <sys/syscall.h>
#elif ENABLE_MACH_SEMAPHORE
# include <mach/semaphore.h>
#else
# include "base/thread/monitor.h"
# include "base/clock.h"
#endif

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_strand;

#define MM_EVENT_LISTENER_RETAIN_MIN	(3)
#define MM_EVENT_LISTENER_RETAIN_MAX	(6)
#define MM_EVENT_LISTENER_FORWARD_MAX	MM_EVENT_FORWARD_BUFFER_SIZE

#define MM_EVENT_LISTENER_STATUS	((uint32_t) 3)

typedef enum
{
	MM_EVENT_LISTENER_RUNNING = 0,
	MM_EVENT_LISTENER_POLLING = 1,
	MM_EVENT_LISTENER_WAITING = 2,
} mm_event_listener_status_t;

#if ENABLE_EVENT_STATS
/* Event listener statistics. */
struct mm_event_listener_stats
{
	uint64_t poll_calls;
	uint64_t zero_poll_calls;
	uint64_t wait_calls;
	uint64_t omit_calls;

	uint64_t stray_events;
	uint64_t direct_events;
	uint64_t enqueued_events;
	uint64_t dequeued_events;
	uint64_t forwarded_events;

	/* Counters of asynchronous procedure calls. */
	uint64_t enqueued_async_calls;
	uint64_t enqueued_async_posts;
	uint64_t dequeued_async_calls;
	uint64_t dequeued_async_posts;
};
#endif

struct mm_event_listener
{
	/*
	 * The listener state.
	 *
	 * The two least-significant bits contain a mm_event_listener_status_t
	 * value. The rest contain a snapshot of the dequeue stamp. On 32-bit
	 * platforms this discards its 2 most significant bits. However the 30
	 * remaining bits suffice to avoid any stamp clashes in practice.
	 */
	mm_atomic_uintptr_t state;

#if ENABLE_LINUX_FUTEX
	/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
	semaphore_t semaphore;
#else
	struct mm_thread_monitor monitor;
#endif

	union {
		struct {
			/* The number of directly handled events. */
			uint16_t direct;
			/* The number of events published in the sink queue. */
			uint16_t enqueued;
			uint16_t dequeued;
			/* The number of events forwarded to other listeners. */
			uint16_t forwarded;
		} events;

		/* Storage for event counters. */
		uint64_t events_any;
	};

	/* The number of locally handled events found while adjusting
	   the listener for appropriate event forwarding strategy. */
	uint16_t direct_events_estimate;

	/* Associated strand. */
	struct mm_strand *strand;

	/* The top-level event dispatch data. */
	struct mm_event_dispatch *dispatch;

	/* Asynchronous call queue. */
	struct mm_ring_mpmc *async_queue;

	/* Event sink reclamation data. */
	struct mm_event_epoch_local epoch;

	/* Listener's helper to forward events. */
	struct mm_event_forward_cache forward;

	/* Private event storage. */
	struct mm_event_backend_storage storage;

#if ENABLE_EVENT_STATS
	/* Statistics. */
	struct mm_event_listener_stats stats;
#endif

} CACHE_ALIGN;

/**********************************************************************
 * Event listener initialization and cleanup.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
			  struct mm_strand *strand, uint32_t listener_queue_size);

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener);

/**********************************************************************
 * Event listener sleep and wake up helpers.
 **********************************************************************/

/* Announce that the event listener is running. */
static inline void NONNULL(1)
mm_event_listener_running(struct mm_event_listener *listener)
{
	mm_memory_store(listener->state, MM_EVENT_LISTENER_RUNNING);
}

/* Prepare the event listener to one of the sleeping states. */
static inline mm_stamp_t NONNULL(1)
mm_event_listener_posture(struct mm_event_listener *listener, mm_event_listener_status_t status)
{
	mm_stamp_t stamp = mm_ring_mpsc_dequeue_stamp(listener->async_queue);
	mm_atomic_uintptr_fetch_and_set(&listener->state, (((uintptr_t) stamp) << 2) | status);
	return stamp;
}

/* Verify that the event listener has nothing to do but sleep. */
static inline bool NONNULL(1)
mm_event_listener_restful(struct mm_event_listener *listener, mm_stamp_t stamp)
{
	return stamp == mm_ring_mpmc_enqueue_stamp(listener->async_queue);
}

/* Prepare the event listener for polling. */
static inline mm_stamp_t NONNULL(1)
mm_event_listener_polling(struct mm_event_listener *listener)
{
	return mm_event_listener_posture(listener, MM_EVENT_LISTENER_POLLING);
}

#if ENABLE_LINUX_FUTEX
static inline uintptr_t NONNULL(1)
mm_event_listener_futex(struct mm_event_listener *listener)
{
	return (uintptr_t) &listener->thread->async_queue->base.tail;
}
#endif

static inline void NONNULL(1)
mm_event_listener_signal(struct mm_event_listener *listener)
{
#if ENABLE_LINUX_FUTEX
	mm_syscall_3(SYS_futex, mm_event_listener_futex(listener), FUTEX_WAKE_PRIVATE, 1);
#elif ENABLE_MACH_SEMAPHORE
	semaphore_signal(listener->semaphore);
#else
	mm_thread_monitor_lock(&listener->monitor);
	mm_thread_monitor_signal(&listener->monitor);
	mm_thread_monitor_unlock(&listener->monitor);
#endif
}

static inline void NONNULL(1)
mm_event_listener_timedwait(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	/* Announce that the thread is about to sleep. */
	mm_stamp_t stamp = mm_event_listener_posture(listener, MM_EVENT_LISTENER_WAITING);
	if (!mm_event_listener_restful(listener, stamp))
		goto leave;

#if ENABLE_LINUX_FUTEX
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	int rc = mm_syscall_4(SYS_futex, mm_event_listener_futex(listener), FUTEX_WAIT_PRIVATE,
			      stamp, (uintptr_t) &ts);
	if (rc != 0 && errno != EWOULDBLOCK && errno != ETIMEDOUT)
		mm_fatal(errno, "futex");
#elif ENABLE_MACH_SEMAPHORE
	mach_timespec_t ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	kern_return_t r = semaphore_timedwait(listener->semaphore, ts);
	if (r != KERN_SUCCESS && unlikely(r != KERN_OPERATION_TIMED_OUT))
		mm_fatal(0, "semaphore_timedwait");
#else
	mm_timeval_t time = mm_clock_gettime_realtime() + timeout;

	mm_thread_monitor_lock(&listener->monitor);
	if (mm_event_listener_restful(listener, stamp))
		mm_thread_monitor_timedwait(&listener->monitor, time);
	mm_thread_monitor_unlock(&listener->monitor);
#endif

leave:
	// Announce the start of another working cycle.
	mm_event_listener_running(listener);
}

/**********************************************************************
 * Event counters.
 **********************************************************************/

/* Reset event counters. */
static inline void NONNULL(1)
mm_event_listener_clear_events(struct mm_event_listener *listener)
{
	listener->events_any = 0;
}

static inline bool NONNULL(1)
mm_event_listener_got_events(struct mm_event_listener *listener)
{
	return listener->events_any != 0;
}

/**********************************************************************
 * Interface for estimating incoming events.
 **********************************************************************/

static inline bool NONNULL(1)
mm_event_listener_adjust_start(struct mm_event_listener *listener, uint32_t nevents)
{
	listener->direct_events_estimate = 0;
	return nevents > MM_EVENT_LISTENER_RETAIN_MIN;
}

static inline bool NONNULL(1, 2)
mm_event_listener_adjust(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	if (sink->listener == listener && (sink->flags & MM_EVENT_NOTIFY_FD) == 0)
		listener->direct_events_estimate++;
	return listener->direct_events_estimate < MM_EVENT_LISTENER_RETAIN_MIN;
}

/**********************************************************************
 * Interface for handling incoming events.
 **********************************************************************/

void NONNULL(1)
mm_event_listener_handle_queued(struct mm_event_listener *listener);

void NONNULL(1)
mm_event_listener_handle_start(struct mm_event_listener *listener, uint32_t nevents);

void NONNULL(1)
mm_event_listener_handle_finish(struct mm_event_listener *listener);

void NONNULL(1, 2)
mm_event_listener_input(struct mm_event_listener *listener, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_listener_input_error(struct mm_event_listener *listener, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_listener_output(struct mm_event_listener *listener, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_listener_output_error(struct mm_event_listener *listener, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink);

#endif /* BASE_EVENT_LISTENER_H */
