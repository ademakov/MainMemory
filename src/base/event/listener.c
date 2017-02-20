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

#include "base/bitops.h"
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

/**********************************************************************
 * Event listener sleep and wake up helpers.
 **********************************************************************/

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

/**********************************************************************
 * Event sink reclamation.
 **********************************************************************/

static bool
mm_event_listener_reclaim_queue_empty(struct mm_event_listener *listener)
{
	return (mm_stack_empty(&listener->reclaim_queue[0])
		&& mm_stack_empty(&listener->reclaim_queue[1]));
}

static void
mm_event_listener_reclaim_queue_insert(struct mm_event_listener *listener,
				       struct mm_event_fd *sink)
{
	uint32_t epoch = listener->reclaim_epoch;
	struct mm_stack *stack = &listener->reclaim_queue[epoch & 1];
	mm_stack_insert(stack, &sink->reclaim_link);
}

static void
mm_event_listener_reclaim_epoch(struct mm_event_listener *listener, uint32_t epoch)
{
	struct mm_stack *stack = &listener->reclaim_queue[epoch & 1];
	while (!mm_stack_empty(stack)) {
		struct mm_slink *link = mm_stack_remove(stack);
		struct mm_event_fd *sink = containerof(link, struct mm_event_fd, reclaim_link);
		mm_event_convey(sink, MM_EVENT_RECLAIM);
	}
}

void NONNULL(1)
mm_event_listener_observe_epoch(struct mm_event_listener *listener)
{
	ENTER();

	// Reclaim queued event sinks associated with a past epoch.
	uint32_t epoch = mm_memory_load(listener->dispatch->reclaim_epoch);
	uint32_t local = listener->reclaim_epoch;
	if (local != epoch) {
		VERIFY((local + 1) == epoch);
		mm_memory_store(listener->reclaim_epoch, epoch);
		mm_event_listener_reclaim_epoch(listener, epoch);
		mm_event_dispatch_advance_epoch(listener->dispatch);
	}

	// Finish reclamation if there are no more queued event sinks.
	if (mm_event_listener_reclaim_queue_empty(listener)) {
		mm_memory_store_fence();
		mm_memory_store(listener->reclaim_active, false);
	}

	LEAVE();
}

/**********************************************************************
 * Event listener poll helpers.
 **********************************************************************/

static inline void NONNULL(1)
mm_event_listener_poll_start(struct mm_event_listener *listener)
{
	ENTER();

	// No events arrived yet.
	listener->direct_events_estimate = 0;
	listener->direct_events = 0;
	listener->enqueued_events = 0;
	listener->dequeued_events = 0;
	listener->forwarded_events = 0;

	// Start a reclamation-critical section.
	if (!listener->reclaim_active) {
		mm_memory_store(listener->reclaim_active, true);
		mm_memory_strict_fence();
		// Catch up with the current reclamation epoch.
		uint32_t epoch = mm_memory_load(listener->dispatch->reclaim_epoch);
		mm_memory_store(listener->reclaim_epoch, epoch);
	}

	LEAVE();
}

static inline void NONNULL(1)
mm_event_listener_poll_finish(struct mm_event_listener *listener)
{
	ENTER();

	listener->stats.direct_events += listener->direct_events;
	listener->stats.enqueued_events += listener->enqueued_events;
	listener->stats.dequeued_events += listener->dequeued_events;

	// Flush and count forwarded events.
	if (listener->forwarded_events) {
		listener->stats.forwarded_events += listener->forwarded_events;

		struct mm_event_dispatch *dispatch = listener->dispatch;
		struct mm_event_forward_cache *cache = &listener->forward;

		mm_thread_t target = mm_bitset_find(&cache->targets, 0);
		while (target != MM_THREAD_NONE) {
			struct mm_event_listener *target_listener = &dispatch->listeners[target];
			struct mm_event_forward_buffer *buffer = &cache->buffers[target];
			mm_event_forward_flush(target_listener->thread, buffer);
			buffer->ntotal = 0;

			if (++target < mm_bitset_size(&cache->targets))
				target = mm_bitset_find(&cache->targets, target);
			else
				target = MM_THREAD_NONE;
		}

		mm_bitset_clear_all(&cache->targets);
	}

	// Advance the reclamation epoch.
	mm_event_listener_observe_epoch(listener);

	LEAVE();
}

/**********************************************************************
 * Event sink queue.
 **********************************************************************/

static void
mm_event_listener_enqueue_sink(struct mm_event_listener *listener, struct mm_event_fd *sink, mm_event_t event)
{
	uint8_t bit = 1 << event;
	if (sink->queued_events == 0) {
		sink->queued_events = bit;
		listener->enqueued_events++;

		struct mm_event_dispatch *dispatch = listener->dispatch;
		uint16_t mask = dispatch->sink_queue_size - 1;
		uint16_t index = dispatch->sink_queue_tail++ & mask;
		dispatch->sink_queue[index] = sink;

	} else if ((sink->queued_events & bit) == 0) {
		sink->queued_events |= bit;
		listener->enqueued_events++;
	}
}

static void
mm_event_listener_dequeue_sink(struct mm_event_listener *listener)
{
	struct mm_event_dispatch *dispatch = listener->dispatch;
	uint16_t mask = dispatch->sink_queue_size - 1;
	uint16_t index = dispatch->sink_queue_head++ & mask;
	struct mm_event_fd *sink = dispatch->sink_queue[index];

	sink->target = listener->target;
	while (sink->queued_events) {
		mm_event_t event = mm_ctz(sink->queued_events);
		sink->queued_events ^= 1 << event;
		mm_event_convey(sink, event);
		listener->dequeued_events++;
	}
}

/**********************************************************************
 * Event backend interface.
 **********************************************************************/

void NONNULL(1)
mm_event_listener_dispatch_start(struct mm_event_listener *listener, uint32_t nevents)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

	mm_regular_lock(&dispatch->sink_lock);

	for (;;) {
		uint16_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
		if (nq == 0)
			break;

		if ((nq + nevents) <= dispatch->sink_queue_size) {
			uint16_t nr = listener->direct_events + listener->dequeued_events;
			if (nr >= MM_EVENT_LISTENER_RETAIN_MAX)
				break;
		}

		mm_event_listener_dequeue_sink(listener);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_listener_dispatch_finish(struct mm_event_listener *listener)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

	uint16_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
	mm_memory_store(dispatch->sink_queue_num, nq);

	mm_regular_unlock(&dispatch->sink_lock);

	if (listener->enqueued_events > MM_EVENT_LISTENER_RETAIN_MIN)
		mm_event_dispatch_notify_waiting(dispatch);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_dispatch(struct mm_event_listener *listener, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();

	if (unlikely(sink->loose_target)) {
		// Handle the event immediately.
		mm_event_convey(sink, event);
		listener->stats.loose_events++;

	} else {
		mm_thread_t target = mm_event_target(sink);

		// If the event sink can be detached from its target thread
		// then do it now. But make sure the target thread has some
		// minimal amount if work.
		if (!sink->bound_target && !mm_event_active(sink)) {
			ASSERT(target != MM_THREAD_NONE);
			uint16_t nr = listener->dequeued_events;
			if (target == listener->target) {
				nr += listener->direct_events;
				if (nr >= MM_EVENT_LISTENER_RETAIN_MAX)
					sink->target = target = MM_THREAD_NONE;
			} else {
				nr += max(listener->direct_events, listener->direct_events_estimate);
				if (nr < MM_EVENT_LISTENER_RETAIN_MIN)
					sink->target = target = listener->target;
				else if (listener->forward.buffers[target].ntotal >= MM_EVENT_LISTENER_FORWARD_MAX)
					sink->target = target = MM_THREAD_NONE;
			}
		}

		// Count the received event.
		mm_event_update_receive_stamp(sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == listener->target) {
			mm_event_convey(sink, event);
			listener->direct_events++;
		} else if (target == MM_THREAD_NONE) {
			mm_event_listener_enqueue_sink(listener, sink, event);
		} else {
			mm_event_forward(&listener->forward, listener->dispatch, sink, event);
			listener->forwarded_events++;
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	mm_event_update_receive_stamp(sink);
	mm_event_listener_reclaim_queue_insert(listener, sink);
	mm_event_convey(sink, MM_EVENT_DISABLE);

	LEAVE();
}

/**********************************************************************
 * Event listener initialization and cleanup.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
			  struct mm_thread *thread)
{
	ENTER();

	listener->state = 0;

	// Remember the owners.
	mm_thread_t thread_number = listener - dispatch->listeners;
	listener->target = thread_number;
	listener->thread = thread;
	listener->dispatch = dispatch;

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

	// Initialize event forwarding data.
	mm_event_forward_prepare(&listener->forward, dispatch->nlisteners);

	// Initialize change event storage.
	mm_event_batch_prepare(&listener->changes, 256);

	// Initialize the reclamation data.
	listener->reclaim_epoch = 0;
	listener->reclaim_active = false;
	mm_stack_prepare(&listener->reclaim_queue[0]);
	mm_stack_prepare(&listener->reclaim_queue[1]);

	// Initialize the statistic counters.
	listener->stats.poll_calls = 0;
	listener->stats.zero_poll_calls = 0;
	listener->stats.wait_calls = 0;
	listener->stats.loose_events = 0;
	listener->stats.direct_events = 0;
	listener->stats.enqueued_events = 0;
	listener->stats.dequeued_events = 0;
	listener->stats.forwarded_events = 0;

	// Initialize private event storage.
	mm_event_backend_storage_prepare(&listener->storage);

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

/**********************************************************************
 * Event listener main functionality -- listening and notification.
 **********************************************************************/

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
			mm_event_backend_notify(&listener->dispatch->backend);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_listener_poll(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	ENTER();

	// Update statistics.
	listener->stats.poll_calls++;
	listener->stats.zero_poll_calls += (timeout == 0);

	// Prepare to receive events.
	mm_event_listener_poll_start(listener);

	if (timeout != 0) {
		// Get the next expected notify stamp.
		const mm_ring_seqno_t stamp = mm_event_listener_dequeue_stamp(listener);

		// Cleanup stale event notifications.
		mm_event_backend_dampen(&listener->dispatch->backend);

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
	mm_event_backend_listen(&listener->dispatch->backend,
				&listener->changes, listener, timeout);

	// Advertise the start of another working cycle.
	mm_memory_store(listener->state, MM_EVENT_LISTENER_RUNNING);

	// Flush received events.
	mm_event_listener_poll_finish(listener);

	// Forget just handled change events.
	mm_event_listener_clear_changes(listener);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_wait(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(timeout != 0);

	// Update statistics.
	listener->stats.wait_calls++;

	// Get the next expected notify stamp.
	const mm_ring_seqno_t stamp = mm_event_listener_dequeue_stamp(listener);

	// Advertise that the thread is about to sleep.
	uintptr_t state = (stamp << 2) | MM_EVENT_LISTENER_WAITING;
	mm_memory_store(listener->state, state);
	mm_memory_strict_fence(); // TODO: store_load fence

	// Wait for a wake-up notification or timeout unless
	// an already pending notification is detected.
	if (stamp == mm_event_listener_enqueue_stamp(listener))
		mm_event_listener_timedwait(listener, stamp, timeout);

	// Advertise the start of another working cycle.
	mm_memory_store(listener->state, MM_EVENT_LISTENER_RUNNING);

	LEAVE();
}
