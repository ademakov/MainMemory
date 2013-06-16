/*
 * work.h - MainMemory work queue.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef WORK_H
#define WORK_H

#include "common.h"
#include "list.h"

/* Forward declaration. */
struct mm_core;

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_list queue;

	/* The work is pinned to a specific core. */
	bool pinned;

	/* The work routine. */
	mm_routine_t routine;
	/* The work routine argument. */
	uintptr_t routine_arg;
};

struct mm_work *mm_work_create(mm_routine_t routine,
			       uintptr_t routine_arg,
			       bool pinned)
	__attribute__((nonnull(1)));

void mm_work_destroy(struct mm_work *work)
	__attribute__((nonnull(1)));

void mm_work_recycle(struct mm_work *work)
	__attribute__((nonnull(1)));

void mm_work_put(struct mm_work *work)
	__attribute__((nonnull(1)));

struct mm_work * mm_work_get(void);

static inline void
mm_work_add(mm_routine_t routine,
	    uintptr_t routine_arg,
	    bool pinned)
{
	mm_work_put(mm_work_create(routine, routine_arg, pinned));
}

#endif /* WORK_H */
