/*
 * base/event/forward.h - MainMemory event forwarding.
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

#ifndef BASE_EVENT_FORWARD_H
#define BASE_EVENT_FORWARD_H

#include "common.h"
#include "base/bitset.h"
#include "base/event/event.h"

#if ENABLE_SMP

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_thread;

#define MM_EVENT_FORWARD_BUFFER_SIZE	(5)

/* Event sink forward buffer. */
struct mm_event_forward_buffer
{
	uint32_t nsinks;
	uint32_t ntotal;
	mm_event_index_t events[MM_EVENT_FORWARD_BUFFER_SIZE];
	struct mm_event_fd *sinks[MM_EVENT_FORWARD_BUFFER_SIZE];
};

struct mm_event_forward_cache
{
	/* Target threads that have received events. */
	struct mm_bitset targets;

	/* Per-thread temporary store for sinks of received events. */
	struct mm_event_forward_buffer *buffers;
};

void NONNULL(1)
mm_event_forward_prepare(struct mm_event_forward_cache *cache, mm_thread_t ntargets);

void NONNULL(1)
mm_event_forward_cleanup(struct mm_event_forward_cache *cache);

void NONNULL(1)
mm_event_forward_flush(struct mm_event_forward_cache *cache, struct mm_event_dispatch *dispatch);

void NONNULL(1, 2)
mm_event_forward(struct mm_event_forward_cache *cache, struct mm_event_fd *sink, mm_event_index_t event);

#endif /* ENABLE_SMP */
#endif /* BASE_EVENT_FORWARD_H */
