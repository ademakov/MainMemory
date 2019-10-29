/*
 * base/event/listener.c - MainMemory event listener.
 *
 * Copyright (C) 2015-2019  Aleksey Demakov
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

#define MM_EVENT_LISTINER_QUEUE_MIN_SIZE	(16)

#if ENABLE_SMP

/* Mark a sink as having an incoming event received from the system. */
static inline void
mm_event_update(struct mm_event_fd *sink UNUSED)
{
	sink->receive_stamp++;
}

/* Check if a sink has some not yet fully processed events. */
static inline bool NONNULL(1)
mm_event_active(const struct mm_event_fd *sink UNUSED)
{
	// TODO: acquire memory fence
	mm_event_stamp_t stamp = mm_memory_load(sink->complete_stamp);
	return sink->receive_stamp != stamp;
}

static bool
mm_event_test_binding(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	// Cannot unbind certain kinds of sinks at all.
	if ((sink->flags & (MM_EVENT_FIXED_LISTENER | MM_EVENT_NOTIFY_FD)) != 0)
		return false;

	// Cannot unbind if there is some event handling activity.
	ASSERT(sink->listener != NULL);
	if (mm_event_active(sink))
		return true;

	// Attach the sink to the poller.
	sink->listener = listener;
	return true;
}

#endif

/**********************************************************************
 * Interface for handling incoming events.
 **********************************************************************/

void NONNULL(1)
mm_event_listener_handle_start(struct mm_event_listener *listener, uint32_t nevents UNUSED)
{
	ENTER();

	// Prepare the backend for handling events.
	mm_event_backend_poller_start(&listener->storage);

#if ENABLE_SMP && ENABLE_EVENT_SINK_LOCK
	// Acquire coarse-grained event sink lock.
	mm_regular_lock(&listener->dispatch->sink_lock);
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_listener_handle_finish(struct mm_event_listener *listener)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

#if ENABLE_SMP
#if ENABLE_EVENT_SINK_LOCK
	// Release coarse-grained event sink lock.
	mm_regular_unlock(&dispatch->sink_lock);
#endif

	// Flush forwarded events.
	if (listener->events.forwarded)
		mm_event_forward_flush(&listener->forward, dispatch);
#endif

	// Make the backend done with handling events.
	mm_event_backend_poller_finish(&dispatch->backend, &listener->storage);

	// TODO: at this point it might miss disable and reclaim events.
#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.direct_events += listener->events.direct;
	listener->stats.forwarded_events += listener->events.forwarded;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_input(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	if (!mm_event_test_binding(listener, sink)) {
		if ((sink->flags & MM_EVENT_NOTIFY_FD) != 0) {
			sink->flags |= MM_EVENT_INPUT_READY;
#if ENABLE_EVENT_STATS
			listener->stats.stray_events++;
#endif
			goto leave;
		}
	}

	// Count the received event.
	mm_event_update(sink);

	// If the event sink belongs to the poller thread then handle it immediately,
	// otherwise store it for later delivery to the target thread.
	if (sink->listener == listener) {
		mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT_READY);
		listener->events.direct++;
	} else {
		mm_event_forward(&listener->forward, sink, MM_EVENT_INDEX_INPUT);
		listener->events.forwarded++;
	}
#else
	if ((sink->flags & MM_EVENT_NOTIFY_FD) != 0) {
		sink->flags |= MM_EVENT_INPUT_READY;
#if ENABLE_EVENT_STATS
		listener->stats.stray_events++;
#endif
		goto leave;
	}

	mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT_READY);
	listener->events.direct++;
#endif

leave:
	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_input_error(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	if (!mm_event_test_binding(listener, sink)) {
		// Error events must never occur with notify FD.
		VERIFY((sink->flags & MM_EVENT_NOTIFY_FD) == 0);
	}

	// Count the received event.
	mm_event_update(sink);

	// If the event sink belongs to the poller thread then handle it immediately,
	// otherwise store it for later delivery to the target thread.
	if (sink->listener == listener) {
		mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT_ERROR);
		listener->events.direct++;
	} else {
		mm_event_forward(&listener->forward, sink, MM_EVENT_INDEX_INPUT_ERROR);
		listener->events.forwarded++;
	}
#else
	mm_event_backend_poller_input(&listener->storage, sink, MM_EVENT_INPUT_ERROR);
	listener->events.direct++;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_output(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	if (!mm_event_test_binding(listener, sink)) {
		// Never register notify FD for output events.
		ASSERT((sink->flags & MM_EVENT_NOTIFY_FD) == 0);
	}

	// Count the received event.
	mm_event_update(sink);

	// If the event sink belongs to the poller thread then handle it immediately,
	// otherwise store it for later delivery to the target thread.
	if (sink->listener == listener) {
		mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT_READY);
		listener->events.direct++;
	} else {
		mm_event_forward(&listener->forward, sink, MM_EVENT_INDEX_OUTPUT);
		listener->events.forwarded++;
	}
#else
	mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT_READY);
	listener->events.direct++;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_output_error(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	if (!mm_event_test_binding(listener, sink)) {
		// Never register notify FD for output events.
		ASSERT((sink->flags & MM_EVENT_NOTIFY_FD) == 0);
	}

	// Count the received event.
	mm_event_update(sink);

	// If the event sink belongs to the poller thread then handle it immediately,
	// otherwise store it for later delivery to he target thread.
	if (sink->listener == listener) {
		mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT_ERROR);
		listener->events.direct++;
	} else {
		mm_event_forward(&listener->forward, sink, MM_EVENT_INDEX_OUTPUT_ERROR);
		listener->events.forwarded++;
	}
#else
	mm_event_backend_poller_output(&listener->storage, sink, MM_EVENT_OUTPUT_ERROR);
	listener->events.direct++;
#endif

	LEAVE();
}

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Count the received event.
	mm_event_update(sink);
#endif

	// Initiate event sink reclamation unless the client code asked otherwise.
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
	listener->spin_count = 0;

	// Set the pointers among associated entities.
	listener->strand = strand;
	listener->dispatch = dispatch;
	strand->listener = listener;

	// Prepare storage for tasks.
	mm_task_list_prepare(&listener->tasks);

	// Create the private request queue.
	uint32_t sz = mm_upper_pow2(listener_queue_size);
	if (sz < MM_EVENT_LISTINER_QUEUE_MIN_SIZE)
		sz = MM_EVENT_LISTINER_QUEUE_MIN_SIZE;
	listener->async_queue = mm_ring_mpmc_create(sz);

	// Prepare the timer queue.
	mm_timeq_prepare(&listener->timer_queue, &mm_common_space.xarena);

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

	// Initialize event sink reclamation data.
	mm_event_epoch_prepare_local(&listener->epoch);

#if ENABLE_SMP
	// Initialize event forwarding data.
	mm_event_forward_prepare(&listener->forward, dispatch->nlisteners);
#endif

	// Initialize the statistic counters.
	mm_event_listener_clear_events(listener);
#if ENABLE_EVENT_STATS
	listener->stats.poll_calls = 0;
	listener->stats.zero_poll_calls = 0;
	listener->stats.wait_calls = 0;
	listener->stats.spin_count = 0;
	listener->stats.stray_events = 0;
	listener->stats.direct_events = 0;
	listener->stats.forwarded_events = 0;
	listener->stats.received_forwarded_events = 0;
	listener->stats.retargeted_forwarded_events = 0;
	listener->stats.enqueued_async_calls = 0;
	listener->stats.enqueued_async_posts = 0;
	listener->stats.dequeued_async_calls = 0;
	listener->stats.direct_calls = 0;
#endif

	// Initialize private event storage.
	mm_event_backend_storage_prepare(&listener->storage);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener)
{
	ENTER();

	// Destroy storage for tasks.
	mm_task_list_cleanup(&listener->tasks);

	// Destroy the timer queue.
	mm_timeq_destroy(&listener->timer_queue);

	// Destroy the associated request queue.
	mm_ring_mpmc_destroy(listener->async_queue);

#if ENABLE_SMP
	// Release event forwarding data.
	mm_event_forward_cleanup(&listener->forward);
#endif

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_thread_monitor_cleanup(&listener->monitor);
#endif

	LEAVE();
}
