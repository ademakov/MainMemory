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
#include "base/list.h"
#include "core/core.h"

/* A work routine. */
typedef mm_value_t (*mm_work_routine_t)(struct mm_work *work);

/* A work completion notification routine. */
typedef void (*mm_work_complete_t)(struct mm_work *work, mm_value_t result);

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_qlink link;
	/* The work routine. */
	mm_work_routine_t routine;
	/* The work completion routine. */
	mm_work_complete_t complete;
};

/**********************************************************************
 * Work item initialization.
 **********************************************************************/

void mm_work_complete_noop(struct mm_work *work, mm_value_t result);

static inline void NONNULL(1, 2)
mm_work_prepare(struct mm_work *work, mm_work_routine_t routine, mm_work_complete_t complete)
{
	work->routine = routine;
	work->complete = complete != NULL ? complete : mm_work_complete_noop;
}

#endif /* CORE_WORK_H */
