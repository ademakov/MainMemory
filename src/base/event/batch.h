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

typedef enum {
	MM_EVENT_REGISTER,
	MM_EVENT_UNREGISTER,
	MM_EVENT_TRIGGER_INPUT,
	MM_EVENT_TRIGGER_OUTPUT,
} mm_event_change_t;

/* Event change data. */
struct mm_event_change
{
	mm_event_change_t kind;
	struct mm_event_fd *sink;
};

/* Event change data batch. */
struct mm_event_batch
{
	struct mm_event_change *changes;
	unsigned int nchanges;
	unsigned int nchanges_max;
};

void NONNULL(1)
mm_event_batch_prepare(struct mm_event_batch *batch, unsigned int size);

void NONNULL(1)
mm_event_batch_cleanup(struct mm_event_batch *batch);

void NONNULL(1)
mm_event_batch_expand(struct mm_event_batch *batch);

static inline void NONNULL(1, 3)
mm_event_batch_add(struct mm_event_batch *batch, mm_event_change_t kind, struct mm_event_fd *sink)
{
	if (unlikely(batch->nchanges == batch->nchanges_max))
		mm_event_batch_expand(batch);

	unsigned int i = batch->nchanges++;
	batch->changes[i].kind = kind;
	batch->changes[i].sink = sink;
}

static inline void NONNULL(1)
mm_event_batch_clear(struct mm_event_batch *batch)
{
	batch->nchanges = 0;
}

static inline bool NONNULL(1)
mm_event_batch_empty(struct mm_event_batch *batch)
{
	return (batch->nchanges == 0);
}

#endif /* BASE_EVENT_BATCH_H */
