/*
 * task.c - MainMemory tasks.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#include "task.h"

#include "util.h"

void
mm_task_init(void)
{
	ENTER();

	LEAVE();
}

void
mm_task_free(void)
{
	ENTER();

	LEAVE();
}

struct mm_task *
mm_task_create(uint16_t flags, mm_routine routine, uintptr_t routine_arg)
{
	ENTER();

	LEAVE();
	return NULL;
}

void
mm_task_start(struct mm_task *task)
{
	ENTER();

	LEAVE();
}

void
mm_task_block(struct mm_task *task)
{
	ENTER();

	LEAVE();
}
