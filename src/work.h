/*
 * work.h - MainMemory work queue.
 *
 * Copyright (C) 2013  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
	/* Work items flags. */
	mm_task_flags_t flags;
	/* Work items count. */
	uint32_t count;
	/* The work routine. */
	mm_routine routine;
	/* Work items array. */
	uintptr_t items[];
};


void mm_work_init(void);
void mm_work_term(void);

struct mm_work * mm_work_create(uint32_t count);
void mm_work_destroy(struct mm_work *work);

struct mm_work * mm_work_get(void);
void mm_work_put(struct mm_work *work);

#endif /* WORK_H */
