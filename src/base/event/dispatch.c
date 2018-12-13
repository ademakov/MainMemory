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

#include "base/bitops.h"
#include "base/logger.h"
#include "base/event/listener.h"
#include "base/fiber/strand.h"
#include "base/memory/memory.h"

#define MM_DISPATCH_QUEUE_MIN_SIZE	16

void NONNULL(1, 3)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, mm_thread_t nthreads, struct mm_strand *strands,
			  uint32_t dispatch_queue_size, uint32_t listener_queue_size)
{
	ENTER();
	ASSERT(nthreads > 0);

	// Initialize event sink reclamation data.
	mm_event_epoch_prepare(&dispatch->global_epoch);

	// Create the associated request queue.
	uint32_t sz = mm_upper_pow2(dispatch_queue_size);
	if (sz < MM_DISPATCH_QUEUE_MIN_SIZE)
		sz = MM_DISPATCH_QUEUE_MIN_SIZE;
	dispatch->async_queue = mm_ring_mpmc_create(sz);

	// Prepare listener info.
	dispatch->nlisteners = nthreads;
	dispatch->listeners = mm_common_calloc(nthreads, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < nthreads; i++)
		mm_event_listener_prepare(&dispatch->listeners[i], dispatch, &strands[i], listener_queue_size);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend, &dispatch->listeners[0].storage);

#if ENABLE_SMP
	// Initialize poller thread data.
	dispatch->poller_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	dispatch->poller_spin = 0;

#if ENABLE_EVENT_SINK_LOCK
	// Initialize the event sink lock.
	dispatch->sink_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
#endif
#endif

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

	// Destroy the associated request queue.
	mm_ring_mpmc_destroy(dispatch->async_queue);

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

		mm_log_fmt("listener %d:\n"
			   " listen=%llu (wait=%llu poll=%llu/%llu omit=%llu)\n"
			   " stray=%llu direct=%llu forwarded=%llu\n"
			   " async-calls=%llu/%llu async-posts=%llu/%llu\n", i,
			   (unsigned long long) (stats->wait_calls + stats->poll_calls + stats->omit_calls),
			   (unsigned long long) stats->wait_calls,
			   (unsigned long long) stats->poll_calls,
			   (unsigned long long) stats->zero_poll_calls,
			   (unsigned long long) stats->omit_calls,
			   (unsigned long long) stats->stray_events,
			   (unsigned long long) stats->direct_events,
			   (unsigned long long) stats->forwarded_events,
			   (unsigned long long) stats->enqueued_async_calls,
			   (unsigned long long) stats->dequeued_async_calls,
			   (unsigned long long) stats->enqueued_async_posts,
			   (unsigned long long) stats->dequeued_async_posts);

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
