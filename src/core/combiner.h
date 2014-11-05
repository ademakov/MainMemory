/*
 * core/combiner.h - MainMemory task combining synchronization.
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

#ifndef CORE_COMBINER_H
#define CORE_COMBINER_H

#include "common.h"
#include "base/combiner.h"
#include "base/list.h"
#include "base/mem/cdata.h"

struct mm_task_combiner
{
	/* Per-core wait list of pending requests. */
	MM_CDATA(struct mm_list, wait_queue);

	struct mm_combiner combiner;
};

struct mm_task_combiner * mm_task_combiner_create(const char *name,
						  mm_combiner_routine_t routine,
						  size_t size, size_t handoff);

void mm_task_combiner_destroy(struct mm_task_combiner *combiner)
	__attribute__((nonnull(1)));

void mm_task_combiner_prepare(struct mm_task_combiner *combiner,
			      const char *name,
			      mm_combiner_routine_t routine,
			      size_t size, size_t handoff)
	__attribute__((nonnull(1)));

void mm_task_combiner_execute(struct mm_task_combiner *combiner, uintptr_t data)
	__attribute__((nonnull(1)));

#endif /* CORE_COMBINER_H */
