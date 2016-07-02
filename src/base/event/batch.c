/*
 * base/event/batch.c - MainMemory event batch.
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

#include "base/event/batch.h"

#include "base/report.h"
#include "base/memory/memory.h"

#define MM_EVENT_NCHANGES_MIN	(4u)
#define MM_EVENT_NCHANGES_MAX	(16u * 1024u)

void NONNULL(1)
mm_event_batch_prepare(struct mm_event_batch *batch, unsigned int size)
{
	ENTER();

	batch->nchanges = 0;
	batch->nchanges_max = max(size, MM_EVENT_NCHANGES_MIN);
	batch->changes = mm_common_alloc(batch->nchanges_max * sizeof(struct mm_event_change));

	LEAVE();
}

void NONNULL(1)
mm_event_batch_cleanup(struct mm_event_batch *batch)
{
	ENTER();

	mm_common_free(batch->changes);

	LEAVE();
}

void NONNULL(1)
mm_event_batch_expand(struct mm_event_batch *batch)
{
	ENTER();

	if (unlikely(batch->nchanges_max == MM_EVENT_NCHANGES_MAX))
		mm_fatal(0, "too many event change entries");

	batch->nchanges_max *= 2;
	batch->changes = mm_common_realloc(batch->changes,
					   batch->nchanges_max * sizeof(struct mm_event_change));

	LEAVE();
}
