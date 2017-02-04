/*
 * base/event/listener.c - MainMemory event listener.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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

#include "base/event/listener.h"

#include "base/logger.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/memory/memory.h"

#if ENABLE_LINUX_FUTEX
# include "arch/syscall.h"
# include <linux/futex.h>
# include <sys/syscall.h>
#elif ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#else
# include "base/clock.h"
#endif

static uintptr_t
mm_event_listener_enqueue_stamp(struct mm_event_listener *listener)
{
	return mm_ring_mpmc_enqueue_stamp(listener->thread->request_queue);
}

static uintptr_t
mm_event_listener_dequeue_stamp(struct mm_event_listener *listener)
{
	return mm_ring_mpsc_dequeue_stamp(listener->thread->request_queue);
}

#if ENABLE_LINUX_FUTEX
static uintptr_t
mm_event_listener_futex(struct mm_event_listener *listener)
{
	return (uintptr_t) &listener->thread->request_queue->base.tail;
}
#endif

void NONNULL(1, 2, 3)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
			  struct mm_thread *thread)
{
	ENTER();

	listener->state = 0;

	listener->thread = thread;
	listener->busywait = 0;

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	kern_return_t r = semaphore_create(mach_task_self(), &listener->semaphore,
					   SYNC_POLICY_FIFO, 0);
	if (r != KERN_SUCCESS)
		mm_fatal(0, "semaphore_create");
#else
	mm_thread_monitor_prepare(&listener->monitor);
#endif

	// Initialize the receiver.
	mm_thread_t thread_number = listener - dispatch->listeners;
	mm_event_receiver_prepare(&listener->receiver, dispatch, thread_number);

	// Initialize change event storage.
	mm_event_batch_prepare(&listener->changes, 256);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_thread_monitor_cleanup(&listener->monitor);
#endif

	mm_event_batch_cleanup(&listener->changes);

	LEAVE();
}

static void NONNULL(1)
mm_event_listener_signal(struct mm_event_listener *listener)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	mm_syscall_3(SYS_futex,
		     mm_event_listener_futex(listener),
		     FUTEX_WAKE_PRIVATE, 1);
#elif ENABLE_MACH_SEMAPHORE
	semaphore_signal(listener->semaphore);
#else
	mm_thread_monitor_lock(&listener->monitor);
	mm_thread_monitor_signal(&listener->monitor);
	mm_thread_monitor_unlock(&listener->monitor);
#endif

	LEAVE();
}

static void NONNULL(1)
mm_event_listener_timedwait(struct mm_event_listener *listener, mm_ring_seqno_t stamp,
			    mm_timeout_t timeout)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a sleep.
	mm_log_relay();

	int rc = mm_syscall_4(SYS_futex,
			      mm_event_listener_futex(listener),
			      FUTEX_WAIT_PRIVATE, stamp, (uintptr_t) &ts);
	if (rc != 0 && errno != EWOULDBLOCK && errno != ETIMEDOUT)
		mm_fatal(errno, "futex");
#elif ENABLE_MACH_SEMAPHORE
	(void) stamp;

	mach_timespec_t ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a sleep.
	mm_log_relay();

	kern_return_t r = semaphore_timedwait(listener->semaphore, ts);
	if (r != KERN_SUCCESS && unlikely(r != KERN_OPERATION_TIMED_OUT))
		mm_fatal(0, "semaphore_timedwait");
#else
	mm_timeval_t time = mm_clock_gettime_realtime() + timeout;

	mm_thread_monitor_lock(&listener->monitor);
#if ENABLE_NOTIFY_STAMP
	if (stamp == mm_event_listener_enqueue_stamp(listener))
		mm_thread_monitor_timedwait(&listener->monitor, time);
#else
	if (listener->notify_stamp == stamp)
		mm_thread_monitor_timedwait(&listener->monitor, time);
#endif
	mm_thread_monitor_unlock(&listener->monitor);
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_listener_notify(struct mm_event_listener *listener, mm_ring_seqno_t stamp UNUSED)
{
	ENTER();

	uintptr_t state = mm_memory_load(listener->state);
	if ((stamp << 2) == (state & ~MM_EVENT_LISTENER_STATUS)) {
		// Get the current status of the listener. It might
		// become obsolete by the time the notification is
		// sent. This is not a problem however as it implies
		// the listener thread has woken up on its own and
		// seen all the sent data.
		//
		// Sometimes this might lead to an extra listener
		// wake up (if the listener makes a full cycle) or
		// a wrong listener being waken (if another listener
		// becomes polling). So listeners should be prepared
		// to get spurious wake up notifications.
		mm_event_listener_status_t status = state & MM_EVENT_LISTENER_STATUS;
		if (status == MM_EVENT_LISTENER_WAITING)
			mm_event_listener_signal(listener);
		else if (status == MM_EVENT_LISTENER_POLLING)
			mm_event_backend_notify(&listener->receiver.dispatch->backend);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_listener_poll(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	ENTER();

	// Prepare to receive events.
	mm_event_receiver_start(&listener->receiver);

	// Get the next expected notify stamp.
	const mm_ring_seqno_t stamp = mm_event_listener_dequeue_stamp(listener);

	if (timeout != 0) {
		// Cleanup stale event notifications.
		mm_event_backend_dampen(&listener->receiver.dispatch->backend);

		// Advertise that the thread is about to sleep.
		uintptr_t state = (stamp << 2) | MM_EVENT_LISTENER_POLLING;
		mm_memory_store(listener->state, state);
		mm_memory_strict_fence(); // TODO: store_load fence

		// Wait for a wake-up notification or timeout unless
		// an already pending notification is detected.
		if (stamp != mm_event_listener_enqueue_stamp(listener))
			timeout = 0;
	}

	// Check incoming events and wait for notification/timeout.
	mm_event_backend_listen(&listener->receiver.dispatch->backend,
				&listener->changes, &listener->receiver, timeout);

	// Advertise the start of another working cycle.
	mm_memory_store(listener->state, MM_EVENT_LISTENER_RUNNING);

	// Flush received events.
	mm_event_receiver_finish(&listener->receiver);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_wait(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	ENTER();

	// Get the next expected notify stamp.
	const mm_ring_seqno_t stamp = mm_event_listener_dequeue_stamp(listener);

	if (timeout != 0) {
		// Advertise that the thread is about to sleep.
		uintptr_t state = (stamp << 2) | MM_EVENT_LISTENER_WAITING;
		mm_memory_store(listener->state, state);
		mm_memory_strict_fence(); // TODO: store_load fence

		// Wait for a wake-up notification or timeout unless
		// an already pending notification is detected.
		if (stamp == mm_event_listener_enqueue_stamp(listener))
			mm_event_listener_timedwait(listener, stamp, timeout);
	}

	// Advertise the start of another working cycle.
	mm_memory_store(listener->state, MM_EVENT_LISTENER_RUNNING);

	LEAVE();
}
