/*
 * core/work.h - MainMemory work items.
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

#ifndef CORE_WORK_H
#define CORE_WORK_H

#include "common.h"
#include "arch/atomic.h"
#include "base/list.h"
#include "core/core.h"

/* Completion notification routine for work items.  */
typedef void (*mm_work_complete_t)(struct mm_work *work, mm_value_t result);

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_qlink link;

	/* The work routine. */
	mm_routine_t routine;
	/* The work routine argument. */
	mm_value_t argument;

	/* The work completion routine. */
	mm_work_complete_t complete;
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
	return mm_work_create_low(mm_core_self());
}

static inline void
mm_work_destroy(struct mm_work *work)
{
	mm_work_destroy_low(mm_core_self(), work);
}

/**********************************************************************
 * Work item initialization.
 **********************************************************************/

void mm_work_complete_noop(struct mm_work *work, mm_value_t result);

static inline void
mm_work_prepare(struct mm_work *work,
		mm_routine_t routine,
		mm_value_t argument,
		mm_work_complete_t complete)
{
	work->routine = routine;
	work->argument = argument;
	work->complete = complete;
}

#endif /* CORE_WORK_H */
