/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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
#include "base/thread/domain.h"
#include "base/thread/thread.h"

/**********************************************************************
 * I/O events control.
 **********************************************************************/

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_handler_t handler,
		    mm_event_occurrence_t input_mode, mm_event_occurrence_t output_mode,
		    mm_event_affinity_t target)
{
	ASSERT(fd >= 0);
	// It is forbidden to have both input and output ignored.
	VERIFY(input_mode != MM_EVENT_IGNORED || output_mode != MM_EVENT_IGNORED);

	sink->fd = fd;
	sink->target = MM_THREAD_NONE;
	sink->handler = handler;

#if ENABLE_SMP
	sink->receive_stamp = 0;
	sink->dispatch_stamp = 0;
	sink->complete_stamp = 0;
#endif
	sink->queued_events = 0;

	sink->loose_target = (target == MM_EVENT_LOOSE);
	sink->bound_target = (target == MM_EVENT_BOUND);

	sink->oneshot_input_trigger = false;
	sink->oneshot_output_trigger = false;
	sink->changed = false;

	if (input_mode == MM_EVENT_IGNORED) {
		sink->regular_input = false;
		sink->oneshot_input = false;
	} else if (input_mode == MM_EVENT_ONESHOT) {
		// Oneshot state cannot be properly managed for loose sinks.
		ASSERT(!sink->loose_target);
		sink->regular_input = false;
		sink->oneshot_input = true;
	} else {
		sink->regular_input = true;
		sink->oneshot_input = false;
	}

	if (output_mode == MM_EVENT_IGNORED) {
		sink->regular_output = false;
		sink->oneshot_output = false;
	} else if (output_mode == MM_EVENT_ONESHOT) {
		// Oneshot state cannot be properly managed for loose sinks.
		ASSERT(!sink->loose_target);
		sink->regular_output = false;
		sink->oneshot_output = true;
	} else {
		sink->regular_output = true;
		sink->oneshot_output = false;
	}

	return true;
}

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	mm_thread_t target = mm_event_target(sink);
	if (target == MM_THREAD_NONE) {
		sink->target = listener->target;
	} else {
		VERIFY(target == listener->target);
	}
	mm_event_listener_add(listener, sink, MM_EVENT_REGISTER);
}

void NONNULL(1)
mm_event_unregister_fd(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_UNREGISTER);
}

void NONNULL(1)
mm_event_unregister_faulty_fd(struct mm_event_fd *sink)
{
	struct mm_event_change change = { .kind = MM_EVENT_UNREGISTER, .sink = sink };
	struct mm_domain *domain = mm_domain_selfptr();
	struct mm_event_dispatch *dispatch = mm_domain_getdispatch(domain);
	mm_event_backend_change(&dispatch->backend, &change);
}

void NONNULL(1)
mm_event_trigger_input(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_INPUT);
}

void NONNULL(1)
mm_event_trigger_output(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_OUTPUT);
}

/**********************************************************************
 * Event listening and notification.
 **********************************************************************/

#define MM_EVENT_POLLER_SPIN	(4)

static void NONNULL(1)
mm_event_wait(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
	      mm_timeout_t timeout)
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

	// Get the next expected notify stamp.
	const mm_stamp_t stamp = mm_event_listener_stamp(listener);
	// Announce that the thread is about to sleep.
	mm_event_listener_waiting(listener, stamp);

	// Wait for a wake-up notification or timeout unless a pending
	// notification is detected.
	if (mm_event_listener_restful(listener, stamp))
		mm_event_listener_timedwait(listener, stamp, timeout);

	// Announce the start of another working cycle.
	mm_event_listener_running(listener);

	LEAVE();
}

static void NONNULL(1)
mm_event_poll(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
	      mm_timeout_t timeout)
{
	ENTER();

#if ENABLE_EVENT_STATS
	// Update statistics.
	listener->stats.poll_calls++;
	listener->stats.zero_poll_calls += (timeout == 0);
#endif

	// Reset event counters.
	listener->direct_events_estimate = 0;
	listener->direct_events = 0;
	listener->enqueued_events = 0;
	listener->dequeued_events = 0;
	listener->forwarded_events = 0;

	// Start a reclamation critical section.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	if (timeout != 0) {
		// Cleanup stale event notifications.
		mm_event_backend_dampen(&dispatch->backend);

		// Publish the log before a possible sleep.
		mm_log_relay();

		// Get the next expected notify stamp.
		const mm_stamp_t stamp = mm_event_listener_stamp(listener);
		// Advertise that the thread is about to sleep.
		mm_event_listener_polling(listener, stamp);

		// Wait for a wake-up notification or timeout unless a pending
		// notification is detected.
		if (!mm_event_listener_restful(listener, stamp))
			timeout = 0;
	}

	// Check incoming events and wait for notification/timeout.
	mm_event_backend_listen(&dispatch->backend, listener, timeout);

	// Announce the start of another working cycle.
	mm_event_listener_running(listener);

	// End a reclamation critical section.
	mm_event_epoch_leave(&listener->epoch, &dispatch->global_epoch);

	// Flush forwarded events.
	if (listener->forwarded_events)
		mm_event_forward_flush(&listener->forward);

#if ENABLE_EVENT_STATS
	// Update event statistics.
	listener->stats.direct_events += listener->direct_events;
	listener->stats.enqueued_events += listener->enqueued_events;
	listener->stats.dequeued_events += listener->dequeued_events;
	listener->stats.forwarded_events += listener->forwarded_events;
#endif

	// Forget just handled change events.
	mm_event_listener_clear_changes(listener);

	LEAVE();
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
	} else if (mm_memory_load(dispatch->sink_queue_num) != 0) {
		// Check if there are immediately available events in the queue.
		// This check does not have to be precise so there is no need to
		// use event_sink_lock here.
		timeout = 0;
	} else if (mm_event_listener_has_changes(listener)) {
		// There may be changes that need to be immediately acknowledged.
		timeout = 0;
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

void NONNULL(1)
mm_event_notify_any(struct mm_event_dispatch *dispatch)
{
	ENTER();

	mm_thread_t n = dispatch->nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &dispatch->listeners[i];
		uintptr_t state = mm_memory_load(listener->state);
		mm_event_listener_status_t status = state & MM_EVENT_LISTENER_STATUS;
		if (status == MM_EVENT_LISTENER_WAITING) {
			mm_thread_wakeup(listener->thread);
			break;
		}
	}

	LEAVE();
}
