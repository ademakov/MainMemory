/*
 * task.h - MainMemory tasks.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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
#include "arch/stack.h"
#include "core.h"
#include "list.h"
#include "value.h"

/* Maximal task name length (including terminating zero). */
#define MM_TASK_NAME_SIZE	40

/* Task priority type. */
typedef int8_t			mm_priority_t;

/* The lowest and highest allowed priority values. */
#define MM_PRIO_LOWERMOST	((mm_priority_t) 31)
#define MM_PRIO_UPPERMOST	((mm_priority_t) 0)

/* Given a priority value get the value n levels below that. */
#define MM_PRIO_LOWER(p, n)	min(MM_PRIO_LOWERMOST, (mm_priority_t) ((p) + (n)))

/* Given a priority value get the value n levels above that. */
#define MM_PRIO_UPPER(p, n)	max(MM_PRIO_UPPERMOST, (mm_priority_t) ((p) - (n)))

/* Basic task priorities. */
#define MM_PRIO_BOOT		MM_PRIO_LOWERMOST
#define MM_PRIO_IDLE		MM_PRIO_UPPER(MM_PRIO_BOOT, 1)
#define MM_PRIO_WORK		MM_PRIO_UPPER(MM_PRIO_IDLE, 1)
#define MM_PRIO_CORE		MM_PRIO_UPPER(MM_PRIO_WORK, 10)

/* Specific task priorities. */
#define MM_PRIO_DEALER		MM_PRIO_IDLE
#define MM_PRIO_WORKER		MM_PRIO_WORK
#define MM_PRIO_MASTER		MM_PRIO_CORE
#define MM_PRIO_SYSTEM		MM_PRIO_UPPER(MM_PRIO_CORE, 1)

/* Task state values. */
typedef enum {
	MM_TASK_BLOCKED,
	MM_TASK_PENDING,
	MM_TASK_RUNNING,
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
typedef uint8_t			mm_task_flags_t;

/* Task creation attributes. */
struct mm_task_attr
{
	/* Task flags. */
	mm_task_flags_t flags;

	/* Task scheduling priority. */
	mm_priority_t priority;

	/* The task stack size. */
	uint32_t stack_size;

	/* The task name. */
	char name[MM_TASK_NAME_SIZE];
};

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
		struct mm_task *__task = mm_task_self();		\
		struct mm_task_cleanup_rec __cleanup = {		\
				.next = __task->cleanup,		\
				.routine = (void (*)(uintptr_t)) (rtn),	\
				.routine_arg = (uintptr_t) (arg),	\
			};						\
		__task->cleanup = &__cleanup;				\
		do {

/* Unregister a cleanup handler optionally executing it. */
#define mm_task_cleanup_pop(execute)					\
		} while (0);						\
		__task->cleanup = __cleanup.next;			\
		if (execute) {						\
			__cleanup.routine(__cleanup.routine_arg);	\
		}							\
	} while (0)


/* A user-space (green) thread. */
struct mm_task
{
	/* A link in a run/dead queue. */
	struct mm_list queue;
	struct mm_list wait_queue;

	/* The task status. */
	mm_task_state_t state;
	mm_task_flags_t flags;

	/* Task scheduling priority. */
	mm_priority_t priority;
	mm_priority_t original_priority;

	/* The list of task-local dynamically-allocated memory. */
	struct mm_list chunks;

	/* The list of task's ports. */
	struct mm_list ports;

	/* The list of task cleanup records. */
	struct mm_task_cleanup_rec *cleanup;

	/* The task result. */
	mm_value_t result;

	/* The task start routine and its argument. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* The task stack. */
	uint32_t stack_size;
	void *stack_base;
	mm_stack_ctx_t stack_ctx;

	/* The task core. */
	struct mm_core *core;

	/* The task name. */
	char name[MM_TASK_NAME_SIZE];

#if ENABLE_TRACE
	/* Trace nesting level. */
	int trace_level;
	/* Trace recursion detection. */
	int trace_recur;
#endif
};

void mm_task_init(void);
void mm_task_term(void);

/**********************************************************************
 * Task creation and destruction.
 **********************************************************************/

void mm_task_attr_init(struct mm_task_attr *attr)
	__attribute__((nonnull(1)));
void mm_task_attr_setflags(struct mm_task_attr *attr, mm_task_flags_t flags)
	__attribute__((nonnull(1)));
void mm_task_attr_setpriority(struct mm_task_attr *attr, mm_priority_t priority)
	__attribute__((nonnull(1)));
void mm_task_attr_setstacksize(struct mm_task_attr *attr, uint32_t stack_size)
	__attribute__((nonnull(1)));
void mm_task_attr_setname(struct mm_task_attr *attr, const char *name)
	__attribute__((nonnull(1)));

struct mm_task * mm_task_create(const struct mm_task_attr *attr,
				mm_routine_t start,
				mm_value_t start_arg)
	__attribute__((nonnull(2)));

void mm_task_destroy(struct mm_task *task)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Task utilities.
 **********************************************************************/

static inline struct mm_task *
mm_task_self(void)
{
	return mm_core->task;
}

static inline const char *
mm_task_getname(const struct mm_task *task)
{
	return task->name;
}

void mm_task_setname(struct mm_task *task, const char *name)
	__attribute__((nonnull(1, 2)));

mm_task_t mm_task_getid(const struct mm_task *task)
	__attribute__((nonnull(1)));

struct mm_task * mm_task_getptr(mm_task_t id);

/**********************************************************************
 * Task execution.
 **********************************************************************/

void mm_task_run(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_task_hoist(struct mm_task *task, mm_priority_t priority)
	__attribute__((nonnull(1)));

void mm_task_yield(void);
void mm_task_block(void);

void mm_task_exit(mm_value_t result)
	__attribute__((noreturn));

/**********************************************************************
 * Task cancellation.
 **********************************************************************/

#define MM_TASK_CANCEL_TEST(task_flags)			\
	((task_flags & (MM_TASK_CANCEL_DISABLE		\
			| MM_TASK_CANCEL_REQUIRED	\
			| MM_TASK_CANCEL_OCCURRED))	\
	 == MM_TASK_CANCEL_REQUIRED)

#define MM_TASK_CANCEL_TEST_ASYNC(task_flags)		\
	((task_flags & (MM_TASK_CANCEL_DISABLE		\
			| MM_TASK_CANCEL_REQUIRED	\
			| MM_TASK_CANCEL_OCCURRED	\
			| MM_TASK_CANCEL_ASYNCHRONOUS)) \
	 == (MM_TASK_CANCEL_REQUIRED | MM_TASK_CANCEL_ASYNCHRONOUS))

static inline void
mm_task_testcancel(void)
{
	struct mm_task *task = mm_task_self();
	if (unlikely(MM_TASK_CANCEL_TEST(task->flags))) {
		task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_RESULT_CANCELED);
	}
}

static inline void
mm_task_testcancel_asynchronous(void)
{
	struct mm_task *task = mm_task_self();
	if (unlikely(MM_TASK_CANCEL_TEST_ASYNC(task->flags))) {
		task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_RESULT_CANCELED);
	}
}

void mm_task_setcancelstate(int new_value, int *old_value_ptr);
void mm_task_setcanceltype(int new_value, int *old_value_ptr);

int mm_task_enter_cancel_point(void);
void mm_task_leave_cancel_point(int);

void mm_task_cancel(struct mm_task *task)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Task-local dynamic memory.
 **********************************************************************/

void * mm_task_alloc(size_t size)
	__attribute__((malloc));

void mm_task_free(void *ptr);

#endif /* TASK_H */
