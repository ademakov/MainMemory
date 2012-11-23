/*
 * task.h - MainMemory tasks.
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

#ifndef TASK_H
#define TASK_H

#include "common.h"
#include "list.h"

/* Task state values. */
#define MM_TASK_PENDING		0
#define MM_TASK_RUNNING		1
#define MM_TASK_BLOCKED		2
#define MM_TASK_INVALID		3

/* Task flags. */
#define MM_TASK_RUNNABLE	1

typedef void (*mm_routine)(uintptr_t arg);

struct mm_task
{
	uint16_t state;
	uint16_t flags;
	uint32_t reserved;

	/* A link in a run/wait queue. */
	struct mm_list queue;

	/* The list of task's ports. */
	struct mm_list ports;

	mm_routine start;
	uintptr_t start_arg;
};

void mm_task_init(void);
void mm_task_free(void);

struct mm_task * mm_task_create(uint16_t flags, mm_routine start, uintptr_t start_arg)
	__attribute__((nonnull(2)));

void mm_task_destroy(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_start(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_block(struct mm_task *task)
	__attribute__((nonnull(1)));

#endif /* TASK_H */
