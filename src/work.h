/*
 * work.h - MainMemory work items.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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
#include "core.h"
#include "list.h"

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_link link;

	/* The work is pinned to a specific core. */
	bool pinned;

	/* The work routine. */
	mm_routine_t routine;
	/* The work routine argument. */
	mm_value_t routine_arg;

	/* The work result. */
	mm_value_t result;
};

/**********************************************************************
 * Work item module initialization.
 **********************************************************************/

void mm_work_init(void);

/**********************************************************************
 * Work item creation and destruction.
 **********************************************************************/

struct mm_work *mm_work_create_low(mm_core_t core);

void mm_work_destroy_low(mm_core_t, struct mm_work *work)
	__attribute__((nonnull(2)));

static inline struct mm_work *
mm_work_create(void)
{
	return mm_work_create_low(mm_core_selfid());
}

static inline void
mm_work_destroy(struct mm_work *work)
{
	mm_work_destroy_low(mm_core_selfid(), work);
}

/**********************************************************************
 * Work item initialization.
 **********************************************************************/

static inline void
mm_work_prepare(struct mm_work *work, bool pinned,
		mm_routine_t routine, mm_value_t routine_arg, mm_value_t result)
{
	work->pinned = pinned;
	work->routine = routine;
	work->routine_arg = routine_arg;
	work->result = result;
}

#endif /* WORK_H */
