/*
 * base/event/dispatch.c - MainMemory event dispatch.
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

#include "base/event/dispatch.h"

#include "base/logger.h"
#include "base/event/listener.h"
#include "base/memory/alloc.h"

#define MM_DISPATCH_EVENT_QUEUE_SIZE		(2 * MM_EVENT_BACKEND_NEVENTS)
#define MM_DISPATCH_ASYNC_QUEUE_MIN_SIZE	16

struct mm_event_dispatch_listener_attr
{
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
		mm_memory_free(attr->listeners_attr);
}

void NONNULL(1)
mm_event_dispatch_attr_setlisteners(struct mm_event_dispatch_attr *attr, mm_thread_t n)
{
	VERIFY(n > 0);

	attr->nlisteners = n;
	if (attr->listeners_attr != NULL) {
		mm_memory_free(attr->listeners_attr);
		attr->listeners_attr = NULL;
	}
}

#if DISPATCH_ATTRS
void NONNULL(1, 3)
mm_event_dispatch_attr_setxxx(struct mm_event_dispatch_attr *attr, mm_thread_t n, xxx_t xxx)
{
	if (unlikely(attr->nlisteners == 0))
		mm_fatal(0, "the number of event listeners is not set");
	if (unlikely(n >= attr->nlisteners))
		mm_fatal(0, "invalid event listener number: %d (max is %d)", n, attr->nlisteners);

	if (attr->listeners_attr == NULL) {
		attr->listeners_attr = mm_memory_xcalloc(attr->nlisteners, sizeof(struct mm_event_dispatch_listener_attr));
	}

	attr->listeners_attr[n].xxx = xxx;
}
#endif

void NONNULL(1, 2)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, const struct mm_event_dispatch_attr *attr)
{
	ENTER();

	// Validate the provided listener info.
	if (unlikely(attr->nlisteners == 0))
		mm_fatal(0, "the number of event listeners is not set");
#if DISPATCH_ATTRS
	if (unlikely(attr->listeners_attr == NULL))
		mm_fatal(0, "event listener attributes are not set");
#endif

	// Initialize common system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);
	// Initialize event sink reclamation data.
	mm_event_epoch_prepare(&dispatch->global_epoch);

	// Prepare listener info.
	dispatch->nlisteners = attr->nlisteners;
	dispatch->listeners = mm_memory_xcalloc(attr->nlisteners, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < attr->nlisteners; i++)
		mm_event_listener_prepare(&dispatch->listeners[i], dispatch);

	LEAVE();
}

void NONNULL(1)
mm_event_dispatch_cleanup(struct mm_event_dispatch *dispatch)
{
	ENTER();

	// Release listener info.
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_listener_cleanup(&dispatch->listeners[i]);
	mm_memory_free(dispatch->listeners);

	// Release common system-specific resources.
	mm_event_backend_cleanup(&dispatch->backend);

	LEAVE();
}

/**********************************************************************
 * Event statistics.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_stats(struct mm_event_dispatch *dispatch UNUSED)
{
	ENTER();

	mm_thread_t n = dispatch->nlisteners;
	struct mm_event_listener *listeners = dispatch->listeners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &listeners[i];
		mm_log_fmt("listener %d:\n", i);

#if ENABLE_EVENT_STATS
		struct mm_event_listener_stats *stats = &listener->stats;

		mm_log_fmt(" listen=%llu (wait=%llu poll=%llu/%llu)\n"
			   " notifications=%llu events=%llu/%llu/%llu\n",
			   (unsigned long long) (stats->wait_calls + stats->poll_calls),
			   (unsigned long long) stats->wait_calls,
			   (unsigned long long) stats->poll_calls,
			   (unsigned long long) stats->zero_poll_calls,
			   (unsigned long long) listener->notifications,
			   (unsigned long long) stats->events,
			   (unsigned long long) stats->forwarded_events,
			   (unsigned long long) stats->repeatedly_forwarded_events);

		for (int j = 0; j <= MM_EVENT_BACKEND_NEVENTS; j++) {
			uint64_t n = listener->backend.nevents_stats[j];
			if (j && !n)
				continue;
			mm_log_fmt(" %d=%llu", j, (unsigned long long) n);
		}
#else
		mm_log_fmt(" notifications=%llu", (unsigned long long) listener->notifications);
#endif

		mm_log_str("\n");
	}

	LEAVE();
}
