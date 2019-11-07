/*
 * base/event/forward.c - MainMemory event forwarding.
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

#include "base/event/forward.h"

#include "base/async.h"
#include "base/context.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"
#include "base/memory/memory.h"

#if ENABLE_SMP

/**********************************************************************
 * Event forwarding request handlers.
 **********************************************************************/

static int NONNULL(1)
mm_event_forward_handle(struct mm_context *context, struct mm_event_fd *sink, mm_event_index_t event)
{
	// Check if the event sink has been bound to another target after forwarding the event.
	struct mm_context *sink_context = sink->context;
	if (context != sink_context) {
		struct mm_event_dispatch *dispatch = context->listener->dispatch;
		mm_thread_t target = sink_context->listener - dispatch->listeners;

		// Add the event to the buffer.
		struct mm_event_forward_buffer *buffer = &context->listener->forward.buffers[target];
		uint32_t n = buffer->nsinks++;
		buffer->sinks[n] = sink;
		buffer->events[n] = event;

		// Account for it.
		mm_bitset_set(&context->listener->forward.targets, target);
		return 1;
	}

	if (event < MM_EVENT_INDEX_OUTPUT) {
		mm_event_backend_target_input(sink, (1u << event));
	} else {
		mm_event_backend_target_output(sink, (1u << event));
	}
	return 0;
}

static void
mm_event_forward_1(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	const uintptr_t events = arguments[1];
	int retargeted = mm_event_forward_handle(context, (struct mm_event_fd *) arguments[0], events & 15);

	// Flush any events that changed their target.
	struct mm_event_listener *const listener = context->listener;
	if (retargeted) {
		mm_event_forward_flush(&listener->forward, listener->dispatch);
#if ENABLE_EVENT_STATS
		listener->stats.retargeted_forwarded_events += retargeted;
#endif
	}
#if ENABLE_EVENT_STATS
	listener->stats.received_forwarded_events += 1;
#endif

	LEAVE();
}

static void
mm_event_forward_2(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	const uintptr_t events = arguments[2];
	int retargeted = mm_event_forward_handle(context, (struct mm_event_fd *) arguments[0], events & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[1], events >> 4);

	// Flush any events that changed their target.
	struct mm_event_listener *const listener = context->listener;
	if (retargeted) {
		mm_event_forward_flush(&listener->forward, listener->dispatch);
#if ENABLE_EVENT_STATS
		listener->stats.retargeted_forwarded_events += retargeted;
#endif
	}
#if ENABLE_EVENT_STATS
	listener->stats.received_forwarded_events += 2;
#endif

	LEAVE();
}

static void
mm_event_forward_3(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	const uintptr_t events = arguments[3];
	int retargeted = mm_event_forward_handle(context, (struct mm_event_fd *) arguments[0], events & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[2], events >> 8);

	// Flush any events that changed their target.
	struct mm_event_listener *const listener = context->listener;
	if (retargeted) {
		mm_event_forward_flush(&listener->forward, listener->dispatch);
#if ENABLE_EVENT_STATS
		listener->stats.retargeted_forwarded_events += retargeted;
#endif
	}
#if ENABLE_EVENT_STATS
	listener->stats.received_forwarded_events += 3;
#endif

	LEAVE();
}

static void
mm_event_forward_4(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	const uintptr_t events = arguments[4];
	int retargeted = mm_event_forward_handle(context, (struct mm_event_fd *) arguments[0], events & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[3], events >> 12);

	// Flush any events that changed their target.
	struct mm_event_listener *const listener = context->listener;
	if (retargeted) {
		mm_event_forward_flush(&listener->forward, listener->dispatch);
#if ENABLE_EVENT_STATS
		listener->stats.retargeted_forwarded_events += retargeted;
#endif
	}
#if ENABLE_EVENT_STATS
	listener->stats.received_forwarded_events += 4;
#endif

	LEAVE();
}

static void
mm_event_forward_5(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	const uintptr_t events = arguments[5];
	int retargeted = mm_event_forward_handle(context, (struct mm_event_fd *) arguments[0], events & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[3], (events >> 12) & 15);
	retargeted += mm_event_forward_handle(context, (struct mm_event_fd *) arguments[4], events >> 16);

	// Flush any events that changed their target.
	struct mm_event_listener *const listener = context->listener;
	if (retargeted) {
		mm_event_forward_flush(&listener->forward, listener->dispatch);
#if ENABLE_EVENT_STATS
		listener->stats.retargeted_forwarded_events += retargeted;
#endif
	}
#if ENABLE_EVENT_STATS
	listener->stats.received_forwarded_events += 5;
#endif

	LEAVE();
}

/**********************************************************************
 * Event forwarding request posting.
 **********************************************************************/

static void NONNULL(1, 2)
mm_event_forward_post(struct mm_context *context, struct mm_event_forward_buffer *buffer)
{
	ENTER();

	switch (buffer->nsinks) {
	case 0:
		break;
	case 1:
		buffer->nsinks = 0;
		mm_async_call_2(context, mm_event_forward_1,
				(uintptr_t) buffer->sinks[0],
				buffer->events[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_async_call_3(context, mm_event_forward_2,
				(uintptr_t) buffer->sinks[0],
				(uintptr_t) buffer->sinks[1],
				buffer->events[0]
				| (buffer->events[1] << 4));
		break;
	case 3:
		buffer->nsinks = 0;
		mm_async_call_4(context, mm_event_forward_3,
				(uintptr_t) buffer->sinks[0],
				(uintptr_t) buffer->sinks[1],
				(uintptr_t) buffer->sinks[2],
				buffer->events[0]
				| (buffer->events[1] << 4)
				| (buffer->events[2] << 8));
		break;
	case 4:
		buffer->nsinks = 0;
		mm_async_call_5(context, mm_event_forward_4,
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
		mm_async_call_6(context, mm_event_forward_5,
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
mm_event_forward_flush(struct mm_event_forward_cache *cache, struct mm_event_dispatch *dispatch)
{
	ENTER();

	mm_thread_t target = mm_bitset_find(&cache->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_forward_buffer *buffer = &cache->buffers[target];
		mm_event_forward_post(dispatch->listeners[target].context, buffer);
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
mm_event_forward(struct mm_event_forward_cache *cache, struct mm_event_fd *sink, mm_event_index_t event)
{
	ENTER();

	struct mm_event_listener *listener = containerof(cache, struct mm_event_listener, forward);
	struct mm_event_dispatch *dispatch = listener->dispatch;

	struct mm_event_listener *sink_listener = sink->context->listener;
	mm_thread_t target = sink_listener - dispatch->listeners;

	// Flush the buffer if it is full.
	struct mm_event_forward_buffer *buffer = &cache->buffers[target];
	if (buffer->nsinks == MM_EVENT_FORWARD_BUFFER_SIZE)
		mm_event_forward_post(sink->context, buffer);

	// Add the event to the buffer.
	uint32_t n = buffer->nsinks++;
	buffer->sinks[n] = sink;
	buffer->events[n] = event;
	buffer->ntotal++;

	// Account for it.
	mm_bitset_set(&cache->targets, target);

	LEAVE();
}

#endif /* ENABLE_SMP */

