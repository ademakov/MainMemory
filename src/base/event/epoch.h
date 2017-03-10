/*
 * base/event/epoch.h - MainMemory event sink reclamation epochs.
 *
 * Copyright (C) 2016-2017  Aleksey Demakov
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

#ifndef BASE_EVENT_EPOCH_H
#define BASE_EVENT_EPOCH_H

#include "common.h"
#include "arch/atomic.h"
#include "base/report.h"
#include "base/event/event.h"

/* Event sink reclamation epoch. A valid epoch is never zero. */
typedef mm_atomic_uint32_t mm_event_epoch_t;
typedef uint32_t mm_event_epoch_snapshot_t;

/* Local (per-thread) epoch. */
struct mm_event_epoch_local
{
	/* A snapshot of the global epoch or zero if not active. */
	mm_event_epoch_snapshot_t epoch;

	/* Number of event sinks put aside for reclamation. */
	uint16_t count;

	/* The next listener to check while advancing the epoch. */
	mm_thread_t index;

	/* Event sinks retired within the ongoing critical section. */
	struct mm_queue queue;

	/* Event sinks put aside for reclamation at coming epochs. */
	struct mm_stack limbo[2];
};

void NONNULL(1)
mm_event_epoch_prepare(mm_event_epoch_t *global);

void NONNULL(1)
mm_event_epoch_prepare_local(struct mm_event_epoch_local *local);

void NONNULL(1, 2)
mm_event_epoch_advance(struct mm_event_epoch_local *local, mm_event_epoch_t *global);

/* Announce a reclamation critical section start. */
static inline void NONNULL(1, 2)
mm_event_epoch_enter(struct mm_event_epoch_local *local, mm_event_epoch_t *global)
{
	/* The store operation here is not atomic. It should be followed by
	   a system call such as kqueue() or epoll_ctl() that will serve as
	   a memory fence. The epoch value set here might become obsolete by
	   then. But this is compensated in mm_event_epoch_leave(). */
	if (local->epoch == 0) {
		mm_event_epoch_snapshot_t epoch = mm_memory_load(*global);
		mm_memory_fence(); // TODO: load_store fence
		mm_memory_store(local->epoch, epoch);
		local->index = 0;
	}
}

/* Conclude the current reclamation critical section. */
static inline void NONNULL(1, 2)
mm_event_epoch_leave(struct mm_event_epoch_local *local, mm_event_epoch_t *global)
{
	if (local->count == 0) {
		/* Finish the critical section. */
		mm_memory_store(local->epoch, 0);
	} else {
		/* Try to advance the epoch and reclaim some sinks. */
		mm_event_epoch_advance(local, global);
	}
}

/* Queue an event sink for reclamation. */
static inline void NONNULL(1, 2)
mm_event_epoch_retire(struct mm_event_epoch_local *local, struct mm_event_fd *sink)
{
	ASSERT((local->epoch & 1) != 0);
	VERIFY(local->count < UINT16_MAX);
	mm_queue_append(&local->queue, &sink->retire_link);
	local->count++;
}

/* Check if there any pending event sinks. */
static inline bool NONNULL(1)
mm_event_epoch_active(struct mm_event_epoch_local *local)
{
	return local->count != 0;
}

#endif /* BASE_EVENT_EPOCH_H */
