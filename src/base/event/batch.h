/*
 * base/event/batch.h - MainMemory event batch.
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

#ifndef BASE_EVENT_BATCH_H
#define BASE_EVENT_BATCH_H

#include "common.h"
#include "base/event/event.h"

#define MM_EVENT_BATCH_REGISTER		((unsigned int) 1)
#define MM_EVENT_BATCH_UNREGISTER	((unsigned int) 2)
#define MM_EVENT_BATCH_INPUT_OUTPUT	((unsigned int) 4)

struct mm_event_batch
{
	unsigned int flags;
	unsigned int nevents;
	unsigned int nevents_max;
	struct mm_event *events;
};

void NONNULL(1)
mm_event_batch_prepare(struct mm_event_batch *batch, unsigned int size);

void NONNULL(1)
mm_event_batch_cleanup(struct mm_event_batch *batch);

void NONNULL(1)
mm_event_batch_expand(struct mm_event_batch *batch);

void NONNULL(1, 2)
mm_event_batch_append(struct mm_event_batch *restrict batch,
		      const struct mm_event_batch *restrict batch2);

static inline void NONNULL(1)
mm_event_batch_addflags(struct mm_event_batch *batch, unsigned int flags)
{
	batch->flags |= flags;
}

static inline bool NONNULL(1)
mm_event_batch_hasflags(struct mm_event_batch *batch, unsigned int flags)
{
	return (batch->flags & flags) != 0;
}

static inline void NONNULL(1)
mm_event_batch_add(struct mm_event_batch *batch, mm_event_t event,
		   struct mm_event_fd *ev_fd)
{
	if (unlikely(batch->nevents == batch->nevents_max))
		mm_event_batch_expand(batch);

	batch->events[batch->nevents].event = event;
	batch->events[batch->nevents].ev_fd = ev_fd;
	batch->nevents++;
}

static inline void NONNULL(1)
mm_event_batch_clear(struct mm_event_batch *batch)
{
	batch->flags = 0;
	batch->nevents = 0;
}

static inline bool NONNULL(1)
mm_event_batch_empty(struct mm_event_batch *batch)
{
	return (batch->nevents == 0);
}

#endif /* BASE_EVENT_BATCH_H */
