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
#include "base/stdcall.h"
#include "base/event/dispatch.h"
#include "base/fiber/strand.h"
#include "base/memory/memory.h"

#if ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#endif

#define MM_LISTINER_QUEUE_MIN_SIZE 16

/**********************************************************************
 * Event sink queue.
 **********************************************************************/

static void
mm_event_listener_enqueue_sink(struct mm_event_listener *listener, struct mm_event_fd *sink, mm_event_t event)
{
	uint8_t bit = 1u << event;
	if ((sink->queued_events & bit) == 0) {
		mm_event_update(sink);

		if (sink->queued_events == 0) {
			struct mm_event_dispatch *dispatch = listener->dispatch;
			uint16_t mask = dispatch->sink_queue_size - 1;
			uint16_t index = dispatch->sink_queue_tail++ & mask;
			dispatch->sink_queue[index] = sink;
		}

		sink->queued_events |= bit;
		listener->events.enqueued++;
	}
}

static void
mm_event_listener_dequeue_sink(struct mm_event_listener *listener)
{
	struct mm_event_dispatch *dispatch = listener->dispatch;
	uint16_t mask = dispatch->sink_queue_size - 1;
	uint16_t index = dispatch->sink_queue_head++ & mask;

	struct mm_event_fd *sink = dispatch->sink_queue[index];
	sink->listener = listener;

	while (sink->queued_events) {
		mm_event_t event = mm_ctz(sink->queued_events);
		sink->queued_events &= ~(1u << event);
		if (event < MM_EVENT_OUTPUT)
			mm_event_backend_poller_input(&listener->storage, sink, event);
		else
			mm_event_backend_poller_output(&listener->storage, sink, event);
		listener->events.dequeued++;
	}
}

/**********************************************************************
 * Interface for handling incoming events.
 **********************************************************************/

static void
mm_event_listener_test_binding(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	// If the event sink can be detached from its target thread
	// then do it now. But make sure the target thread has some
	// minimal amount if work.
	if ((sink->flags & MM_EVENT_FIXED_LISTENER) == 0 && !mm_event_active(sink)) {
		ASSERT(sink->listener != NULL);
		uint16_t nr = listener->events.dequeued;
		if (sink->listener == listener) {
			nr += listener->events.direct;
			if (nr >= MM_EVENT_LISTENER_RETAIN_MAX)
				sink->listener = NULL;
		} else {
			nr += max(listener->events.direct, listener->direct_events_estimate);
			if (nr < MM_EVENT_LISTENER_RETAIN_MIN) {
				sink->listener = listener;
			} else {
				mm_thread_t target = sink->listener - listener->dispatch->listeners;
				uint32_t ntotal = listener->forward.buffers[target].ntotal;
				if (ntotal >= MM_EVENT_LISTENER_FORWARD_MAX)
					sink->listener = NULL;
			}
		}
	}
}

void NONNULL(1)
mm_event_listener_handle_queued(struct mm_event_listener *listener)
{
	ENTER();

	// Prepare the backend for handling events.
	mm_event_backend_poller_start(&listener->storage);

	struct mm_event_dispatch *dispatch = listener->dispatch;

	// Acquire coarse-grained event sink lock.
	if (!mm_regular_trylock(&dispatch->sink_lock))
		goto leave;

	// Try to pull events from the event sink queue.
	uint16_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
	for (; nq; --nq) {
		mm_event_listener_dequeue_sink(listener);
		if (listener->events.dequeued >= MM_EVENT_LISTENER_RETAIN_MAX)
			break;
	}
	mm_memory_store(dispatch->sink_queue_num, nq);

	// Release coarse-grained event sink lock.
	mm_regular_unlock(&dispatch->sink_lock);

	// Make the backend done with handling events.
	mm_event_backend_poller_finish(&dispatch->backend, &listener->storage);

#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.dequeued_events += listener->events.dequeued;
#endif

leave:
	LEAVE();
}

void NONNULL(1)
mm_event_listener_handle_start(struct mm_event_listener *listener, uint32_t nevents)
{
	ENTER();

	// Prepare the backend for handling events.
	mm_event_backend_poller_start(&listener->storage);

	struct mm_event_dispatch *dispatch = listener->dispatch;

	// Acquire coarse-grained event sink lock.
	mm_regular_lock(&dispatch->sink_lock);

	// Try to pull events from the event sink queue.
	uint16_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
	for (; nq; --nq) {
		mm_event_listener_dequeue_sink(listener);
		if (listener->events.dequeued >= MM_EVENT_LISTENER_RETAIN_MAX
		    && (nq + nevents) <= dispatch->sink_queue_size)
			break;
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
	if (listener->events.forwarded)
		mm_event_forward_flush(&listener->forward);
	if (listener->events.enqueued > MM_EVENT_LISTENER_RETAIN_MIN)
		mm_event_wakeup_any(dispatch);

	// Make the backend done with handling events.
	mm_event_backend_poller_finish(&dispatch->backend, &listener->storage);

	// TODO: at this point it might miss disable and reclaim events.
#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.direct_events += listener->events.direct;
	listener->stats.enqueued_events += listener->events.enqueued;
	listener->stats.dequeued_events += listener->events.dequeued;
	listener->stats.forwarded_events += listener->events.forwarded;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_input(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	if (unlikely((sink->flags & MM_EVENT_NOTIFY_FD) != 0)) {
		// Handle the event immediately.
		mm_event_handle_input(sink, MM_EVENT_READ_READY);
#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif
	} else {
		// Unbind or rebind the sink if appropriate.
		mm_event_listener_test_binding(listener, sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (sink->listener == listener) {
			mm_event_update(sink);
			mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT);
			listener->events.direct++;
		} else if (sink->listener != NULL) {
			mm_event_update(sink);
			mm_event_forward(&listener->forward, sink, MM_EVENT_INPUT);
			listener->events.forwarded++;
		} else {
			mm_event_listener_enqueue_sink(listener, sink, MM_EVENT_INPUT);
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_input_error(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	if (unlikely((sink->flags & MM_EVENT_NOTIFY_FD) != 0)) {
		// Handle the event immediately.
		mm_event_handle_input(sink, MM_EVENT_READ_ERROR);
#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif
	} else {
		// Unbind or rebind the sink if appropriate.
		mm_event_listener_test_binding(listener, sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (sink->listener == listener) {
			mm_event_update(sink);
			mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT_ERROR);
			listener->events.direct++;
		} else if (sink->listener != NULL) {
			mm_event_update(sink);
			mm_event_forward(&listener->forward, sink, MM_EVENT_INPUT_ERROR);
			listener->events.forwarded++;
		} else {
			mm_event_listener_enqueue_sink(listener, sink, MM_EVENT_INPUT_ERROR);
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_output(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	if (unlikely((sink->flags & MM_EVENT_NOTIFY_FD) != 0)) {
		// Handle the event immediately.
		mm_event_handle_output(sink, MM_EVENT_WRITE_READY);
#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif
	} else {
		// Unbind or rebind the sink if appropriate.
		mm_event_listener_test_binding(listener, sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (sink->listener == listener) {
			mm_event_update(sink);
			mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT);
			listener->events.direct++;
		} else if (sink->listener != NULL) {
			mm_event_update(sink);
			mm_event_forward(&listener->forward, sink, MM_EVENT_OUTPUT);
			listener->events.forwarded++;
		} else {
			mm_event_listener_enqueue_sink(listener, sink, MM_EVENT_OUTPUT);
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_output_error(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	if (unlikely((sink->flags & MM_EVENT_NOTIFY_FD) != 0)) {
		// Handle the event immediately.
		mm_event_handle_output(sink, MM_EVENT_WRITE_ERROR);
#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif
	} else {
		// Unbind or rebind the sink if appropriate.
		mm_event_listener_test_binding(listener, sink);

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (sink->listener == listener) {
			mm_event_update(sink);
			mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT_ERROR);
			listener->events.direct++;
		} else if (sink->listener != NULL) {
			mm_event_update(sink);
			mm_event_forward(&listener->forward, sink, MM_EVENT_OUTPUT_ERROR);
			listener->events.forwarded++;
		} else {
			mm_event_listener_enqueue_sink(listener, sink, MM_EVENT_OUTPUT_ERROR);
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
	if (likely((sink->flags & MM_EVENT_BROKEN) == 0)) {
		// Queue it for reclamation.
		mm_event_epoch_retire(&listener->epoch, sink);

		// Close the file descriptor.
		ASSERT(sink->fd >= 0);
		mm_close(sink->fd);
		sink->fd = -1;
	}

	LEAVE();
}

/**********************************************************************
 * Event listener initialization and cleanup.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
			  struct mm_strand *strand, uint32_t listener_queue_size)
{
	ENTER();

	listener->state = 0;

	// Set the pointers among associated entities.
	listener->strand = strand;
	listener->dispatch = dispatch;
	strand->listener = listener;
	strand->dispatch = dispatch;

	// Create the private request queue.
	uint32_t sz = mm_upper_pow2(listener_queue_size);
	if (sz < MM_LISTINER_QUEUE_MIN_SIZE)
		sz = MM_LISTINER_QUEUE_MIN_SIZE;
	listener->async_queue = mm_ring_mpmc_create(sz);

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

	// Initialize the statistic counters.
	mm_event_listener_clear_events(listener);
#if ENABLE_EVENT_STATS
	listener->stats.poll_calls = 0;
	listener->stats.zero_poll_calls = 0;
	listener->stats.wait_calls = 0;
	listener->stats.omit_calls = 0;
	listener->stats.stray_events = 0;
	listener->stats.direct_events = 0;
	listener->stats.enqueued_events = 0;
	listener->stats.dequeued_events = 0;
	listener->stats.forwarded_events = 0;
	listener->stats.enqueued_async_calls = 0;
	listener->stats.enqueued_async_posts = 0;
	listener->stats.dequeued_async_calls = 0;
	listener->stats.dequeued_async_posts = 0;
#endif

	// Initialize private event storage.
	mm_event_backend_storage_prepare(&listener->storage);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener)
{
	ENTER();

	// Destroy the associated request queue.
	mm_ring_mpmc_destroy(listener->async_queue);

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_thread_monitor_cleanup(&listener->monitor);
#endif

	LEAVE();
}
