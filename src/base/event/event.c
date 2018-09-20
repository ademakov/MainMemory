/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2018  Aleksey Demakov
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

/* The size of ring data for a given number of post arguments. */
#define MM_POST_ARGC(c)		((c) + 1)
/* Define ring data for a post request together with its arguments. */
#define MM_POST_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) __VA_ARGS__ }

/* Post a request to a cross-thread request ring. */
#define MM_POST(n, ring, stat, notify, target, ...)			\
	do {								\
		mm_stamp_t s;						\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		mm_ring_mpmc_enqueue_sn(ring, &s, v, MM_POST_ARGC(n));	\
		notify(target, s);					\
		stat();							\
	} while (0)

/* Try to post a request to a cross-thread request ring. */
#define MM_TRYPOST(n, ring, stat, notify, target, ...)			\
	do {								\
		bool rc;						\
		mm_stamp_t s;						\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		rc = mm_ring_mpmc_put_sn(ring, &s, v, MM_POST_ARGC(n));	\
		if (rc) {						\
			notify(target, s);				\
			stat();						\
		}							\
		return rc;						\
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
mm_event_post_notify(struct mm_event_dispatch *dispatch, mm_stamp_t stamp UNUSED)
{
	mm_event_wakeup_any(dispatch);
}

/**********************************************************************
 * Event sink activity.
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
		// Run the reader fiber presumably blocked on the socket.
		mm_fiber_run(sink->input_fiber);
	} else {
		// Check to see if a new reader should be spawned.
		flags = sink->flags & (MM_EVENT_INPUT_STARTED | MM_EVENT_INPUT_PENDING);
		if (flags == MM_EVENT_INPUT_PENDING) {
			if ((sink->flags & MM_EVENT_ONESHOT_INPUT) != 0)
				sink->flags &= ~MM_EVENT_INPUT_PENDING;
			// Remember a reader has been started.
			sink->flags |= MM_EVENT_INPUT_STARTED;
			// Submit a reader work.
			mm_strand_add_work(sink->listener->strand, &sink->input_work);
		} else if (flags == 0) {
			mm_event_complete(sink);
		}
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
		// Run the writer fiber presumably blocked on the socket.
		mm_fiber_run(sink->output_fiber);
	} else {
		// Check to see if a new writer should be spawned.
		flags = sink->flags & (MM_EVENT_OUTPUT_STARTED | MM_EVENT_OUTPUT_PENDING);
		if (flags == MM_EVENT_OUTPUT_PENDING) {
			if ((sink->flags & MM_EVENT_ONESHOT_OUTPUT) != 0)
				sink->flags &= ~MM_EVENT_OUTPUT_PENDING;
			// Remember a writer has been started.
			sink->flags |= MM_EVENT_OUTPUT_STARTED;
			// Submit a writer work.
			mm_strand_add_work(sink->listener->strand, &sink->output_work);
		} else if (flags == 0) {
			mm_event_complete(sink);
		}
	}

	LEAVE();
}

static void NONNULL(1)
mm_event_input_complete(struct mm_work *work, mm_value_t value UNUSED)
{
	ENTER();

	struct mm_event_fd *sink = containerof(work, struct mm_event_fd, input_work);
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Bail out if the event sink is shutdown.
	ASSERT((sink->flags & MM_EVENT_INPUT_STARTED) != 0);
	if (mm_event_input_closed(sink)) {
		sink->flags &= ~MM_EVENT_INPUT_STARTED;
		mm_event_complete(sink);
		goto leave;
	}

	// Check to see if a new reader should be spawned.
	uint32_t fd_flags = sink->flags & (MM_EVENT_INPUT_READY | MM_EVENT_INPUT_ERROR);
	if ((sink->flags & MM_EVENT_INPUT_PENDING) != 0 && fd_flags != 0) {
		if ((sink->flags & MM_EVENT_ONESHOT_INPUT) != 0)
			sink->flags &= ~MM_EVENT_INPUT_PENDING;
		// Submit a reader work.
		mm_strand_add_work(sink->listener->strand, &sink->input_work);
	} else {
		sink->flags &= ~MM_EVENT_INPUT_STARTED;
		mm_event_complete(sink);
	}

leave:
	LEAVE();
}

static void NONNULL(1)
mm_event_output_complete(struct mm_work *work, mm_value_t value UNUSED)
{
	ENTER();

	struct mm_event_fd *sink = containerof(work, struct mm_event_fd, output_work);
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Bail out if the event sink is shutdown.
	ASSERT((sink->flags & MM_EVENT_OUTPUT_STARTED) != 0);
	if (mm_event_output_closed(sink)) {
		sink->flags &= ~MM_EVENT_OUTPUT_STARTED;
		mm_event_complete(sink);
		goto leave;
	}

	// Check to see if a new writer should be spawned.
	uint32_t fd_flags = sink->flags & (MM_EVENT_OUTPUT_READY | MM_EVENT_OUTPUT_ERROR);
	if ((sink->flags & MM_EVENT_OUTPUT_PENDING) != 0 && fd_flags != 0) {
		if ((sink->flags & MM_EVENT_ONESHOT_OUTPUT) != 0)
			sink->flags &= ~MM_EVENT_OUTPUT_PENDING;
		// Submit a writer work.
		mm_strand_add_work(sink->listener->strand, &sink->output_work);
	} else {
		sink->flags &= ~MM_EVENT_OUTPUT_STARTED;
		mm_event_complete(sink);
	}

leave:
	LEAVE();
}

static mm_value_t NONNULL(1)
mm_event_reclaim_routine(struct mm_work *work)
{
	ENTER();

	struct mm_event_fd *sink = containerof(work, struct mm_event_fd, reclaim_work);
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	// Notify a reader/writer about closing.
	// TODO: don't block here, have a queue of closed sinks
	while (sink->input_fiber != NULL || sink->output_fiber != NULL) {
		struct mm_fiber *fiber = sink->listener->strand->fiber;
		mm_priority_t priority = MM_PRIO_UPPER(fiber->priority, 1);
		if (sink->input_fiber != NULL)
			mm_fiber_hoist(sink->input_fiber, priority);
		if (sink->output_fiber != NULL)
			mm_fiber_hoist(sink->output_fiber, priority);
		mm_fiber_yield();
	}

	// Destroy the sink.
	ASSERT(mm_event_closed(sink));
	(sink->destroy)(sink);

	LEAVE();
	return 0;
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

static mm_value_t
mm_event_unexpected(struct mm_work *work UNUSED)
{
	mm_error(0, "unexpected event");
	return 0;
}

void NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd,
		    mm_work_routine_t input_routine, mm_work_routine_t output_routine,
		    mm_event_capacity_t input, mm_event_capacity_t output,
		    bool fixed_listener)
{
	ENTER();
	DEBUG("fd %d", fd);
	ASSERT(fd >= 0);

	sink->fd = fd;
	sink->flags = 0;
	sink->listener = NULL;
	sink->input_fiber = NULL;
	sink->output_fiber = NULL;

#if ENABLE_SMP
	sink->receive_stamp = 0;
	sink->dispatch_stamp = 0;
	sink->complete_stamp = 0;
#endif
	sink->queued_events = 0;

	if (input_routine != NULL) {
		mm_work_prepare(&sink->input_work, input_routine, mm_event_input_complete);
	} else {
		mm_work_prepare_simple(&sink->input_work, mm_event_unexpected);
	}
	if (output_routine != NULL) {
		mm_work_prepare(&sink->output_work, output_routine, mm_event_output_complete);
	} else {
		mm_work_prepare_simple(&sink->output_work, mm_event_unexpected);
	}
	mm_work_prepare_simple(&sink->reclaim_work, mm_event_reclaim_routine);

	if (fixed_listener)
		sink->flags |= MM_EVENT_FIXED_LISTENER;

	if (input == MM_EVENT_REGULAR) {
		sink->flags |= MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_PENDING;
	} else if (input == MM_EVENT_ONESHOT) {
		sink->flags |= MM_EVENT_ONESHOT_INPUT | MM_EVENT_INPUT_TRIGGER;
	}

	if (output == MM_EVENT_REGULAR) {
		sink->flags |= MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_PENDING;
	} else if (output == MM_EVENT_ONESHOT) {
		sink->flags |= MM_EVENT_ONESHOT_OUTPUT | MM_EVENT_OUTPUT_TRIGGER;
	}

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

	sink->flags &= ~MM_EVENT_INPUT_READY;

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

	sink->flags &= ~MM_EVENT_OUTPUT_READY;

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

	if (mm_event_input_closed(sink))
		goto leave;

	if ((sink->flags & MM_EVENT_INPUT_STARTED) != 0) {
		// If a reader is already active then remember to start another
		// one when it ends.
		sink->flags |= MM_EVENT_INPUT_PENDING;
	} else {
		// Remember a reader has been started.
		sink->flags |= MM_EVENT_INPUT_STARTED;
		// Submit a reader work.
		mm_strand_add_work(sink->listener->strand, &sink->input_work);
		// Let it start immediately.
		mm_fiber_yield();
	}

leave:
	LEAVE();
}

void NONNULL(1)
mm_event_start_output_work(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->listener->strand == mm_strand_selfptr());

	if (mm_event_output_closed(sink))
		goto leave;

	if ((sink->flags & MM_EVENT_OUTPUT_STARTED) != 0) {
		// If a writer is already active then remember to start another
		// one when it ends.
		sink->flags |= MM_EVENT_OUTPUT_PENDING;
	} else {
		// Remember a writer has been started.
		sink->flags |= MM_EVENT_OUTPUT_STARTED;
		// Submit a writer work.
		mm_strand_add_work(sink->listener->strand, &sink->output_work);
		// Let it start immediately.
		mm_fiber_yield();
	}

leave:
	LEAVE();
}

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

#define MM_EVENT_POLLER_SPIN	(4)

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

static void
mm_event_wakeup_req(uintptr_t *arguments UNUSED)
{
}

void NONNULL(1)
mm_event_listen(struct mm_event_listener *listener, mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_dispatch *dispatch = listener->dispatch;

	if (mm_event_listener_got_events(listener)) {
		// Presume that if there were incoming events moments ago then
		// there is a chance to get some more immediately. Don't sleep
		// to avoid a context switch.
		timeout = 0;
		// Reset event counters set at the previous cycle.
		mm_event_listener_clear_events(listener);
	}

	if (mm_event_backend_has_changes(&listener->storage)) {
		// There may be changes that need to be immediately acknowledged.
		timeout = 0;
	} else {
		// Check to see if there are any queued events. If so then
		// try to bypass the entire poll/wait machinery. The check
		// does not have to be precise so there is no need to take
		// the event_sink_lock lock.
		if (mm_memory_load(dispatch->sink_queue_num) != 0) {
			// Try to pull a few queued events. This may fail
			// because of concurrent listeners doing the same.
			mm_event_listener_handle_queued(listener);
			if (listener->events.dequeued) {
#if ENABLE_EVENT_STATS
				// Update statistics.
				listener->stats.omit_calls++;
#endif
				goto leave;
			}
		}
	}

	// The first arrived thread is elected to conduct the next event poll.
	bool is_poller_thread = mm_regular_trylock(&dispatch->poller_lock);
	if (is_poller_thread) {
		// If the previous poller thread received some events then keep
		// spinning for a while to avoid extra context switches.
		if (dispatch->poller_spin) {
			dispatch->poller_spin--;
			timeout = 0;
		}

		// Wait for incoming events or timeout expiration.
		mm_event_poll(listener, dispatch, timeout);

		// Reset the poller spin counter.
		if (mm_event_listener_got_events(listener))
			dispatch->poller_spin = MM_EVENT_POLLER_SPIN;

		// Give up the poller thread role.
		mm_regular_unlock(&dispatch->poller_lock);

	} else if (timeout == 0) {
		// Poll for immediately available events.
		mm_event_poll(listener, dispatch, 0);

	} else {
		// Wait for forwarded events or timeout expiration.
		mm_event_wait(listener, dispatch, timeout);
	}

leave:
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

/* Wakeup a listener if it sleeps. */
void NONNULL(1)
mm_event_wakeup(struct mm_event_listener *listener)
{
	ENTER();

	mm_event_call_0(listener, mm_event_wakeup_req);

	LEAVE();
}


void NONNULL(1)
mm_event_wakeup_any(struct mm_event_dispatch *dispatch)
{
	ENTER();

	mm_thread_t n = dispatch->nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &dispatch->listeners[i];
		uintptr_t state = mm_memory_load(listener->state);
		mm_event_listener_status_t status = state & MM_EVENT_LISTENER_STATUS;
		if (status == MM_EVENT_LISTENER_WAITING) {
			mm_event_wakeup(listener);
			break;
		}
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
mm_event_async_execute(struct mm_event_async *post)
{
	(*post->routine)(post->arguments);
}

static inline bool NONNULL(1, 2)
mm_event_receive_call(struct mm_event_listener *listener, struct mm_event_async *post)
{
	return mm_ring_mpsc_get_n(listener->async_queue, post->data, (MM_EVENT_ASYNC_MAX + 1));
}

static inline bool NONNULL(1, 2)
mm_event_receive_post(struct mm_event_dispatch *dispatch, struct mm_event_async *post)
{
	return mm_ring_mpmc_get_n(dispatch->async_queue, post->data, (MM_EVENT_ASYNC_MAX + 1));
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
			mm_event_async_execute(&async);
#if ENABLE_EVENT_STATS
			listener->stats.dequeued_async_calls++;
#endif
		} while (mm_event_receive_call(listener, &async));

		// Restore normal running state.
		strand->state = state;
	}

	LEAVE();
}

bool NONNULL(1)
mm_event_handle_posts(struct mm_event_listener *listener UNUSED)
{
	ENTER();
	bool rc = false;

#if ENABLE_SMP
	struct mm_event_async async;
	if (mm_event_receive_post(listener->dispatch, &async)) {
		mm_event_async_execute(&async);
#if ENABLE_EVENT_STATS
		listener->stats.dequeued_async_posts++;
#endif
		rc = true;
	}
#endif

	LEAVE();
	return rc;
}


/**********************************************************************
 * Asynchronous procedure calls targeting a single listener.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_call_0(struct mm_event_listener *listener, mm_event_async_routine_t r)
{
	MM_POST(0, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r);
}

bool NONNULL(1, 2)
mm_event_trycall_0(struct mm_event_listener *listener, mm_event_async_routine_t r)
{
	MM_TRYPOST(0, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r);
}

void NONNULL(1, 2)
mm_event_call_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1)
{
	MM_POST(1, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1);
}

bool NONNULL(1, 2)
mm_event_trycall_1(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1)
{
	MM_TRYPOST(1, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1);
}

void NONNULL(1, 2)
mm_event_call_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2)
{
	MM_POST(2, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2);
}

bool NONNULL(1, 2)
mm_event_trycall_2(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2)
{
	MM_TRYPOST(2, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2);
}

void NONNULL(1, 2)
mm_event_call_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_POST(3, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3);
}

bool NONNULL(1, 2)
mm_event_trycall_3(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYPOST(3, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3);
}

void NONNULL(1, 2)
mm_event_call_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_POST(4, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4);
}

bool NONNULL(1, 2)
mm_event_trycall_4(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYPOST(4, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4);
}

void NONNULL(1, 2)
mm_event_call_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_POST(5, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4, a5);
}

bool NONNULL(1, 2)
mm_event_trycall_5(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYPOST(5, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4, a5);
}

void NONNULL(1, 2)
mm_event_call_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_POST(6, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		r, a1, a2, a3, a4, a5, a6);
}

bool NONNULL(1, 2)
mm_event_trycall_6(struct mm_event_listener *listener, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYPOST(6, listener->async_queue, mm_event_call_stat, mm_event_notify, listener,
		   r, a1, a2, a3, a4, a5, a6);
}

/**********************************************************************
 * Asynchronous procedure calls targeting any listener of a dispatcher.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_post_0(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r)
{
	MM_POST(0, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r);
}

bool NONNULL(1, 2)
mm_event_trypost_0(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r)
{
	MM_TRYPOST(0, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r);
}

void NONNULL(1, 2)
mm_event_post_1(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1)
{
	MM_POST(1, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1);
}

bool NONNULL(1, 2)
mm_event_trypost_1(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1)
{
	MM_TRYPOST(1, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r, a1);
}

void NONNULL(1, 2)
mm_event_post_2(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2)
{
	MM_POST(2, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1, a2);
}

bool NONNULL(1, 2)
mm_event_trypost_2(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2)
{
	MM_TRYPOST(2, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r, a1, a2);
}

void NONNULL(1, 2)
mm_event_post_3(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_POST(3, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1, a2, a3);
}

bool NONNULL(1, 2)
mm_event_trypost_3(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYPOST(3, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r, a1, a2, a3);
}

void NONNULL(1, 2)
mm_event_post_4(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_POST(4, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1, a2, a3, a4);
}

bool NONNULL(1, 2)
mm_event_trypost_4(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYPOST(4, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify,
		   dispatch, r, a1, a2, a3, a4);
}

void NONNULL(1, 2)
mm_event_post_5(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_POST(5, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1, a2, a3, a4, a5);
}

bool NONNULL(1, 2)
mm_event_trypost_5(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYPOST(5, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r, a1, a2, a3, a4, a5);
}

void NONNULL(1, 2)
mm_event_post_6(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_POST(6, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		r, a1, a2, a3, a4, a5, a6);
}

bool NONNULL(1, 2)
mm_event_trypost_6(struct mm_event_dispatch *dispatch, mm_event_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYPOST(6, dispatch->async_queue, mm_event_post_stat, mm_event_post_notify, dispatch,
		   r, a1, a2, a3, a4, a5, a6);
}
