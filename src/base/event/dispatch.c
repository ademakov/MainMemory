/*
 * base/event/dispatch.c - MainMemory event dispatch.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

#include "base/event/dispatch.h"

#include "base/logger.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

static uint16_t mm_events_busywait = 5;

void NONNULL(1, 2, 4)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch,
			  struct mm_domain *domain,
			  mm_thread_t nthreads,
			  struct mm_thread *threads[])
{
	ENTER();
	ASSERT(nthreads > 0);

	// Store associated domain.
	dispatch->domain = domain;
	mm_domain_setdispatch(domain, dispatch);

	// Prepare listener info.
	dispatch->nlisteners = nthreads;
	dispatch->listeners = mm_common_calloc(nthreads, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < nthreads; i++) {
		mm_event_listener_prepare(&dispatch->listeners[i], dispatch, threads[i]);
		mm_thread_setlistener(threads[i], &dispatch->listeners[i]);
	}

	dispatch->poller_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	dispatch->poller_thread = MM_THREAD_NONE;

	dispatch->event_sink_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;

	dispatch->reclaim_epoch = 0;

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);

	LEAVE();
}

void NONNULL(1)
mm_event_dispatch_cleanup(struct mm_event_dispatch *dispatch)
{
	ENTER();

	// Release system-specific resources.
	mm_event_backend_cleanup(&dispatch->backend);

	// Release listener info.
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_listener_cleanup(&dispatch->listeners[i]);
	mm_common_free(dispatch->listeners);

	LEAVE();
}

void NONNULL(1)
mm_event_dispatch_listen(struct mm_event_dispatch *dispatch, mm_thread_t thread,
			 mm_timeout_t timeout)
{
	ENTER();
	ASSERT(thread < dispatch->nlisteners);
	struct mm_event_listener *listener = mm_event_dispatch_listener(dispatch, thread);

	if (listener->busywait) {
		// Presume that if there were incoming events moments ago then
		// there is a chance to get some more immediately. Spin a little
		// bit to avoid context switches.
		listener->busywait--;
		timeout = 0;
	} else if (mm_event_listener_has_changes(listener)) {
		// There may be changes that need to be immediately acknowledged.
		timeout = 0;
	}

	// The first arrived thread that is going to sleep is elected to conduct
	// the next event poll.
	bool is_poller_thread = false;
	if (timeout != 0) {
		mm_regular_lock(&dispatch->poller_lock);
		if (dispatch->poller_thread == MM_THREAD_NONE) {
			dispatch->poller_thread = thread;
			is_poller_thread = true;
		}
		mm_regular_unlock(&dispatch->poller_lock);
	}

	if (timeout == 0 || is_poller_thread) {
		if (timeout != 0)
			mm_event_dispatch_advance_epoch(dispatch);

		// Wait for incoming events or timeout expiration.
		mm_event_listener_poll(listener, timeout);

		// Give up the poller thread role.
		if (is_poller_thread) {
			mm_regular_lock(&dispatch->poller_lock);
			dispatch->poller_thread = MM_THREAD_NONE;
			mm_regular_unlock(&dispatch->poller_lock);
		}

		// Forget just handled change events.
		mm_event_listener_clear_changes(listener);

		// Arm busy-wait counter if got any events.
		if (listener->receiver.got_events)
			listener->busywait += mm_events_busywait;
	} else {
		// Wait for forwarded events or timeout expiration.
		mm_event_listener_wait(listener, timeout);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_dispatch_notify_waiting(struct mm_event_dispatch *dispatch)
{
	ENTER();

	mm_thread_t n = dispatch->nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &dispatch->listeners[i];
		if (mm_event_listener_getstate(listener) == MM_EVENT_LISTENER_WAITING) {
			mm_thread_wakeup(listener->thread);
			break;
		}
	}

	LEAVE();
}

/**********************************************************************
 * Reclamation epoch maintenance.
 **********************************************************************/

static void
mm_event_dispatch_observe_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_event_receiver *receiver = (struct mm_event_receiver *) arguments[0];
	if (receiver->reclaim_active)
		mm_event_receiver_observe_epoch(receiver);

	LEAVE();
}

static bool
mm_event_dispatch_check_epoch(struct mm_event_dispatch *dispatch, uint32_t epoch)
{
	ENTER();
	bool rc = true;

	mm_thread_t n = dispatch->nlisteners;
	struct mm_event_listener *listeners = dispatch->listeners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &listeners[i];
		struct mm_event_receiver *receiver = &listener->receiver;
		uint32_t local = mm_memory_load(receiver->reclaim_epoch);
		if (local == epoch)
			continue;

		mm_memory_load_fence();
		bool active = mm_memory_load(receiver->reclaim_active);
		if (active) {
			mm_thread_post_1(listener->thread, mm_event_dispatch_observe_req,
					 (uintptr_t) receiver);
			rc = false;
			break;
		}
	}

	LEAVE();
	return rc;
}

bool NONNULL(1)
mm_event_dispatch_advance_epoch(struct mm_event_dispatch *dispatch)
{
	ENTER();

	uint32_t epoch = mm_memory_load(dispatch->reclaim_epoch);
	bool rc = mm_event_dispatch_check_epoch(dispatch, epoch);
	if (rc) {
		mm_memory_fence(); // TODO: load_store fence
		mm_memory_store(dispatch->reclaim_epoch, epoch + 1);
		DEBUG("advance epoch %u", epoch + 1);
	}

	LEAVE();
	return rc;
}

/**********************************************************************
 * Event statistics.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_stats(struct mm_event_dispatch *dispatch)
{
	ENTER();

	mm_thread_t n = dispatch->nlisteners;
	struct mm_event_listener *listeners = dispatch->listeners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &listeners[i];
		struct mm_event_receiver *receiver = &listener->receiver;
		struct mm_event_receiver_stats *stats = &receiver->stats;

		mm_log_fmt("listener %d: loose=%llu, direct=%llu/%llu, forwarded=%llu, published=%llu\n", i,
			   (unsigned long long) stats->loose_events,
			   (unsigned long long) stats->direct_events,
			   (unsigned long long) stats->stolen_events,
			   (unsigned long long) stats->forwarded_events,
			   (unsigned long long) stats->published_events);

		for (int j = 0; j <= MM_EVENT_BACKEND_NEVENTS; j++) {
			uint64_t n = listener->receiver.storage.storage.nevents_stats[j];
			if (j && !n)
				continue;
			mm_log_fmt(" %d=%llu", j, (unsigned long long) n);
		}
		mm_log_str("\n");
	}

	LEAVE();
}
