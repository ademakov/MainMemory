/*
 * base/event/dispatch.c - MainMemory event dispatch.
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

#include "base/event/dispatch.h"

#include "base/logger.h"
#include "base/event/listener.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

void NONNULL(1)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, mm_thread_t nthreads)
{
	ENTER();
	ASSERT(nthreads > 0);

	// Domain pointer is set when domain with corresponding dispatch
	// attribute is created.
	dispatch->domain = NULL;

	// Initialize event sink reclamation data.
	mm_event_epoch_prepare(&dispatch->global_epoch);

	// Prepare listener info.
	dispatch->nlisteners = nthreads;
	dispatch->listeners = mm_common_calloc(nthreads, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < nthreads; i++)
		mm_event_listener_prepare(&dispatch->listeners[i], dispatch);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend, &dispatch->listeners[0].storage);

	// Initialize poller thread data.
	dispatch->poller_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	dispatch->poller_spin = 0;

	// Initialize the sink queue.
	dispatch->sink_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	dispatch->sink_queue_num = 0;
	dispatch->sink_queue_head = 0;
	dispatch->sink_queue_tail = 0;
	dispatch->sink_queue_size = 2 * MM_EVENT_BACKEND_NEVENTS;
	dispatch->sink_queue = mm_common_calloc(dispatch->sink_queue_size, sizeof(dispatch->sink_queue[0]));

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

	// Release the sink queue.
	mm_common_free(dispatch->sink_queue);

	LEAVE();
}

/**********************************************************************
 * Event statistics.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_stats(struct mm_event_dispatch *dispatch UNUSED)
{
	ENTER();

#if ENABLE_EVENT_STATS
	mm_thread_t n = dispatch->nlisteners;
	struct mm_event_listener *listeners = dispatch->listeners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &listeners[i];
		struct mm_event_listener_stats *stats = &listener->stats;

		mm_log_fmt("listener %d: "
			   "wait=%llu poll=%llu/%llu "
			   "stray=%llu direct=%llu queued=%llu/%llu forwarded=%llu\n", i,
			   (unsigned long long) stats->wait_calls,
			   (unsigned long long) stats->poll_calls,
			   (unsigned long long) stats->zero_poll_calls,
			   (unsigned long long) stats->stray_events,
			   (unsigned long long) stats->direct_events,
			   (unsigned long long) stats->enqueued_events,
			   (unsigned long long) stats->dequeued_events,
			   (unsigned long long) stats->forwarded_events);

		for (int j = 0; j <= MM_EVENT_BACKEND_NEVENTS; j++) {
			uint64_t n = listener->storage.nevents_stats[j];
			if (j && !n)
				continue;
			mm_log_fmt(" %d=%llu", j, (unsigned long long) n);
		}
		mm_log_str("\n");
	}
#endif

	LEAVE();
}
