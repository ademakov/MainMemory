/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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

#include "base/event/event.h"

#include "base/context.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"

/**********************************************************************
 * Event sink I/O tasks.
 **********************************************************************/

static void NONNULL(1)
mm_event_complete(struct mm_event_fd *sink)
{
	ENTER();

	/* Close the sink on a error unless there is still some active task using it. */
	const uint32_t flags = sink->flags;
	if ((flags & (MM_EVENT_CLOSED | MM_EVENT_INPUT_STARTED | MM_EVENT_OUTPUT_STARTED)) == 0) {
		if ((flags & (MM_EVENT_INPUT_ERROR | MM_EVENT_OUTPUT_ERROR)) != 0) {
			mm_event_close_fd(sink);
		} else if (sink->regular_listener) {
			sink->context = sink->regular_listener->context;
		}
	}

	LEAVE();
}

static mm_value_t
mm_event_unexpected_input(mm_value_t arg)
{
	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->context == mm_context_selfptr());

	mm_error(0, "unexpected input handler on fd %d", sink->fd);

	sink->flags &= ~MM_EVENT_INPUT_STARTED;
	mm_event_complete(sink);

	return 0;
}

static mm_value_t
mm_event_unexpected_output(mm_value_t arg)
{
	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->context == mm_context_selfptr());

	mm_error(0, "unexpected output handler on fd %d", sink->fd);

	sink->flags &= ~MM_EVENT_OUTPUT_STARTED;
	mm_event_complete(sink);

	return 0;
}

static void
mm_event_input_complete(mm_value_t arg, mm_value_t result UNUSED)
{
	ENTER();

	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->context == mm_context_selfptr());
	ASSERT((sink->flags & MM_EVENT_INPUT_STARTED) != 0);

	// Check to see if another input work should be started.
	if (mm_event_input_in_progress(sink) && !mm_event_input_closed(sink)) {
		// Submit an input task for execution again.
		sink->flags &= ~MM_EVENT_INPUT_RESTART;
		mm_context_add_task(sink->context, &sink->tasks->input, arg);
	} else {
		// Done with input for now.
		sink->flags &= ~MM_EVENT_INPUT_STARTED;
		mm_event_complete(sink);
	}

	LEAVE();
}

static void
mm_event_output_complete(mm_value_t arg, mm_value_t result UNUSED)
{
	ENTER();

	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->context == mm_context_selfptr());
	ASSERT((sink->flags & MM_EVENT_OUTPUT_STARTED) != 0);

	// Check to see if another output work should be started.
	if (mm_event_output_in_progress(sink) && !mm_event_output_closed(sink)) {
		// Submit an output task for execution again.
		sink->flags &= ~MM_EVENT_OUTPUT_RESTART;
		mm_context_add_task(sink->context, &sink->tasks->output, arg);
	} else {
		// Done with output for now.
		sink->flags &= ~MM_EVENT_OUTPUT_STARTED;
		mm_event_complete(sink);
	}

	LEAVE();
}

static bool
mm_event_reassign_io(mm_value_t arg, struct mm_context *context)
{
	ENTER();
	bool reassigned = false;

	struct mm_event_fd *const sink = (struct mm_event_fd *) arg;
	const uint32_t flags = sink->flags;

	// Check to see if the task may be reassigned:
	// 1. it cannot be reassigned for a fixed sink by definition;
	// 2. a one-shot-enabled sink stays with the context that originally enabled it to keep
	//    matters simple for epoll code that re-arms file descriptors after one-shot events;
	// 3. the task in question (whatever it is -- input or output) can only be reassigned if
	//    this is the only active task associated with the sink otherwise we could end up
	//    with two tasks for the same sink running on two threads at once.
	if ((flags & (MM_EVENT_FIXED_POLLER | MM_EVENT_ONESHOT_INPUT | MM_EVENT_ONESHOT_OUTPUT)) == 0
	    && ((flags & MM_EVENT_INPUT_STARTED) == 0 || (flags & MM_EVENT_OUTPUT_STARTED) == 0)) {
		sink->context = context;
		reassigned = true;
	}

	LEAVE();
	return reassigned;
}

void NONNULL(1)
mm_event_prepare_io(struct mm_event_io *io, mm_task_execute_t input, mm_task_execute_t output)
{
	if (input == NULL)
		input = mm_event_unexpected_input;
	if (output == NULL)
		output = mm_event_unexpected_output;

	mm_task_prepare(&io->input, input, mm_event_input_complete, mm_event_reassign_io);
	mm_task_prepare(&io->output, output, mm_event_output_complete, mm_event_reassign_io);
}

struct mm_event_io *
mm_event_instant_io(void)
{
	static struct mm_event_io instant_io = {
		.input = {
			.execute = mm_event_unexpected_input,
			.complete = mm_task_complete_noop,
			.reassign = mm_task_reassign_off
		},
		.output = {
			.execute = mm_event_unexpected_output,
			.complete = mm_task_complete_noop,
			.reassign = mm_task_reassign_off
		}
	};
	return &instant_io;
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 4, 5)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, uint32_t flags, const struct mm_event_io *tasks, void (*destroy)(struct mm_event_fd *))
{
	ENTER();
	DEBUG("fd %d, status %d", fd, flags);
	VERIFY((flags & MM_EVENT_ONESHOT_INPUT) == 0);
	VERIFY((flags & MM_EVENT_ONESHOT_OUTPUT) == 0);
	VERIFY((flags & MM_EVENT_REGULAR_INPUT) == 0 || (flags & MM_EVENT_REGULAR_OUTPUT) == 0);

	sink->fd = fd;
	sink->flags = flags;
	sink->tasks = tasks;
	sink->context = NULL;
	sink->regular_listener = NULL;
	sink->input_fiber = NULL;
	sink->output_fiber = NULL;
	sink->destroy = destroy;

	LEAVE();
}

void NONNULL(1, 2)
mm_event_register_fd(struct mm_event_fd *sink, struct mm_context *const context)
{
	ENTER();
	ASSERT(sink->fd >= 0);
	ASSERT(context == mm_context_selfptr());

	// Bind the sink to this thread's event listener.
	sink->context = context;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_REGULAR_OUTPUT)) != 0 && (sink->flags & MM_EVENT_COMMON_POLLER) == 0)
		sink->regular_listener = context->listener;

	// Register with the event backend.
	struct mm_event_listener *const listener = context->listener;
	mm_event_backend_register_fd(&listener->dispatch->backend, &listener->backend, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_trigger_input(struct mm_event_fd *sink, struct mm_context *const context)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT(!mm_event_input_closed(sink));
	ASSERT(context == sink->context);
	ASSERT(context == mm_context_selfptr());

	mm_event_reset_input_ready(sink);

	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_ONESHOT_INPUT)) == 0) {
		sink->flags |= MM_EVENT_ONESHOT_INPUT;

		struct mm_event_listener *const listener = context->listener;
		mm_event_backend_enable_input(&listener->dispatch->backend, &listener->backend, sink);
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_trigger_output(struct mm_event_fd *sink, struct mm_context *const context)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT(!mm_event_output_closed(sink));
	ASSERT(context == sink->context);
	ASSERT(context == mm_context_selfptr());

	mm_event_reset_output_ready(sink);

	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_OUTPUT)) == 0) {
		sink->flags |= MM_EVENT_ONESHOT_OUTPUT;

		struct mm_event_listener *const listener = context->listener;
		mm_event_backend_enable_output(&listener->dispatch->backend, &listener->backend, sink);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_close_fd(struct mm_event_fd *sink)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT((sink->flags & (MM_EVENT_CLOSED | MM_EVENT_BROKEN)) == 0);

	// Mark the sink as closed.
	mm_event_set_closed(sink);

	// Unregister it.
	struct mm_event_listener *const listener = sink->context->listener;
	mm_event_backend_unregister_fd(&listener->dispatch->backend, &listener->backend, sink);

	LEAVE();
}

void NONNULL(1)
mm_event_close_broken_fd(struct mm_event_fd *sink)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT((sink->flags & (MM_EVENT_CLOSED | MM_EVENT_BROKEN)) == 0);

	// Mark the sink as closed.
	mm_event_set_broken(sink);

	// Unregister it immediately.
	struct mm_event_listener *const listener = sink->context->listener;
	mm_event_backend_unregister_fd(&listener->dispatch->backend, &listener->backend, sink);
	mm_event_backend_flush(&listener->dispatch->backend, &listener->backend);

	LEAVE();
}

/**********************************************************************
 * Event sink fiber control.
 **********************************************************************/

void NONNULL(1)
mm_event_submit_input(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->context == mm_context_selfptr());

	// Ask an input task to run.
	if (!mm_event_input_closed(sink)) {
		if ((sink->flags & MM_EVENT_INPUT_STARTED) != 0) {
			sink->flags |= MM_EVENT_INPUT_RESTART;
		} else {
			sink->flags |= MM_EVENT_INPUT_STARTED;
			mm_context_add_task(sink->context, &sink->tasks->input, (mm_value_t) sink);
		}
	}

	LEAVE();
}

void NONNULL(1)
mm_event_submit_output(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->context == mm_context_selfptr());

	// Ask an output task to run.
	if (!mm_event_output_closed(sink)) {
		if ((sink->flags & MM_EVENT_OUTPUT_STARTED) != 0) {
			sink->flags |= MM_EVENT_OUTPUT_RESTART;
		} else {
			sink->flags |= MM_EVENT_OUTPUT_STARTED;
			mm_context_add_task(sink->context, &sink->tasks->output, (mm_value_t) sink);
		}
	}

	LEAVE();
}

/**********************************************************************
 * Timer event sink control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_prepare_task_timer(struct mm_event_timer *sink, const struct mm_task *task)
{
	mm_timeq_entry_prepare(&sink->entry, 0);
	sink->fiber = NULL;
	sink->task = task;
}

void NONNULL(1, 2)
mm_event_prepare_fiber_timer(struct mm_event_timer *sink, struct mm_fiber *fiber)
{
	mm_timeq_entry_prepare(&sink->entry, 0);
	sink->fiber = fiber;
	sink->task = NULL;
}

void NONNULL(1, 2)
mm_event_arm_timer(struct mm_context *context, struct mm_event_timer *sink, mm_timeout_t timeout)
{
	ENTER();

	if (mm_event_timer_armed(sink))
		mm_timeq_delete(&context->listener->timer_queue, &sink->entry);

	mm_timeval_t time = mm_context_gettime(context) + timeout;
	mm_timeq_entry_settime(&sink->entry, time);
	mm_timeq_insert(&context->listener->timer_queue, &sink->entry);

	DEBUG("armed timer: %lld", (long long) time);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_disarm_timer(struct mm_context *context, struct mm_event_timer *sink)
{
	ENTER();

	if (mm_event_timer_armed(sink))
		mm_timeq_delete(&context->listener->timer_queue, &sink->entry);

	LEAVE();
}

static mm_timeout_t
mm_event_check_timer(struct mm_context *const context, struct mm_timeq_entry *timer, mm_timeout_t timeout)
{
	mm_timeval_t timer_time = timer->value;
	mm_timeval_t clock_time = mm_context_gettime(context);
	if (timer_time <= clock_time) {
		timeout = 0;
	} else {
		mm_timeval_t timer_timeout = timer_time - clock_time;
		if (timeout > timer_timeout) {
			timeout = timer_timeout;
		}
	}
	return timeout;
}

static void
mm_event_fire_timers(struct mm_context *const context, struct mm_timeq_entry *timer)
{
	mm_timeval_t clock_time = mm_context_gettime(context);
	while (timer != NULL && timer->value <= clock_time) {
		// Remove the timer from the queue.
		mm_timeq_delete(&context->listener->timer_queue, timer);

		// Execute the timer action.
		struct mm_event_timer *sink = containerof(timer, struct mm_event_timer, entry);
		if (sink->fiber != NULL)
			mm_fiber_run(sink->fiber);
		else
			mm_context_add_task(context, sink->task, (mm_value_t) sink);

		// Get the next timer.
		timer = mm_timeq_getmin(&context->listener->timer_queue);
	}
}

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

bool NONNULL(1)
mm_event_poll(struct mm_context *const context, mm_timeout_t timeout)
{
	ENTER();
	bool rc = false;

	struct mm_event_listener *const listener = context->listener;
	struct mm_event_dispatch *const dispatch = listener->dispatch;
	struct mm_timeq_entry *const timer = mm_timeq_getmin(&listener->timer_queue);

	if (timeout) {
		if (mm_event_backend_has_urgent_changes(&listener->backend)) {
			// There are event poll changes that need to be immediately
			// acknowledged.
			timeout = 0;
		} else if (timer != NULL) {
			timeout = mm_event_check_timer(context, timer, timeout);
		}
	}

	if (timeout) {
		// Cleanup stale event notifications.
		mm_event_backend_notify_clean(&dispatch->backend, &listener->backend);

		// Publish the log before a possible sleep.
		mm_log_relay();

		// Indicate that clocks need to be updated.
		mm_timepiece_reset(&context->clock);
	}

	// Start a reclamation critical section.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Wait for incoming events or timeout expiration.
	mm_event_backend_poll(&dispatch->backend, &listener->backend, timeout);

	// End a reclamation critical section.
	mm_event_epoch_leave(&listener->epoch, &dispatch->global_epoch);

#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.poll_calls++;
	listener->stats.zero_poll_calls += (timeout == 0);
#endif

	// Execute the timers which time has come.
	if (timer != NULL)
		mm_event_fire_timers(context, timer);

	// Reset the poller event counter.
	if (mm_event_listener_got_events(listener)) {
		mm_event_listener_clear_events(listener);
		rc = true;
	}

	LEAVE();
	return rc;
}

void NONNULL(1)
mm_event_wait(struct mm_context *const context, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(timeout != 0);

	struct mm_event_listener *const listener = context->listener;
	struct mm_timeq_entry *const timer = mm_timeq_getmin(&listener->timer_queue);

	if (timeout && timer != NULL)
		timeout = mm_event_check_timer(context, timer, timeout);

	if (timeout) {
		// Publish the log before a possible sleep.
		mm_log_relay();

		// Indicate that clocks need to be updated.
		mm_timepiece_reset(&context->clock);

		// Wait for a wake-up notification or timeout.
		mm_event_listener_timedwait(listener, timeout);
	}

	// Try to reclaim some pending event sinks before sleeping.
	if (mm_event_epoch_active(&listener->epoch))
		mm_event_epoch_advance(&listener->epoch, &listener->dispatch->global_epoch);

#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.wait_calls++;
#endif

	// Execute the timers which time has come.
	if (timer != NULL)
		mm_event_fire_timers(context, timer);

	LEAVE();
}

void NONNULL(1)
mm_event_notify(struct mm_context *context, mm_stamp_t stamp)
{
	ENTER();

	uintptr_t status = mm_memory_load(context->status);
	if ((((uintptr_t) stamp) << 2) == (status & ~MM_CONTEXT_STATUS)) {
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
		status &= MM_CONTEXT_STATUS;

		struct mm_event_listener *listener = context->listener;
		if (status == MM_CONTEXT_POLLING)
			mm_event_backend_notify(&listener->dispatch->backend, &listener->backend);
		else if (status == MM_CONTEXT_WAITING)
			mm_event_listener_signal(listener);
	}

	LEAVE();
}
