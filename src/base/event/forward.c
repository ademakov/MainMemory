/*
 * base/event/forward.c - MainMemory event forwarding.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#include "base/event/forward.h"

#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"
#include "base/memory/memory.h"
#include "base/thread/thread.h"

/**********************************************************************
 * Event forwarding request handlers.
 **********************************************************************/

static void
mm_event_forward_1(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[1];
	mm_backend_handle((struct mm_event_fd *) arguments[0], events & 15);

	LEAVE();
}

static void
mm_event_forward_2(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[2];
	mm_backend_handle((struct mm_event_fd *) arguments[0], events & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[1], events >> 4);

	LEAVE();
}

static void
mm_event_forward_3(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[3];
	mm_backend_handle((struct mm_event_fd *) arguments[0], events & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[2], events >> 8);

	LEAVE();
}

static void
mm_event_forward_4(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[4];
	mm_backend_handle((struct mm_event_fd *) arguments[0], events & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[3], events >> 12);

	LEAVE();
}

static void
mm_event_forward_5(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[5];
	mm_backend_handle((struct mm_event_fd *) arguments[0], events & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[3], (events >> 12) & 15);
	mm_backend_handle((struct mm_event_fd *) arguments[4], events >> 16);

	LEAVE();
}

/**********************************************************************
 * Event forwarding request posting.
 **********************************************************************/

static void NONNULL(1, 3)
mm_event_forward_post(struct mm_event_dispatch *dispatch, mm_thread_t target,
		      struct mm_event_forward_buffer *buffer)
{
	ENTER();

	struct mm_event_listener *listener = &dispatch->listeners[target];
	struct mm_thread *thread = listener->thread;

	switch (buffer->nsinks) {
	case 0:
		break;
	case 1:
		buffer->nsinks = 0;
		mm_thread_post_2(thread, mm_event_forward_1,
				 (uintptr_t) buffer->sinks[0],
				 buffer->events[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_thread_post_3(thread, mm_event_forward_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 buffer->events[0]
				 | (buffer->events[1] << 4));
		break;
	case 3:
		buffer->nsinks = 0;
		mm_thread_post_4(thread, mm_event_forward_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8));
		break;
	case 4:
		buffer->nsinks = 0;
		mm_thread_post_5(thread, mm_event_forward_4,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8)
				 | (buffer->events[3] << 12));
		break;
	case 5:
		buffer->nsinks = 0;
		mm_thread_post_6(thread, mm_event_forward_5,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 (uintptr_t) buffer->sinks[4],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8)
				 | (buffer->events[3] << 12)
				 | (buffer->events[4] << 16));
		break;
	default:
		ABORT();
	}

	LEAVE();
}

/**********************************************************************
 * Event forwarding.
 **********************************************************************/

void NONNULL(1)
mm_event_forward_prepare(struct mm_event_forward_cache *cache, mm_thread_t ntargets)
{
	ENTER();

	cache->buffers = mm_common_calloc(ntargets, sizeof(cache->buffers[0]));
	for (mm_thread_t i = 0; i < ntargets; i++) {
		cache->buffers[i].nsinks = 0;
		cache->buffers[i].ntotal = 0;
	}

	mm_bitset_prepare(&cache->targets, &mm_common_space.xarena, ntargets);

	LEAVE();
}

void NONNULL(1)
mm_event_forward_cleanup(struct mm_event_forward_cache *cache)
{
	ENTER();

	// Release forward buffers.
	mm_common_free(cache->buffers);
	mm_bitset_cleanup(&cache->targets, &mm_common_space.xarena);

	LEAVE();
}

void NONNULL(1)
mm_event_forward_flush(struct mm_event_forward_cache *cache)
{
	ENTER();

	struct mm_event_listener *listener = containerof(cache, struct mm_event_listener, forward);
	struct mm_event_dispatch *dispatch = listener->dispatch;

	mm_thread_t target = mm_bitset_find(&cache->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_forward_buffer *buffer = &cache->buffers[target];
		mm_event_forward_post(dispatch, target, buffer);
		buffer->ntotal = 0;

		if (++target < mm_bitset_size(&cache->targets))
			target = mm_bitset_find(&cache->targets, target);
		else
			target = MM_THREAD_NONE;
	}

	mm_bitset_clear_all(&cache->targets);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_forward(struct mm_event_forward_cache *cache, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();

	mm_thread_t target = sink->listener->target;
	struct mm_event_forward_buffer *buffer = &cache->buffers[target];

	// Flush the buffer if it is full.
	if (buffer->nsinks == MM_EVENT_FORWARD_BUFFER_SIZE) {
		struct mm_event_listener *listener = containerof(cache, struct mm_event_listener, forward);
		mm_event_forward_post(listener->dispatch, target, buffer);
	}

	// Add the event to the buffer.
	uint8_t n = buffer->nsinks++;
	buffer->sinks[n] = sink;
	buffer->events[n] = event;
	buffer->ntotal++;

	// Account for it.
	mm_bitset_set(&cache->targets, target);

	LEAVE();
}
