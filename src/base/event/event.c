/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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

#include "base/logger.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"
#include "base/fiber/strand.h"

/**********************************************************************
 * Asynchronous procedure call construction.
 **********************************************************************/

// The size of ring data for a given number of post arguments.
#define MM_SEND_ARGC(c)		((c) + 1)
// Define ring data for a post request together with its arguments.
#define MM_SEND_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) __VA_ARGS__ }

// Send a request to a cross-thread request ring.
#define MM_SEND(n, ring, stat, notify, target, ...)			\
	do {								\
		mm_stamp_t s;						\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		mm_ring_mpmc_enqueue_sn(ring, &s, v, MM_SEND_ARGC(n));	\
		mm_event_notify(target, s);				\
		stat();							\
	} while (0)

// Try to send a request to a cross-thread request ring.
#define MM_TRYSEND(n, ring, stat, notify, target, ...)			\
	do {								\
		bool rc;						\
		mm_stamp_t s;						\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		rc = mm_ring_mpmc_put_sn(ring, &s, v, MM_SEND_ARGC(n));	\
		if (rc) {						\
			mm_event_notify(target, s);			\
			stat();						\
		}							\
		return rc;						\
	} while (0)

// Make a direct call instead of async one
#define MM_DIRECTCALL_0(r)						\
	do {								\
		struct mm_strand *strand = mm_strand_selfptr();		\
		struct mm_event_listener *listener = strand->listener;	\
		MM_SEND_ARGV(v, 0);					\
		r(listener, v);						\
		mm_event_direct_call_stat(listener);			\
	} while (0)
#define MM_DIRECTCALL(r, ...)						\
	do {								\
		struct mm_strand *strand = mm_strand_selfptr();		\
		struct mm_event_listener *listener = strand->listener;	\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		r(listener, v);						\
		mm_event_direct_call_stat(listener);			\
	} while (0)

static inline void
mm_event_call_stat(void)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	struct mm_strand *strand = mm_strand_selfptr();
	if (likely(strand != NULL))
		strand->listener->stats.enqueued_async_calls++;
#endif
}

static inline void
mm_event_post_stat(void)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	struct mm_strand *strand = mm_strand_selfptr();
	if (likely(strand != NULL))
		strand->listener->stats.enqueued_async_posts++;
#endif
}

static inline void
mm_event_direct_call_stat(struct mm_event_listener *listener UNUSED)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.direct_calls++;
#endif
}

#if ENABLE_SMP
static struct mm_event_listener *
mm_event_find_listener(struct mm_event_dispatch *dispatch)
{
	mm_thread_t n = dispatch->nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &dispatch->listeners[i];
		uintptr_t state = mm_memory_load(listener->state);
		if (state != MM_EVENT_LISTENER_RUNNING)
			return listener;
	}
	return NULL;
}
#endif

/**********************************************************************
 * Event sink activity.
 **********************************************************************/

void NONNULL(1)
mm_event_handle_input(struct mm_event_fd *sink, uint32_t flags)
{
	ENTER();
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Update the read readiness flags.
	sink->flags |= flags;
	sink->flags &= ~MM_EVENT_INPUT_TRIGGER;
#if ENABLE_SMP
	// Count the delivered event.
	sink->dispatch_stamp++;
#endif

	if (sink->input_fiber != NULL) {
		// Run the fiber blocked on input.
		mm_fiber_run(sink->input_fiber);
	} else if ((sink->flags & MM_EVENT_INPUT_STARTED) == 0) {
		// Start a new input work.
		sink->flags |= MM_EVENT_INPUT_STARTED;
		mm_event_add_task(sink->listener, &sink->io->input, (mm_value_t) sink);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_handle_output(struct mm_event_fd *sink, uint32_t flags)
{
	ENTER();
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Update the write readiness flags.
	sink->flags |= flags;
	sink->flags &= ~MM_EVENT_OUTPUT_TRIGGER;
#if ENABLE_SMP
	// Count the delivered event.
	sink->dispatch_stamp++;
#endif

	if (sink->output_fiber != NULL) {
		// Run the fiber blocked on output.
		mm_fiber_run(sink->output_fiber);
	} else if ((sink->flags & MM_EVENT_OUTPUT_STARTED) == 0) {
		// Start a new output work.
		sink->flags |= MM_EVENT_OUTPUT_STARTED;
		mm_event_add_task(sink->listener, &sink->io->output, (mm_value_t) sink);
	}

	LEAVE();
}

/**********************************************************************
 * Event sink I/O tasks.
 **********************************************************************/

static void NONNULL(1)
mm_event_complete(struct mm_event_fd *sink)
{
	ENTER();

	const uint32_t flags = sink->flags;
	if ((flags & (MM_EVENT_INPUT_STARTED | MM_EVENT_OUTPUT_STARTED)) != 0) {
		/* Do nothing. */
	} else if ((flags & (MM_EVENT_INPUT_ERROR | MM_EVENT_OUTPUT_ERROR)) != 0) {
		/* Close the sink on error. */
		if ((flags & (MM_EVENT_CLOSED | MM_EVENT_BROKEN)) == 0)
			mm_event_close_fd(sink);
	} else {
		/* Mark the sink as having completed the processing of all
		   the events delivered to the target thread so far. */
#if ENABLE_SMP
		/* TODO: release memory fence */
		mm_memory_store(sink->complete_stamp, sink->dispatch_stamp);
#endif
	}

	LEAVE();
}

static mm_value_t
mm_event_unexpected_input(mm_value_t arg)
{
	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	mm_error(0, "unexpected input handler on fd %d", sink->fd);

	sink->flags &= ~MM_EVENT_INPUT_STARTED;
	mm_event_complete(sink);

	return 0;
}

static mm_value_t
mm_event_unexpected_output(mm_value_t arg)
{
	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->listener->strand == mm_strand_selfptr());

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
	ASSERT(sink->listener->strand == mm_strand_selfptr());
	ASSERT((sink->flags & MM_EVENT_INPUT_STARTED) != 0);

	// Check to see if another input work should be started.
	if ((sink->flags & (MM_EVENT_INPUT_READY | MM_EVENT_INPUT_ERROR)) != 0
	    && (sink->flags & MM_EVENT_REGULAR_INPUT) != 0
	    && !mm_event_input_closed(sink)) {
		// Submit an input task for execution again.
		mm_event_add_task(sink->listener, &sink->io->input, arg);
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
	ASSERT(sink->listener->strand == mm_strand_selfptr());
	ASSERT((sink->flags & MM_EVENT_OUTPUT_STARTED) != 0);

	// Check to see if another output work should be started.
	if ((sink->flags & (MM_EVENT_OUTPUT_READY | MM_EVENT_OUTPUT_ERROR)) != 0
	    && (sink->flags & MM_EVENT_REGULAR_OUTPUT) != 0
	    && !mm_event_output_closed(sink)) {
		// Submit an output task for execution again.
		mm_event_add_task(sink->listener, &sink->io->output, arg);
	} else {
		// Done with output for now.
		sink->flags &= ~MM_EVENT_OUTPUT_STARTED;
		mm_event_complete(sink);
	}

	LEAVE();
}

static bool
mm_event_reassign_io(mm_value_t arg, struct mm_event_listener *listener)
{
	ENTER();
	bool reassigned = false;

	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->listener->strand == mm_strand_selfptr());
	bool input_started = (sink->flags & MM_EVENT_INPUT_STARTED) != 0;
	bool output_started = (sink->flags & MM_EVENT_OUTPUT_STARTED) != 0;
	if (input_started != output_started) {
		sink->listener = listener;
		reassigned = true;
	}

	LEAVE();
	return reassigned;
}

void NONNULL(1)
mm_event_prepare_io(struct mm_event_io *io, mm_event_execute_t input, mm_event_execute_t output)
{
	if (input == NULL)
		input = mm_event_unexpected_input;
	if (output == NULL)
		output = mm_event_unexpected_output;

	mm_event_task_prepare(&io->input, input, mm_event_input_complete, mm_event_reassign_io);
	mm_event_task_prepare(&io->output, output, mm_event_output_complete, mm_event_reassign_io);
}

struct mm_event_io *
mm_event_instant_io(void)
{
	static struct mm_event_io instant_io = {
		.input = {
			.execute = mm_event_unexpected_input,
			.complete = mm_event_complete_noop,
			.reassign = mm_event_reassign_off
		},
		.output = {
			.execute = mm_event_unexpected_output,
			.complete = mm_event_complete_noop,
			.reassign = mm_event_reassign_off
		}
	};
	return &instant_io;
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, struct mm_event_io *io,
		    mm_event_mode_t input, mm_event_mode_t output, uint32_t flags)
{
	ENTER();
	DEBUG("fd %d", fd);
	ASSERT(fd >= 0);

	sink->fd = fd;
	sink->io = io;
	sink->listener = NULL;
	sink->input_fiber = NULL;
	sink->output_fiber = NULL;

#if ENABLE_SMP
	sink->receive_stamp = 0;
	sink->dispatch_stamp = 0;
	sink->complete_stamp = 0;
#endif

	if (input == MM_EVENT_REGULAR)
		flags |= MM_EVENT_REGULAR_INPUT;
	else if (input == MM_EVENT_ONESHOT)
		flags |= MM_EVENT_ONESHOT_INPUT;
	if (output == MM_EVENT_REGULAR)
		flags |= MM_EVENT_REGULAR_OUTPUT;
	else if (output == MM_EVENT_ONESHOT)
		flags |= MM_EVENT_ONESHOT_OUTPUT;
	sink->flags = flags;

	LEAVE();
}

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);

	// Bind the sink to this thread's event listener.
	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_event_listener *listener = strand->listener;
	if (sink->listener != NULL) {
		VERIFY(sink->listener == listener);
	} else if ((sink->flags & MM_EVENT_NOTIFY_FD) == 0) {
		sink->listener = listener;
	}

	// Register with the event backend.
	mm_event_backend_register_fd(&listener->dispatch->backend, &listener->storage, sink);
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
	struct mm_event_listener *listener = sink->listener;
	ASSERT(listener->strand == mm_strand_selfptr());
	mm_event_backend_unregister_fd(&listener->dispatch->backend, &listener->storage, sink);

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
	struct mm_event_listener *listener = sink->listener;
	ASSERT(listener->strand == mm_strand_selfptr());
	mm_event_backend_unregister_fd(&listener->dispatch->backend, &listener->storage, sink);
	mm_event_backend_flush(&listener->dispatch->backend, &listener->storage);

	LEAVE();
}

void NONNULL(1)
mm_event_trigger_input(struct mm_event_fd *sink)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT(!mm_event_input_closed(sink));

	mm_event_reset_input_ready(sink);

	if ((sink->flags & (MM_EVENT_ONESHOT_INPUT | MM_EVENT_INPUT_TRIGGER)) == MM_EVENT_ONESHOT_INPUT) {
		sink->flags |= MM_EVENT_INPUT_TRIGGER;

		struct mm_event_listener *listener = sink->listener;
		ASSERT(listener->strand == mm_strand_selfptr());

		mm_event_backend_trigger_input(&listener->dispatch->backend, &listener->storage, sink);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_trigger_output(struct mm_event_fd *sink)
{
	ENTER();
	DEBUG("fd %d, status %d", sink->fd, sink->flags);
	ASSERT(!mm_event_output_closed(sink));

	mm_event_reset_output_ready(sink);

	if ((sink->flags & (MM_EVENT_ONESHOT_OUTPUT | MM_EVENT_OUTPUT_TRIGGER)) == MM_EVENT_ONESHOT_OUTPUT) {
		sink->flags |= MM_EVENT_OUTPUT_TRIGGER;

		struct mm_event_listener *listener = sink->listener;
		ASSERT(listener->strand == mm_strand_selfptr());

		mm_event_backend_trigger_output(&listener->dispatch->backend, &listener->storage, sink);
	}

	LEAVE();
}

/**********************************************************************
 * Event sink fiber control.
 **********************************************************************/

void NONNULL(1)
mm_event_start_input_work(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Submit an input work for execution if possible.
	if (!mm_event_input_closed(sink) && (sink->flags & MM_EVENT_INPUT_STARTED) == 0) {
		sink->flags |= MM_EVENT_INPUT_STARTED;
		mm_event_add_task(sink->listener, &sink->io->input, (mm_value_t) sink);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_start_output_work(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Submit an output work for execution if possible.
	if (!mm_event_output_closed(sink) && (sink->flags & MM_EVENT_OUTPUT_STARTED) == 0) {
		sink->flags |= MM_EVENT_OUTPUT_STARTED;
		mm_event_add_task(sink->listener, &sink->io->output, (mm_value_t) sink);
	}

	LEAVE();
}

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

#if ENABLE_SMP

static void NONNULL(1)
mm_event_wait(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(timeout != 0);

#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.wait_calls++;
#endif

	// Try to reclaim some pending event sinks before sleeping.
	if (mm_event_epoch_active(&listener->epoch))
		mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);

	// Publish the log before a possible sleep.
	mm_log_relay();

	// Wait for a wake-up notification or timeout.
	mm_event_listener_timedwait(listener, timeout);

	LEAVE();
}

#endif

static void NONNULL(1)
mm_event_poll(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch, mm_timeout_t timeout)
{
	ENTER();

#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.poll_calls++;
	listener->stats.zero_poll_calls += (timeout == 0);
#endif

	if (timeout) {
		// Cleanup stale event notifications.
		mm_event_backend_notify_clean(&dispatch->backend);

		// Publish the log before a possible sleep.
		mm_log_relay();
	}

	// Start a reclamation critical section.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Check incoming events and wait for notification/timeout.
	mm_event_backend_poll(&dispatch->backend, &listener->storage, timeout);

	// End a reclamation critical section.
	mm_event_epoch_leave(&listener->epoch, &dispatch->global_epoch);

	LEAVE();
}

static void NONNULL(1)
mm_event_distribute_tasks(struct mm_event_dispatch *const dispatch, struct mm_event_listener *const listener)
{
	ENTER();

	size_t ntasks = mm_event_task_list_size(&listener->tasks);
	if (ntasks > (2 * MM_EVENT_TASK_SEND_MAX)) {
		const uint32_t nlisteners = dispatch->nlisteners;
		const uint32_t self_index = listener - dispatch->listeners;
		const uint32_t limit = (ntasks + nlisteners - 1) / nlisteners;

		for (uint32_t index = 0; index < nlisteners; index++) {
			if (index == self_index)
				continue;

			struct mm_event_listener *peer = &dispatch->listeners[index];
			uint64_t n = mm_event_task_peer_list_size(&peer->tasks);
			n += mm_ring_mpmc_size(peer->async_queue) * MM_EVENT_TASK_SEND_MAX;
			while (n < limit && mm_event_task_list_reassign(&listener->tasks, peer)) {
				n += MM_EVENT_TASK_SEND_MAX;
			}
		}
	}

	LEAVE();
}

void NONNULL(1)
mm_event_listen(struct mm_event_listener *const listener, mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_dispatch *const dispatch = listener->dispatch;
	if (listener->spin_count) {
		// If previously received some events then speculate that some
		// more are coming so keep spinning for a while to avoid extra
		// context switches.
		listener->spin_count--;
		timeout = 0;
	} else if (mm_event_backend_has_urgent_changes(&listener->storage)) {
		// There are event poll changes that need to be immediately
		// acknowledged.
		timeout = 0;
	}

#if ENABLE_SMP
	// The first arrived thread is elected to conduct the next event poll.
	bool is_poller_thread = mm_regular_trylock(&dispatch->poller_lock);
	if (is_poller_thread) {
		if (dispatch->poll_spin_count) {
			dispatch->poll_spin_count--;
			timeout = 0;
		}

		// Wait for incoming events or timeout expiration.
		mm_event_poll(listener, dispatch, timeout);

		// Reset the poller spin counter.
		if (mm_event_listener_got_events(listener)) {
			mm_event_listener_clear_events(listener);
			listener->spin_count = dispatch->lock_spin_limit;
			dispatch->poll_spin_count = dispatch->poll_spin_limit;
		}

		// Give up the poller thread role.
		mm_regular_unlock(&dispatch->poller_lock);

		// Share event tasks with other listeners if feasible.
		mm_event_distribute_tasks(dispatch, listener);
	} else {
		// Flush event poll changes if any.
		if (mm_event_backend_has_changes(&listener->storage)) {
			mm_event_backend_flush(&dispatch->backend, &listener->storage);
		}

		// Wait for forwarded events or timeout expiration.
		if (timeout) {
			mm_event_wait(listener, dispatch, timeout);
#if ENABLE_EVENT_STATS
		} else {
			// Update statistics.
			listener->stats.spin_count++;
#endif
		}
	}
#else // !ENABLE_SMP
	// Wait for incoming events or timeout expiration.
	mm_event_poll(listener, dispatch, timeout);

	// Reset the poller spin counter and event counters.
	if (mm_event_listener_got_events(listener)) {
		mm_event_listener_clear_events(listener);
		listener->spin_count = dispatch->poll_spin_limit;
	}
#endif // !ENABLE_SMP

	LEAVE();
}

void NONNULL(1)
mm_event_notify(struct mm_event_listener *listener, mm_stamp_t stamp)
{
	ENTER();

	uintptr_t state = mm_memory_load(listener->state);
	if ((((uintptr_t) stamp) << 2) == (state & ~MM_EVENT_LISTENER_STATUS)) {
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

/**********************************************************************
 * Asynchronous procedure call execution.
 **********************************************************************/

struct mm_event_async
{
	union
	{
		uintptr_t data[MM_EVENT_ASYNC_MAX + 1];
		struct
		{
			mm_event_async_routine_t routine;
			uintptr_t arguments[MM_EVENT_ASYNC_MAX];
		};
	};
};

static inline void
mm_event_async_execute(struct mm_event_listener *listener, struct mm_event_async *post)
{
	(*post->routine)(listener, post->arguments);
}

static inline bool NONNULL(1, 2)
mm_event_receive_call(struct mm_event_listener *listener, struct mm_event_async *post)
{
	return mm_ring_mpsc_get_n(listener->async_queue, post->data, (MM_EVENT_ASYNC_MAX + 1));
}

void NONNULL(1)
mm_event_handle_calls(struct mm_event_listener *listener)
{
	ENTER();

	// Execute requests.
	struct mm_event_async async;
	if (mm_event_receive_call(listener, &async)) {
		// Enter the state that forbids a recursive fiber switch.
		struct mm_strand *strand = listener->strand;
		mm_strand_state_t state = strand->state;
		strand->state = MM_STRAND_CSWITCH;

		do {
			mm_event_async_execute(listener, &async);
#if ENABLE_EVENT_STATS
			listener->stats.dequeued_async_calls++;
#endif
		} while (mm_event_receive_call(listener, &async));

		// Restore normal running state.
		strand->state = state;
	}

	LEAVE();
}

/**********************************************************************
 * Asynchronous procedure calls targeting a single listener.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_call_0(struct mm_event_listener *listener, mm_event_async_routine_t r)
{
	MM_SEND(0, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r);
}

bool NONNULL(1, 2)
mm_event_trycall_0(struct mm_event_listener *listener, mm_event_async_routine_t r)
{
	MM_TRYSEND(0, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r);
}

void NONNULL(1, 2)
mm_event_call_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1)
{
	MM_SEND(1, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1);
}

bool NONNULL(1, 2)
mm_event_trycall_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1)
{
	MM_TRYSEND(1, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1);
}

void NONNULL(1, 2)
mm_event_call_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2)
{
	MM_SEND(2, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2);
}

bool NONNULL(1, 2)
mm_event_trycall_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2)
{
	MM_TRYSEND(2, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2);
}

void NONNULL(1, 2)
mm_event_call_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_SEND(3, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3);
}

bool NONNULL(1, 2)
mm_event_trycall_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYSEND(3, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3);
}

void NONNULL(1, 2)
mm_event_call_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_SEND(4, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4);
}

bool NONNULL(1, 2)
mm_event_trycall_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYSEND(4, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4);
}

void NONNULL(1, 2)
mm_event_call_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_SEND(5, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4, a5);
}

bool NONNULL(1, 2)
mm_event_trycall_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYSEND(5, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4, a5);
}

void NONNULL(1, 2)
mm_event_call_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_SEND(6, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4, a5, a6);
}

bool NONNULL(1, 2)
mm_event_trycall_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYSEND(6, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4, a5, a6);
}

/**********************************************************************
 * Asynchronous procedure calls targeting any listener of a dispatcher.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_post_0(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL_0(r);
		return;
	}
	MM_SEND(0, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r);
}

void NONNULL(1, 2)
mm_event_post_1(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1);
		return;
	}
	MM_SEND(1, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1);
}

void NONNULL(1, 2)
mm_event_post_2(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1, a2);
		return;
	}
	MM_SEND(2, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1, a2);
}

void NONNULL(1, 2)
mm_event_post_3(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3);
		return;
	}
	MM_SEND(3, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1, a2, a3);
}

void NONNULL(1, 2)
mm_event_post_4(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4);
		return;
	}
	MM_SEND(4, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1, a2, a3, a4);
}

void NONNULL(1, 2)
mm_event_post_5(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4, a5);
		return;
	}
	MM_SEND(5, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1, a2, a3, a4, a5);
}

void NONNULL(1, 2)
mm_event_post_6(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	struct mm_event_listener *listener = mm_event_find_listener(dispatch);
	if (listener == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4, a5, a6);
		return;
	}
	MM_SEND(6, listener->async_queue, mm_event_post_stat, mm_event_post_notify, listener,
		r, a1, a2, a3, a4, a5, a6);
}

/**********************************************************************
 * Asynchronous task scheduling.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_add_task(struct mm_event_listener *listener, mm_event_task_t task, mm_value_t arg)
{
	ENTER();
	ASSERT(listener->strand == mm_strand_selfptr());

	mm_event_task_list_add(&listener->tasks, task, arg);

	LEAVE();
}

#if ENABLE_SMP

static void
mm_event_add_task_req(struct mm_event_listener *listener, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_task *task = (struct mm_event_task *) arguments[0];
	mm_value_t arg = arguments[1];

	mm_event_add_task(listener, task, arg);

	LEAVE();
}

#endif

void NONNULL(1, 2)
mm_event_send_task(struct mm_event_listener *listener, mm_event_task_t task, mm_value_t arg)
{
	ENTER();

#if ENABLE_SMP
	if (listener->strand == mm_strand_selfptr()) {
		// Enqueue it directly if on the same strand.
		mm_event_add_task(listener, task, arg);
	} else {
		// Submit the work item to the thread request queue.
		mm_event_call_2(listener, mm_event_add_task_req, (uintptr_t) task, arg);
	}
#else
	mm_event_add_task(listener, task, arg);
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_post_task(mm_event_task_t task, mm_value_t arg)
{
	ENTER();

	struct mm_strand *strand = mm_strand_selfptr();
#if ENABLE_SMP
	// Dispatch the task.
	mm_event_post_2(strand->listener->dispatch, mm_event_add_task_req, (mm_value_t) task, arg);
#else
	mm_event_add_task(strand->listener, task, arg);
#endif

	LEAVE();
}
