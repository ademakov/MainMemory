/*
 * base/event/receiver.h - MainMemory event receiver.
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

#ifndef BASE_EVENT_RECEIVER_H
#define BASE_EVENT_RECEIVER_H

#include "common.h"
#include "base/bitset.h"
#include "base/list.h"
#include "base/event/event.h"

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_thread;

#define MM_EVENT_RECEIVER_FWDBUF_SIZE	(5)

/* Event sink forward buffer. */
struct mm_event_receiver_fwdbuf
{
	struct mm_event_fd *sinks[MM_EVENT_RECEIVER_FWDBUF_SIZE];
	mm_event_t events[MM_EVENT_RECEIVER_FWDBUF_SIZE];
	uint8_t nsinks;
	uint8_t ntotal;
};

struct mm_event_receiver
{
	/* The top-level event dispatch data. */
	struct mm_event_dispatch *dispatch;

	/* Target threads that have received events. */
	struct mm_bitset forward_targets;

	/* Per-thread temporary store for sinks of received events. */
	struct mm_event_receiver_fwdbuf *forward_buffers;
};

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_event_dispatch *dispatch);

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver);

void
mm_event_receiver_forward(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event);

void
mm_event_receiver_forward_flush(struct mm_thread *thread, struct mm_event_receiver_fwdbuf *buffer);

#endif /* BASE_EVENT_RECEIVER_H */
