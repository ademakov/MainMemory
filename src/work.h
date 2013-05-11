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
#include "task.h"


/* A batch of work. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_list queue;
	/* The work task flags. */
	mm_task_flags_t flags;
	/* The work routine. */
	mm_routine_t routine;
	/* The work item. */
	uintptr_t item;
};


void mm_work_init(void);
void mm_work_term(void);

struct mm_work * mm_work_create(mm_task_flags_t flags, mm_routine_t routine, uintptr_t item);
void mm_work_destroy(struct mm_work *work);

struct mm_work * mm_work_get(void);

void mm_work_put(struct mm_work *work);

void mm_work_add(mm_task_flags_t flags, mm_routine_t routine, uintptr_t item);

void mm_work_addv(mm_task_flags_t flags, mm_routine_t routine, uintptr_t *items, size_t nitems);

#endif /* WORK_H */
