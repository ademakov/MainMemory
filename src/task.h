/*
 * task.h - MainMemory tasks.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef TASK_H
#define TASK_H

#include "common.h"
#include "arch.h"
#include "list.h"

/* Canceled task execution result. */
#define MM_TASK_CANCELED	((mm_result_t) -1)
/* Unfinished task execution result. */
#define MM_TASK_UNRESOLVED	((mm_result_t) 0xDEADC0DE)

/* Task state values. */
typedef enum {
	MM_TASK_PENDING,
	MM_TASK_RUNNING,
	MM_TASK_BLOCKED,
	MM_TASK_CREATED,
	MM_TASK_INVALID,
} mm_task_state_t;

/* Task flags. */
#define MM_TASK_CANCEL_ENABLE		0x00
#define MM_TASK_CANCEL_DISABLE		0x01
#define MM_TASK_CANCEL_DEFERRED		0x00
#define MM_TASK_CANCEL_ASYNCHRONOUS	0x02
#define MM_TASK_CANCEL_REQUIRED		0x04
#define MM_TASK_CANCEL_OCCURRED		0x08
#define MM_TASK_READING			0x10
#define MM_TASK_WRITING			0x20
#define MM_TASK_WAITING			0x40

/* Task flags type. */
typedef uint8_t mm_task_flags_t;


/* A task cleanup handler record. */
struct mm_task_cleanup_rec
{
	struct mm_task_cleanup_rec *next;
	void (*const routine)(uintptr_t arg);
	uintptr_t routine_arg;
};

/* Register a cleanup handler. */
#define mm_task_cleanup_push(rtn, arg)					\
	do {								\
		struct mm_task_cleanup_rec __cleanup = {		\
				.next = mm_running_task->cleanup,	\
				.routine = (void (*)(uintptr_t)) (rtn),	\
				.routine_arg = (uintptr_t) (arg),	\
			};						\
		do {

/* Unregister a cleanup handler optionally executing it. */
#define mm_task_cleanup_pop(execute)					\
		} while (0);						\
		if (execute) {						\
			mm_running_task->cleanup = __cleanup.next;	\
			__cleanup.routine(__cleanup.routine_arg);	\
		}							\
	} while (0)


/* A user-space (green) thread. */
struct mm_task
{
	/* A link in a run/wait/dead queue. */
	struct mm_list queue;

	/* The task status. */
	mm_task_state_t state;
	mm_task_flags_t flags;

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

	/* The list of task cleanup records. */
	struct mm_task_cleanup_rec *cleanup;

	/* The list of task-local dynamically-allocated memory. */
	struct mm_list chunks;

	/* The task result. */
	mm_result_t result;

	/* The task name. */
	char *name;

#if ENABLE_TRACE
	int trace_level;
#endif
};

void mm_task_init(void);
void mm_task_term(void);

struct mm_task * mm_task_create(const char *name, mm_task_flags_t flags,
				mm_routine start, uintptr_t start_arg)
	__attribute__((nonnull(1, 3)));

void mm_task_destroy(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_recycle(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_exit(mm_result_t result)
	__attribute__((noreturn));

void mm_task_set_name(struct mm_task *task, const char *name)
	__attribute__((nonnull(1, 2)));

void mm_task_testcancel(void);
void mm_task_testcancel_asynchronous(void);

void mm_task_setcancelstate(int new_value, int *old_value_ptr);
void mm_task_setcanceltype(int new_value, int *old_value_ptr);

int mm_task_enter_cancel_point(void);
void mm_task_leave_cancel_point(int);

void mm_task_cancel(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_wait_fifo(struct mm_list *queue);
void mm_task_wait_lifo(struct mm_list *queue);
void mm_task_signal(struct mm_list *queue);
void mm_task_broadcast(struct mm_list *queue);

void * mm_task_alloc(size_t size)
	__attribute__((malloc));

void mm_task_free(void *ptr);

#endif /* TASK_H */
