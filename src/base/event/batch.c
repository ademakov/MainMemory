/*
 * base/event/batch.c - MainMemory event batch.
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

#include "base/event/batch.h"

#include "base/log/error.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"

#define MM_EVENT_BATCH_NEVENTS_MIN	(4u)
#define MM_EVENT_BATCH_NEVENTS_MAX	(16u * 1024u)

void __attribute__((nonnull(1)))
mm_event_batch_prepare(struct mm_event_batch *batch, unsigned int size)
{
	ENTER();

	batch->flags = 0;
	batch->nevents = 0;
	batch->nevents_max = max(size, MM_EVENT_BATCH_NEVENTS_MIN);
	batch->events = mm_common_alloc(batch->nevents_max * sizeof(struct mm_event));

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_batch_cleanup(struct mm_event_batch *batch)
{
	ENTER();

	mm_common_free(batch->events);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_batch_expand(struct mm_event_batch *batch)
{
	ENTER();

	if (unlikely(batch->nevents_max == MM_EVENT_BATCH_NEVENTS_MAX))
		mm_fatal(0, "too many events");

	batch->nevents_max *= 2;
	batch->events = mm_common_realloc(batch->events,
					  batch->nevents_max * sizeof(struct mm_event));

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_event_batch_append(struct mm_event_batch *restrict batch,
		      const struct mm_event_batch *restrict batch2)
{
	ENTER();

	mm_event_batch_addflags(batch, batch2->flags);

	for (unsigned int i = 0; i < batch2->nevents; i++) {
		struct mm_event *event = &batch2->events[i];
		mm_event_batch_add(batch, event->event, event->ev_fd);
	}

	LEAVE();
}
