/*
 * base/event/receiver.c - MainMemory event receiver.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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

#include "base/event/receiver.h"

#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

/**********************************************************************
 * Event forwarding request handlers.
 **********************************************************************/

static void
mm_event_receiver_forward_1(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[1];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);

	LEAVE();
}

static void
mm_event_receiver_forward_2(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[2];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], events >> 4);

	LEAVE();
}

static void
mm_event_receiver_forward_3(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[3];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], events >> 8);

	LEAVE();
}

static void
mm_event_receiver_forward_4(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[4];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[3], events >> 12);

	LEAVE();
}

static void
mm_event_receiver_forward_5(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[5];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[3], (events >> 12) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[4], events >> 16);

	LEAVE();
}

/**********************************************************************
 * Event forwarding.
 **********************************************************************/

static void
mm_event_receiver_fwdbuf_prepare(struct mm_event_receiver_fwdbuf *buffer)
{
	buffer->nsinks = 0;
	buffer->ntotal = 0;
}

void
mm_event_receiver_forward_flush(struct mm_thread *thread, struct mm_event_receiver_fwdbuf *buffer)
{
	ENTER();

	switch (buffer->nsinks) {
	case 0:
		break;
	case 1:
		buffer->nsinks = 0;
		mm_thread_post_2(thread, mm_event_receiver_forward_1,
				 (uintptr_t) buffer->sinks[0],
				 buffer->events[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_thread_post_3(thread, mm_event_receiver_forward_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 buffer->events[0]
				 | (buffer->events[1] << 4));
		break;
	case 3:
		buffer->nsinks = 0;
		mm_thread_post_4(thread, mm_event_receiver_forward_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8));
		break;
	case 4:
		buffer->nsinks = 0;
		mm_thread_post_5(thread, mm_event_receiver_forward_4,
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
		mm_thread_post_6(thread, mm_event_receiver_forward_5,
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

void
mm_event_receiver_forward(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();

	mm_thread_t target = sink->target;
	struct mm_event_receiver_fwdbuf *buffer = &receiver->forward_buffers[target];

	// Flush the buffer if it is full.
	if (buffer->nsinks == MM_EVENT_RECEIVER_FWDBUF_SIZE) {
		struct mm_event_dispatch *dispatch = receiver->dispatch;
		struct mm_event_listener *listener = &dispatch->listeners[target];
		mm_event_receiver_forward_flush(listener->thread, buffer);
	}

	// Add the event to the buffer.
	unsigned int n = buffer->nsinks++;
	buffer->sinks[n] = sink;
	buffer->events[n] = event;
	buffer->ntotal++;

	// Account for it.
	mm_bitset_set(&receiver->forward_targets, target);

	LEAVE();
}

/**********************************************************************
 * Event receiver.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_event_dispatch *dispatch)
{
	ENTER();

	// Remember the owners.
	receiver->dispatch = dispatch;

	// Prepare forward buffers.
	receiver->forward_buffers = mm_common_calloc(dispatch->nlisteners,
						     sizeof(struct mm_event_receiver_fwdbuf));
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_receiver_fwdbuf_prepare(&receiver->forward_buffers[i]);
	mm_bitset_prepare(&receiver->forward_targets, &mm_common_space.xarena,
			  dispatch->nlisteners);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	// Release forward buffers.
	mm_common_free(receiver->forward_buffers);
	mm_bitset_cleanup(&receiver->forward_targets, &mm_common_space.xarena);

	LEAVE();
}
