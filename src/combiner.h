/*
 * combiner.h - MainMemory synchronization via combining/delegation.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef COMBINER_H
#define COMBINER_H

#include "common.h"
#include "cdata.h"
#include "list.h"
#include "ring.h"

typedef void (*mm_combiner_routine_t)(uintptr_t data);

struct mm_combiner
{
	mm_combiner_routine_t routine __align(MM_CACHELINE);
	size_t handoff;

	/* Per-core wait list of pending requests. */
	MM_CDATA(struct mm_queue, wait_list);

	struct mm_ring_mpmc ring;
};

struct mm_combiner * mm_combiner_create(const char *name,
					mm_combiner_routine_t routine,
					size_t size, size_t handoff);

void mm_combiner_destroy(struct mm_combiner *combiner)
	__attribute__((nonnull(1)));

void mm_combiner_prepare(struct mm_combiner *combiner,
			 const char *name,
			 mm_combiner_routine_t routine,
			 size_t size, size_t handoff)
	__attribute__((nonnull(1)));

bool mm_combiner_combine(struct mm_combiner *combiner)
	__attribute__((nonnull(1)));

void mm_combiner_enqueue(struct mm_combiner *combiner, uintptr_t data, bool wait)
	__attribute__((nonnull(1)));

static inline bool
mm_combiner_trylock(struct mm_combiner *combiner)
{
	return mm_ring_sharedget_trylock(&combiner->ring.base);
}

static inline void
mm_combiner_lock(struct mm_combiner *combiner)
{
	mm_ring_sharedget_lock(&combiner->ring.base);
}

static inline void
mm_combiner_unlock(struct mm_combiner *combiner)
{
	mm_ring_sharedget_unlock(&combiner->ring.base);
}

static inline void
mm_combiner_execute(struct mm_combiner *combiner, uintptr_t data, bool wait)
{
	if (mm_combiner_trylock(combiner)) {
		combiner->routine(data);
		mm_combiner_combine(combiner);
		mm_combiner_unlock(combiner);
	} else {
		mm_combiner_enqueue(combiner, data, wait);
	}
}

#endif /* COMBINER_H */
