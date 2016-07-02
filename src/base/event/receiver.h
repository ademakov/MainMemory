/*
 * base/event/receiver.h - MainMemory event receiver.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

/* Forward declarations. */
struct mm_event_dispatch;

#define MM_EVENT_RECEIVER_FWDBUF_SIZE	6
#define MM_EVENT_RECEIVER_PUBBUF_SIZE	4

/* Event sink forward buffer. */
struct mm_event_receiver_fwdbuf
{
	struct mm_event_fd *sinks[MM_EVENT_RECEIVER_FWDBUF_SIZE];
	unsigned int nsinks;
};

/* Event sink publish buffer. */
struct mm_event_receiver_pubbuf
{
	struct mm_event_fd *sinks[MM_EVENT_RECEIVER_PUBBUF_SIZE];
	unsigned int nsinks;
};

/* Event receiver statistics. */
struct mm_event_receiver_stats
{
	uint64_t loose_events;
	uint64_t direct_events;
	uint64_t stolen_events;
	uint64_t forwarded_events;
	uint64_t published_events;
};

struct mm_event_receiver
{
	/* A local snapshot of the event sink reclamation epoch. */
	uint32_t reclaim_epoch;
	bool reclaim_active;

	/* The flag indicating that some events were received. */
	bool got_events;

	/* The thread that owns the receiver. */
	mm_thread_t thread;

	/* The top-level event dispatch data. */
	struct mm_event_dispatch *dispatch;

	/* Target threads that have received events. */
	struct mm_bitset forward_targets;

	/* Per-thread temporary store for sinks of received events. */
	struct mm_event_receiver_fwdbuf *forward_buffers;

	/* Per-domain temporary store for sinks of received events. */
	struct mm_event_receiver_pubbuf publish_buffer;

	/* The number of directly handled events. */
	uint32_t direct_events;
	uint32_t stolen_events;
	/* The number of events forwarded to other listeners. */
	uint32_t forwarded_events;
	/* The number of events published in the domain request queue. */
	uint32_t published_events;

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
mm_event_receiver_start(struct mm_event_receiver *receiver);
void NONNULL(1)
mm_event_receiver_finish(struct mm_event_receiver *receiver);

void NONNULL(1, 2)
mm_event_receiver_input(struct mm_event_receiver *receiver, struct mm_event_fd *sink);
void NONNULL(1, 2)
mm_event_receiver_input_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink);
void NONNULL(1, 2)
mm_event_receiver_output(struct mm_event_receiver *receiver, struct mm_event_fd *sink);
void NONNULL(1, 2)
mm_event_receiver_output_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink);
void NONNULL(1, 2)
mm_event_receiver_unregister(struct mm_event_receiver *receiver, struct mm_event_fd *sink);

#endif /* BASE_EVENT_RECEIVER_H */
