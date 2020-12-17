/*
 * base/event/listener.c - MainMemory event listener.
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

#include "base/event/listener.h"

#include "base/async.h"
#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/dispatch.h"
#include "base/event/event.h"
#include "base/fiber/fiber.h"
#include "base/memory/alloc.h"

#if ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#endif

/**********************************************************************
 * Interface for handling incoming events.
 **********************************************************************/

#if ENABLE_SMP

/* Mark a sink as having an incoming event received from the system. */
static inline void
mm_event_received(struct mm_event_fd *sink)
{
	sink->poll_stamp++;
}

/* Mark a sink as processing a previously received event. */
static inline void
mm_event_delivered(struct mm_event_fd *sink)
{
	sink->task_stamp++;
}

/* Check if a sink has some not yet fully processed events. */
static inline bool NONNULL(1)
mm_event_active(const struct mm_event_fd *sink UNUSED)
{
	uint32_t stamp = mm_memory_load(sink->task_stamp);
	mm_memory_load_fence();
	uint32_t flags = mm_memory_load(sink->flags);

	return sink->poll_stamp != stamp || (flags & (MM_EVENT_INPUT_STARTED | MM_EVENT_OUTPUT_STARTED)) != 0;
}

static void
mm_event_test_binding(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	// Cannot unbind certain kinds of sinks at all.
	if ((sink->flags & (MM_EVENT_FIXED_POLLER)) != 0)
		return;

	// Cannot unbind if there is some event handling activity.
	ASSERT(sink->context != NULL);
	if (mm_event_active(sink))
		return;

	// Attach the sink to the poller.
	sink->context = listener->context;
}

#endif

static void NONNULL(1)
mm_event_listener_handle_input(struct mm_context *context, struct mm_event_fd *sink, uint32_t flags)
{
	ENTER();

	// Cleanup after a oneshot event.
	if ((sink->flags & MM_EVENT_ONESHOT_INPUT) != 0) {
		sink->flags &= ~MM_EVENT_ONESHOT_INPUT;
		mm_event_backend_disable_input(&context->listener->dispatch->backend, &context->listener->backend, sink);
	}

	// Update the read readiness flags.
	sink->flags |= flags;

	if (sink->input_fiber != NULL) {
		// Run the fiber blocked on input.
		mm_fiber_run(sink->input_fiber);
	} else if ((sink->flags & MM_EVENT_INPUT_STARTED) == 0) {
		// Start a new input work.
		sink->flags |= MM_EVENT_INPUT_STARTED;
		mm_context_add_task(context, &sink->tasks->input, (mm_value_t) sink);
	}

	// Count the delivered event.
	mm_memory_store_fence();
	mm_event_delivered(sink);

	LEAVE();
}

static void NONNULL(1)
mm_event_listener_handle_output(struct mm_context *context, struct mm_event_fd *sink, uint32_t flags)
{
	ENTER();

	// Cleanup after a oneshot event.
	if ((sink->flags & MM_EVENT_ONESHOT_OUTPUT) != 0) {
		sink->flags &= ~MM_EVENT_ONESHOT_OUTPUT;
		mm_event_backend_disable_output(&context->listener->dispatch->backend, &context->listener->backend, sink);
	}

	// Update the write readiness flags.
	sink->flags |= flags;

	if (sink->output_fiber != NULL) {
		// Run the fiber blocked on output.
		mm_fiber_run(sink->output_fiber);
	} else if ((sink->flags & MM_EVENT_OUTPUT_STARTED) == 0) {
		// Start a new output work.
		sink->flags |= MM_EVENT_OUTPUT_STARTED;
		mm_context_add_task(context, &sink->tasks->output, (mm_value_t) sink);
	}

	// Count the delivered event.
	mm_memory_store_fence();
	mm_event_delivered(sink);

	LEAVE();
}

#if ENABLE_SMP

static void
mm_event_listener_input_req(struct mm_context *const context, uintptr_t *arguments)
{
	// Fetch the arguments.
	struct mm_event_fd *const sink = (struct mm_event_fd *) arguments[0];
	const uint32_t flags = arguments[1];

	// Check to see if the event sink is already re-assigned to another
	// context and re-submit it there if needed.
	struct mm_context *const task_context = sink->context;
	if (unlikely(task_context != context)) {
		mm_async_call_2(task_context, mm_event_listener_input_req, (uintptr_t) sink, flags);
#if ENABLE_EVENT_STATS
		context->listener->stats.repeatedly_forwarded_events++;
#endif
		return;
	}

	// Start processing the event.
	mm_event_listener_handle_input(context, sink, flags);
}

static void
mm_event_listener_output_req(struct mm_context *const context, uintptr_t *arguments)
{
	// Fetch the arguments.
	struct mm_event_fd *const sink = (struct mm_event_fd *) arguments[0];
	const uint32_t flags = arguments[1];

	// Check to see if the event sink is already re-assigned to another
	// context and re-submit it there if needed.
	struct mm_context *const task_context = sink->context;
	if (unlikely(task_context != context)) {
		mm_async_call_2(task_context, mm_event_listener_output_req, (uintptr_t) sink, flags);
#if ENABLE_EVENT_STATS
		context->listener->stats.repeatedly_forwarded_events++;
#endif
		return;
	}

	// Start processing the event.
	mm_event_listener_handle_output(context, sink, flags);
}

#endif

void NONNULL(1, 2)
mm_event_listener_input(struct mm_event_listener *const listener, struct mm_event_fd *const sink, const uint32_t flags)
{
	// Update event statistics.
	listener->events++;

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	mm_event_test_binding(listener, sink);

	// Count the received event.
	mm_event_received(sink);

	// Submit the event to a peer context if needed.
	struct mm_context *const task_context = sink->context;
	if (task_context != listener->context) {
		mm_async_call_2(task_context, mm_event_listener_input_req, (uintptr_t) sink, flags);
#if ENABLE_EVENT_STATS
		listener->stats.forwarded_events++;
#endif
		return;
	}
#else
	struct mm_context *const task_context = sink->context;
#endif

	// Start processing the event locally.
	mm_event_listener_handle_input(task_context, sink, flags);
}

void NONNULL(1, 2)
mm_event_listener_output(struct mm_event_listener *const listener, struct mm_event_fd *const sink, const uint32_t flags)
{
	// Update event statistics.
	listener->events++;

#if ENABLE_SMP
	// Unbind or rebind the sink if appropriate.
	mm_event_test_binding(listener, sink);

	// Count the received event.
	mm_event_received(sink);

	// Submit the event to a peer context if needed.
	struct mm_context *const task_context = sink->context;
	if (task_context != listener->context) {
		mm_async_call_2(task_context, mm_event_listener_output_req, (uintptr_t) sink, flags);
#if ENABLE_EVENT_STATS
		listener->stats.forwarded_events++;
#endif
		return;
	}
#else
	struct mm_context *const task_context = sink->context;
#endif

	// Start processing the event locally.
	mm_event_listener_handle_output(task_context, sink, flags);
}

void NONNULL(1, 2)
mm_event_listener_unregister(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

#if ENABLE_SMP
	// Count the received event.
	mm_event_received(sink);
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

void NONNULL(1, 2)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch)
{
	ENTER();

	// Set the pointers among associated entities.
	listener->context = NULL;
	listener->dispatch = dispatch;

	// Prepare the timer queue.
	mm_timeq_prepare(&listener->timer_queue, &mm_memory_fixed_xarena);

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	kern_return_t r = semaphore_create(mach_task_self(), &listener->semaphore, SYNC_POLICY_FIFO, 0);
	if (r != KERN_SUCCESS)
		mm_fatal(0, "semaphore_create");
#else
	mm_thread_monitor_prepare(&listener->monitor);
#endif

	// Initialize event sink reclamation data.
	mm_event_epoch_prepare_local(&listener->epoch);

	// Initialize the statistic counters.
	mm_event_listener_clear_events(listener);
	listener->notifications = 0;
#if ENABLE_EVENT_STATS
	listener->stats.poll_calls = 0;
	listener->stats.zero_poll_calls = 0;
	listener->stats.wait_calls = 0;
	listener->stats.events = 0;
	listener->stats.forwarded_events = 0;
	listener->stats.repeatedly_forwarded_events = 0;
#endif

	// Initialize the local part of event backend.
	mm_event_backend_local_prepare(&listener->backend, &dispatch->backend);

	LEAVE();
}

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener)
{
	ENTER();

	// Clean up the local part of event backend.
	mm_event_backend_local_cleanup(&listener->backend);

	// Destroy the timer queue.
	mm_timeq_cleanup(&listener->timer_queue);

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_thread_monitor_cleanup(&listener->monitor);
#endif

	LEAVE();
}
