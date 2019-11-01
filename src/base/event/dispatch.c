/*
 * base/event/dispatch.c - MainMemory event dispatch.
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

#include "base/event/dispatch.h"

#include "base/logger.h"
#include "base/event/listener.h"
#include "base/fiber/strand.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"

#define MM_DISPATCH_EVENT_QUEUE_SIZE		(2 * MM_EVENT_BACKEND_NEVENTS)
#define MM_DISPATCH_ASYNC_QUEUE_MIN_SIZE	16

struct mm_event_dispatch_listener_attr
{
	struct mm_strand *strand;
};

/**********************************************************************
 * Event dispatcher setup and cleanup routines.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_attr_prepare(struct mm_event_dispatch_attr *attr)
{
	memset(attr, 0, sizeof *attr);
}

void NONNULL(1)
mm_event_dispatch_attr_cleanup(struct mm_event_dispatch_attr *attr)
{
	if (attr->listeners_attr != NULL)
		mm_global_free(attr->listeners_attr);
}

void NONNULL(1)
mm_event_dispatch_attr_setlisteners(struct mm_event_dispatch_attr *attr, mm_thread_t n)
{
	VERIFY(n > 0);

	attr->nlisteners = n;
	if (attr->listeners_attr != NULL) {
		mm_global_free(attr->listeners_attr);
		attr->listeners_attr = NULL;
	}
}

void NONNULL(1)
mm_event_dispatch_attr_setlockspinlimit(struct mm_event_dispatch_attr *attr, uint32_t value)
{
	attr->lock_spin_limit = value;
}

void NONNULL(1)
mm_event_dispatch_attr_setpollspinlimit(struct mm_event_dispatch_attr *attr, uint32_t value)
{
	attr->poll_spin_limit = value;
}

void NONNULL(1, 3)
mm_event_dispatch_attr_setlistenerstrand(struct mm_event_dispatch_attr *attr, mm_thread_t n, struct mm_strand *strand)
{
	if (unlikely(attr->nlisteners == 0))
		mm_fatal(0, "the number of event listeners is not set");
	if (unlikely(n >= attr->nlisteners))
		mm_fatal(0, "invalid event listener number: %d (max is %d)", n, attr->nlisteners);

	if (attr->listeners_attr == NULL) {
		attr->listeners_attr = mm_global_calloc(attr->nlisteners, sizeof(struct mm_event_dispatch_listener_attr));
	}

	attr->listeners_attr[n].strand = strand;
}

void NONNULL(1, 2)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, const struct mm_event_dispatch_attr *attr)
{
	ENTER();

	// Validate the provided listener info.
	if (unlikely(attr->nlisteners == 0))
		mm_fatal(0, "the number of event listeners is not set");
	if (unlikely(attr->listeners_attr == NULL))
		mm_fatal(0, "event listener attributes are not set");

	// Prepare listener info.
	dispatch->nlisteners = attr->nlisteners;
	dispatch->listeners = mm_common_calloc(attr->nlisteners, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < attr->nlisteners; i++) {
		struct mm_strand *strand = attr->listeners_attr[i].strand;
		if (strand == NULL)
			mm_fatal(0, "the fiber strand is not set for event listener: %d", i);
		mm_event_listener_prepare(&dispatch->listeners[i], dispatch, strand);
	}

	// Initialize spinning parameters.
	dispatch->lock_spin_limit = attr->lock_spin_limit;
	dispatch->poll_spin_limit = attr->poll_spin_limit;
	mm_brief("event-lock-spin-limit: %d", dispatch->lock_spin_limit);
	mm_brief("event-poll-spin-limit: %d", dispatch->poll_spin_limit);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend, &dispatch->listeners[0].storage);
	// Initialize event sink reclamation data.
	mm_event_epoch_prepare(&dispatch->global_epoch);

#if ENABLE_SMP
	// Initialize poller thread data.
	dispatch->poller_lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	dispatch->poll_spin_count = 0;

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

		mm_log_fmt("listener %d:\n", i);
		mm_log_fmt(" tasks=%llu task-rings=%llu\n",
			   (unsigned long long) mm_counter_shared_load(&listener->context->tasks.tail_count),
			   (unsigned long long) mm_counter_shared_load(&listener->context->tasks.ring_count));
		mm_log_fmt(" listen=%llu (wait=%llu poll=%llu/%llu spin=%llu)\n"
			   " stray=%llu direct=%llu forwarded=%llu received=%llu retargeted=%llu\n"
			   " async-calls=%llu/%llu/%llu direct-calls=%llu\n",
			   (unsigned long long) (stats->wait_calls + stats->poll_calls + stats->spin_count),
			   (unsigned long long) stats->wait_calls,
			   (unsigned long long) stats->poll_calls,
			   (unsigned long long) stats->zero_poll_calls,
			   (unsigned long long) stats->spin_count,
			   (unsigned long long) stats->stray_events,
			   (unsigned long long) stats->direct_events,
			   (unsigned long long) stats->forwarded_events,
			   (unsigned long long) stats->received_forwarded_events,
			   (unsigned long long) stats->retargeted_forwarded_events,
			   (unsigned long long) stats->enqueued_async_calls,
			   (unsigned long long) stats->dequeued_async_calls,
			   (unsigned long long) stats->enqueued_async_posts,
			   (unsigned long long) stats->direct_calls);

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
