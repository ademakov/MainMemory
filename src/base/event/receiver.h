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
#include "base/event/backend.h"
#include "base/event/event.h"

/* Forward declarations. */
struct mm_event_dispatch;

#define MM_EVENT_RECEIVER_FWDBUF_SIZE	(5)
#define MM_EVENT_RECEIVER_RETAIN_MIN	(3)
#define MM_EVENT_RECEIVER_RETAIN_MAX	(6)
#define MM_EVENT_RECEIVER_FORWARD_MAX	MM_EVENT_RECEIVER_FWDBUF_SIZE

/* Event sink forward buffer. */
struct mm_event_receiver_fwdbuf
{
	struct mm_event_fd *sinks[MM_EVENT_RECEIVER_FWDBUF_SIZE];
	mm_event_t events[MM_EVENT_RECEIVER_FWDBUF_SIZE];
	uint8_t nsinks;
	uint8_t ntotal;
};

/* Event receiver statistics. */
struct mm_event_receiver_stats
{
	uint64_t loose_events;
	uint64_t direct_events;
	uint64_t enqueued_events;
	uint64_t dequeued_events;
	uint64_t forwarded_events;
};

struct mm_event_receiver
{
	/* A local snapshot of the event sink reclamation epoch. */
	uint32_t reclaim_epoch;
	bool reclaim_active;

	/* The thread that owns the receiver. */
	mm_thread_t thread;

	/* The number of locally handled events found while adjusting
	   the receiver for appropriate dispatch strategy. */
	uint16_t direct_events_estimate;

	/* The number of directly handled events. */
	uint16_t direct_events;
	/* The number of events published in the sink queue. */
	uint16_t enqueued_events;
	uint16_t dequeued_events;
	/* The number of events forwarded to other listeners. */
	uint16_t forwarded_events;

	/* The top-level event dispatch data. */
	struct mm_event_dispatch *dispatch;

	/* Target threads that have received events. */
	struct mm_bitset forward_targets;

	/* Per-thread temporary store for sinks of received events. */
	struct mm_event_receiver_fwdbuf *forward_buffers;

	/* Event statistics. */
	struct mm_event_receiver_stats stats;

	/* Event sinks with delayed reclamation. */
	struct mm_stack reclaim_queue[2];

};

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_event_dispatch *dispatch,
			  mm_thread_t thread);

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver);

void NONNULL(1)
mm_event_receiver_observe_epoch(struct mm_event_receiver *receiver);

void NONNULL(1)
mm_event_receiver_poll_start(struct mm_event_receiver *receiver);
void NONNULL(1)
mm_event_receiver_poll_finish(struct mm_event_receiver *receiver);

void NONNULL(1)
mm_event_receiver_dispatch_start(struct mm_event_receiver *receiver, uint32_t nevents);
void NONNULL(1)
mm_event_receiver_dispatch_finish(struct mm_event_receiver *receiver);

void NONNULL(1, 2)
mm_event_receiver_dispatch(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event);

void NONNULL(1, 2)
mm_event_receiver_unregister(struct mm_event_receiver *receiver, struct mm_event_fd *sink);

static inline bool NONNULL(1, 2)
mm_event_receiver_adjust(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	if (!sink->loose_target && mm_event_target(sink) == receiver->thread)
		receiver->direct_events_estimate++;
	return receiver->direct_events_estimate < MM_EVENT_RECEIVER_RETAIN_MIN;
}

static inline void NONNULL(1, 2)
mm_event_receiver_input(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	mm_event_receiver_dispatch(receiver, sink, MM_EVENT_INPUT);
}

static inline void NONNULL(1, 2)
mm_event_receiver_input_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	mm_event_receiver_dispatch(receiver, sink, MM_EVENT_INPUT_ERROR);
}

static inline void NONNULL(1, 2)
mm_event_receiver_output(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	mm_event_receiver_dispatch(receiver, sink, MM_EVENT_OUTPUT);
}

static inline void NONNULL(1, 2)
mm_event_receiver_output_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	mm_event_receiver_dispatch(receiver, sink, MM_EVENT_OUTPUT_ERROR);
}

static inline bool NONNULL(1)
mm_event_receiver_got_events(struct mm_event_receiver *receiver)
{
	return receiver->direct_events || receiver->enqueued_events || receiver->forwarded_events;
}

#endif /* BASE_EVENT_RECEIVER_H */
