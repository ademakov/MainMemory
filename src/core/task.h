/*
 * core/task.h - MainMemory tasks.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

#ifndef CORE_TASK_H
#define CORE_TASK_H

#include "common.h"
#include "base/cstack.h"
#include "base/list.h"
#include "core/core.h"
#include "core/value.h"

#define ENABLE_TASK_LOCATION	0
#define ENABLE_TASK_IO_FLAGS	0

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

/* Specific task priorities. */
#define MM_PRIO_DEALER		MM_PRIO_IDLE
#define MM_PRIO_MASTER		MM_PRIO_UPPER(MM_PRIO_DEALER, 1)
#define MM_PRIO_WORKER		MM_PRIO_UPPER(MM_PRIO_MASTER, 1)

/* Task state values. */
typedef enum {
	MM_TASK_BLOCKED,
	MM_TASK_PENDING,
	MM_TASK_RUNNING,
	MM_TASK_INVALID,
} mm_task_state_t;

/*
 * Task flags.
 */
/* Flags for cancellation handling. */
#define MM_TASK_CANCEL_ENABLE		0x0000
#define MM_TASK_CANCEL_DISABLE		0x0001
#define MM_TASK_CANCEL_DEFERRED		0x0000
#define MM_TASK_CANCEL_ASYNCHRONOUS	0x0002
#define MM_TASK_CANCEL_REQUIRED		0x0004
#define MM_TASK_CANCEL_OCCURRED		0x0008
/* Flags for tasks blocked for various reasons. */
#if ENABLE_TASK_IO_FLAGS
#define MM_TASK_READING			0x0010
#define MM_TASK_WRITING			0x0020
#endif
#define MM_TASK_WAITING			0x0040
#define MM_TASK_COMBINING		0x0080
/* The task is a bootstrap task. */
#define MM_TASK_BOOT			0x8000

/* Task flags type. */
typedef uint16_t mm_task_flags_t;

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
		struct mm_task *__task = mm_task_selfptr();		\
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
	struct mm_link queue;
	struct mm_link wait_queue;

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
	mm_cstack_t stack_ctx;

	/* The task core. */
	struct mm_core *core;

	/* The task name. */
	char name[MM_TASK_NAME_SIZE];

#if ENABLE_TASK_LOCATION
	const char *location;
	const char *function;
#endif

#if ENABLE_TRACE
	/* Thread trace context. */
	struct mm_trace_context trace;
#endif
};

/**********************************************************************
 * Task subsystem initialization and termination.
 **********************************************************************/

void
mm_task_init(void);

void
mm_task_term(void);

/**********************************************************************
 * Task creation and destruction.
 **********************************************************************/

void NONNULL(1)
mm_task_attr_init(struct mm_task_attr *attr);
void NONNULL(1)
mm_task_attr_setflags(struct mm_task_attr *attr, mm_task_flags_t flags);
void NONNULL(1)
mm_task_attr_setpriority(struct mm_task_attr *attr, mm_priority_t priority);
void NONNULL(1)
mm_task_attr_setstacksize(struct mm_task_attr *attr, uint32_t stack_size);
void NONNULL(1)
mm_task_attr_setname(struct mm_task_attr *attr, const char *name);

struct mm_task * NONNULL(2)
mm_task_create(const struct mm_task_attr *attr, mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_task_destroy(struct mm_task *task);

/**********************************************************************
 * Task utilities.
 **********************************************************************/

static inline struct mm_task *
mm_task_selfptr(void)
{
	return mm_core_selfptr()->task;
}

struct mm_task *
mm_task_getptr(mm_task_t id);

mm_task_t NONNULL(1)
mm_task_getid(const struct mm_task *task);

static inline mm_task_t
mm_task_self()
{
	return mm_task_getid(mm_task_selfptr());
}

static inline const char * NONNULL(1)
mm_task_getname(const struct mm_task *task)
{
	return task->name;
}

void NONNULL(1)
mm_task_setname(struct mm_task *task, const char *name);

void NONNULL(1)
mm_task_print_status(const struct mm_task *task);

/**********************************************************************
 * Task execution.
 **********************************************************************/

void NONNULL(1)
mm_task_run(struct mm_task *task);

void NONNULL(1)
mm_task_hoist(struct mm_task *task, mm_priority_t priority);

#if ENABLE_TASK_LOCATION

# define mm_task_yield() mm_task_yield_at(__LOCATION__, __FUNCTION__)
# define mm_task_block() mm_task_block_at(__LOCATION__, __FUNCTION__)

void NONNULL(1, 2)
mm_task_yield_at(const char *location, const char *function);

void NONNULL(1, 2)
mm_task_block_at(const char *location, const char *function);

#else /* !ENABLE_TASK_LOCATION */

void
mm_task_yield(void);

void
mm_task_block(void);

#endif /* !ENABLE_TASK_LOCATION */

void NORETURN
mm_task_exit(mm_value_t result);

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
	struct mm_task *task = mm_task_selfptr();
	if (unlikely(MM_TASK_CANCEL_TEST(task->flags))) {
		task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_RESULT_CANCELED);
	}
}

static inline void
mm_task_testcancel_asynchronous(void)
{
	struct mm_task *task = mm_task_selfptr();
	if (unlikely(MM_TASK_CANCEL_TEST_ASYNC(task->flags))) {
		task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_RESULT_CANCELED);
	}
}

void mm_task_setcancelstate(int new_value, int *old_value_ptr);
void mm_task_setcanceltype(int new_value, int *old_value_ptr);

int mm_task_enter_cancel_point(void);
void mm_task_leave_cancel_point(int);

void NONNULL(1)
mm_task_cancel(struct mm_task *task);

/**********************************************************************
 * Task-local dynamic memory.
 **********************************************************************/

void * MALLOC
mm_task_alloc(size_t size);

void
mm_task_free(void *ptr);

#endif /* CORE_TASK_H */
