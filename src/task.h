/*
 * task.h - MainMemory tasks.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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
#include "arch.h"
#include "list.h"

/* Task state values. */
typedef enum {
	MM_TASK_PENDING,
	MM_TASK_RUNNING,
	MM_TASK_BLOCKED,
	MM_TASK_CREATED,
	MM_TASK_INVALID,
} mm_task_state_t;

/* Task flags. */
#define MM_TASK_CANCELLABLE	1
#define MM_TASK_CANCELLED	2

/* A user-space (green) thread. */
struct mm_task
{
	/* A link in a run/wait/dead queue. */
	struct mm_list queue;

	/* The task status. */
	uint8_t state;
	uint8_t flags;

	/* Task scheduling priority. */
	uint8_t priority;

	/* The task stack. */
	uint32_t stack_size;
	void *stack_base;
	mm_stack_ctx_t stack_ctx;

	/* The list of task's ports. */
	struct mm_list ports;

	/* The port the task is blocked on. */
	struct mm_port *blocked_on;

	/* The task start routine and its argument. */
	mm_routine start;
	uintptr_t start_arg;

	/* The list of task-local dynamically-allocated memory. */
	struct mm_list chunks;

	/* The task name. */
	char *name;

#if ENABLE_TRACE
	int trace_level;
#endif
};

void mm_task_init(void);
void mm_task_term(void);

struct mm_task * mm_task_create(const char *name, uint16_t flags,
				mm_routine start, uintptr_t start_arg)
	__attribute__((nonnull(1, 3)));

void mm_task_set_name(struct mm_task *task, const char *name)
	__attribute__((nonnull(1, 2)));

void mm_task_destroy(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_exit(int status)
	__attribute__((noreturn));

void * mm_task_alloc(size_t size)
	__attribute__((malloc));

void mm_task_free(void *ptr);

#endif /* TASK_H */
