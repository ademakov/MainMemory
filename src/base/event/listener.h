/*
 * base/event/listener.h - MainMemory event listener.
 *
 * Copyright (C) 2015-2020  Aleksey Demakov
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
#include "base/context.h"
#include "base/task.h"
#include "base/timeq.h"
#include "base/event/backend.h"
#include "base/event/epoch.h"

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

#if ENABLE_EVENT_STATS
/* Event listener statistics. */
struct mm_event_listener_stats
{
	uint64_t poll_calls;
	uint64_t zero_poll_calls;
	uint64_t wait_calls;
	uint64_t zero_wait_calls;

	uint64_t events;
	uint64_t forwarded_events;
	uint64_t repeatedly_forwarded_events;
};
#endif

struct mm_event_listener
{
	/* Associated context. */
	struct mm_context *context;

	/* The top-level event dispatch data. */
	struct mm_event_dispatch *dispatch;

	/* The number of handled events on the last poll. */
	uint64_t events;

#if ENABLE_LINUX_FUTEX
	/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
	semaphore_t semaphore;
#else
	struct mm_thread_monitor monitor;
#endif

	/* Queue of delayed tasks. */
	struct mm_timeq timer_queue;

	/* Event sink reclamation data. */
	struct mm_event_epoch_local epoch;

	/* Private part of the event backend. */
	struct mm_event_backend_local backend;

	/* Statistics. */
	/* The number of cross-thread wake-up notifications. */
	uint64_t notifications;
#if ENABLE_EVENT_STATS
	struct mm_event_listener_stats stats;
#endif

} CACHE_ALIGN;

/**********************************************************************
 * Event listener initialization and cleanup.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch);

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener);

/**********************************************************************
 * Event listener sleep and wake up helpers.
 **********************************************************************/

/* Announce that the event listener is running. */
static inline void NONNULL(1)
mm_event_listener_running(struct mm_event_listener *listener)
{
	mm_memory_store(listener->context->status, MM_CONTEXT_RUNNING);
}

/* Prepare the event listener to one of the sleeping states. */
static inline mm_stamp_t NONNULL(1)
mm_event_listener_posture(struct mm_event_listener *listener, mm_context_status_t status)
{
	mm_stamp_t stamp = mm_ring_mpsc_dequeue_stamp(&listener->context->async_queue);
	mm_atomic_uintptr_fetch_and_set(&listener->context->status, (((uintptr_t) stamp) << 2) | status);
	return stamp;
}

/* Verify that the event listener has nothing to do but sleep. */
static inline bool NONNULL(1)
mm_event_listener_restful(struct mm_event_listener *listener, mm_stamp_t stamp)
{
	return stamp == mm_ring_mpmc_enqueue_stamp(&listener->context->async_queue);
}

/* Prepare the event listener for polling. */
static inline mm_stamp_t NONNULL(1)
mm_event_listener_polling(struct mm_event_listener *listener)
{
	return mm_event_listener_posture(listener, MM_CONTEXT_POLLING);
}

#if ENABLE_LINUX_FUTEX
static inline uintptr_t NONNULL(1)
mm_event_listener_futex(struct mm_event_listener *listener)
{
	return (uintptr_t) &listener->context->async_queue.tail;
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
	mm_stamp_t stamp = mm_event_listener_posture(listener, MM_CONTEXT_WAITING);
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
	listener->events = 0;
}

static inline bool NONNULL(1)
mm_event_listener_got_events(struct mm_event_listener *listener)
{
	return listener->events != 0;
}

/**********************************************************************
 * Interface for handling incoming events.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_listener_input(struct mm_event_listener *listener, struct mm_event_fd *sink, uint32_t flags);

void NONNULL(1, 2)
mm_event_listener_output(struct mm_event_listener *listener, struct mm_event_fd *sink, uint32_t flags);

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink);

static inline void NONNULL(1)
mm_event_listener_flush(struct mm_event_listener *listener UNUSED)
{
#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.events += listener->events;
#endif
}

#endif /* BASE_EVENT_LISTENER_H */
