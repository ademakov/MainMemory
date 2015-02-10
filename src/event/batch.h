/*
 * event/batch.h - MainMemory event batch.
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

#ifndef EVENT_BATCH_H
#define EVENT_BATCH_H

#include "common.h"
#include "event/event.h"

#define MM_EVENT_BATCH_REGISTER		((unsigned int) 1)
#define MM_EVENT_BATCH_UNREGISTER	((unsigned int) 2)

struct mm_event_batch
{
	unsigned int flags;
	unsigned int nevents;
	unsigned int nevents_max;
	struct mm_event *events;
};

void __attribute__((nonnull(1)))
mm_event_batch_prepare(struct mm_event_batch *batch);

void __attribute__((nonnull(1)))
mm_event_batch_cleanup(struct mm_event_batch *batch);

void __attribute__((nonnull(1)))
mm_event_batch_expand(struct mm_event_batch *batch);

void __attribute__((nonnull(1, 2)))
mm_event_batch_append(struct mm_event_batch *batch, struct mm_event_batch *batch2);

static inline void __attribute__((nonnull(1)))
mm_event_batch_addflags(struct mm_event_batch *batch, unsigned int flags)
{
	batch->flags |= flags;
}

static inline bool __attribute__((nonnull(1)))
mm_event_batch_hasflags(struct mm_event_batch *batch, unsigned int flags)
{
	return (batch->flags & flags) != 0;
}

static inline void __attribute__((nonnull(1)))
mm_event_batch_add(struct mm_event_batch *batch, mm_event_t event, struct mm_event_fd *ev_fd)
{
	if (unlikely(batch->nevents == batch->nevents_max))
		mm_event_batch_expand(batch);

	batch->events[batch->nevents].event = event;
	batch->events[batch->nevents].ev_fd = ev_fd;
	batch->nevents++;
}

static inline void __attribute__((nonnull(1)))
mm_event_batch_clear(struct mm_event_batch *batch)
{
	batch->flags = 0;
	batch->nevents = 0;
}

#endif /* EVENT_BATCH_H */
