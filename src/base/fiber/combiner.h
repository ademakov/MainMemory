/*
 * base/fiber/combiner.h - MainMemory fiber combining synchronization.
 *
 * Copyright (C) 2014-2017  Aleksey Demakov
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

#ifndef BASE_FIBER_COMBINER_H
#define BASE_FIBER_COMBINER_H

#include "common.h"
#include "base/combiner.h"
#include "base/list.h"
#include "base/thread/local.h"

struct mm_fiber_combiner
{
	/* Per-thread wait list of pending requests. */
	MM_THREAD_LOCAL(struct mm_list, wait_queue);

	struct mm_combiner combiner;
};

struct mm_fiber_combiner *
mm_fiber_combiner_create(const char *name, size_t size, size_t handoff);

void NONNULL(1)
mm_fiber_combiner_destroy(struct mm_fiber_combiner *combiner);

void NONNULL(1)
mm_fiber_combiner_prepare(struct mm_fiber_combiner *combiner, const char *name,
			  size_t size, size_t handoff);

void NONNULL(1, 2)
mm_fiber_combiner_execute(struct mm_fiber_combiner *combiner,
			  mm_combiner_routine_t routine, uintptr_t data);

#endif /* BASE_FIBER_COMBINER_H */
