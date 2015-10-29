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
#include "base/event/event.h"
#include "base/event/listener.h"

struct mm_event_receiver
{
	/* The flag indicating that some events were received. */
	bool got_events;

	/* The thread that currently owns the receiver. */
	mm_thread_t control_thread;

	/* A counter to detect event sink detach feasibility. */
	uint32_t arrival_stamp;

	/* Event listeners. */
	struct mm_event_listener *listeners;
	mm_thread_t nlisteners;

	/* Target threads that have received events. */
	struct mm_bitset targets;
};

void __attribute__((nonnull(1, 3)))
mm_event_receiver_prepare(struct mm_event_receiver *receiver,
			  mm_thread_t nthreads,
			  struct mm_thread *threads[]);

void __attribute__((nonnull(1)))
mm_event_receiver_cleanup(struct mm_event_receiver *receiver);

static inline void __attribute__((nonnull(1)))
mm_event_receiver_start(struct mm_event_receiver *receiver)
{
	receiver->arrival_stamp++;
}

void __attribute__((nonnull(1, 3)))
mm_event_receiver_listen(struct mm_event_receiver *receiver,
			 mm_thread_t thread,
			 struct mm_event_backend *backend,
			 mm_timeout_t timeout);

void __attribute__((nonnull(1, 3)))
mm_event_receiver_add(struct mm_event_receiver *receiver,
		      mm_event_t event, struct mm_event_fd *sink);

#endif /* BASE_EVENT_RECEIVER_H */
