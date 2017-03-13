/*
 * base/event/listener.c - MainMemory event listener.
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

#include "base/event/listener.h"

#include "base/bitops.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/memory/memory.h"

#if ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#endif

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
		mm_backend_poller_handle(&listener->storage, sink, event);
		listener->dequeued_events++;
	}
}

/**********************************************************************
 * Event backend interface.
 **********************************************************************/

void NONNULL(1)
mm_event_listener_handle_start(struct mm_event_listener *listener, uint32_t nevents)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

	// Acquire coarse-grained event sink lock.
	mm_regular_lock(&dispatch->sink_lock);

	// Try to pull events from the event sink queue.
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
mm_event_listener_handle_finish(struct mm_event_listener *listener)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

	uint16_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
	mm_memory_store(dispatch->sink_queue_num, nq);

	// Release coarse-grained event sink lock.
	mm_regular_unlock(&dispatch->sink_lock);

	// Flush forwarded events.
	if (listener->forwarded_events)
		mm_event_forward_flush(&listener->forward);
	if (listener->enqueued_events > MM_EVENT_LISTENER_RETAIN_MIN)
		mm_event_notify_any(dispatch);

	// TODO: at this point it might miss disable and reclaim events.
#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.direct_events += listener->direct_events;
	listener->stats.enqueued_events += listener->enqueued_events;
	listener->stats.dequeued_events += listener->dequeued_events;
	listener->stats.forwarded_events += listener->forwarded_events;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_handle(struct mm_event_listener *listener, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();

	if (unlikely(sink->stray_target)) {
		// Handle the event immediately.
		mm_event_handle(sink, event);

#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif

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
		mm_event_update(sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == listener->target) {
			mm_backend_poller_handle(&listener->storage, sink, event);
			listener->direct_events++;
		} else if (target != MM_THREAD_NONE) {
			mm_event_forward(&listener->forward, sink, event);
			listener->forwarded_events++;
		} else {
			mm_event_listener_enqueue_sink(listener, sink, event);
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	// Count the received event.
	mm_event_update(sink);

	// Initiate event sink reclamation unless the client code asked
	// otherwise.
	if (likely(sink->status != MM_EVENT_INVALID)) {
		// Queue it for reclamation.
		mm_event_epoch_retire(&listener->epoch, sink);
		// Let close the file descriptor.
		mm_event_handle(sink, MM_EVENT_RETIRE);
	}

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

	// Initialize event sink reclamation data.
	mm_event_epoch_prepare_local(&listener->epoch);

#if ENABLE_EVENT_STATS
	// Initialize the statistic counters.
	listener->stats.poll_calls = 0;
	listener->stats.zero_poll_calls = 0;
	listener->stats.wait_calls = 0;
	listener->stats.stray_events = 0;
	listener->stats.direct_events = 0;
	listener->stats.enqueued_events = 0;
	listener->stats.dequeued_events = 0;
	listener->stats.forwarded_events = 0;
#endif

	// Initialize private event storage.
	mm_event_backend_storage_prepare(&listener->storage);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener UNUSED)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_thread_monitor_cleanup(&listener->monitor);
#endif

	LEAVE();
}
