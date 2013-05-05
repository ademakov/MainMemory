/*
 * task.c - MainMemory tasks.
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

#include "task.h"

#include "arch.h"
#include "core.h"
#include "pool.h"
#include "port.h"
#include "sched.h"
#include "stack.h"
#include "util.h"

#define MM_TASK_STACK_SIZE (28 * 1024)

/* The memory pool for tasks. */
static struct mm_pool mm_task_pool;

static char mm_task_boot_name[] = "boot";

/**********************************************************************
 * Global task data initialization and termination.
 **********************************************************************/

void
mm_task_init(void)
{
	ENTER();

	mm_pool_init(&mm_task_pool, "task", sizeof (struct mm_task));

	LEAVE();
}

void
mm_task_term(void)
{
	ENTER();

	// TODO: stop and destroy all tasks.

	mm_pool_discard(&mm_task_pool);

	LEAVE();
}

/**********************************************************************
 * Task creation and destruction.
 **********************************************************************/

/* Entry point for a task. */
static void
mm_task_entry(void)
{
	TRACE("enter task %s", mm_running_task->name);

	// Execute the task routine on an empty stack.
	mm_result_t result = mm_running_task->start(mm_running_task->start_arg);

	// Finish the task making sure there is no return from this point
	// as there is no valid stack frame above it.
	mm_task_exit(result);
}

/* Execute task cleanup routines. */
static void
mm_task_cleanup(struct mm_task *task)
{
	ENTER();

	while (task->cleanup != NULL) {
		void (*routine)(uintptr_t) = task->cleanup->routine;
		uintptr_t routine_arg = task->cleanup->routine_arg;
		task->cleanup = task->cleanup->next;
		routine(routine_arg);
	}

	LEAVE();
}

/* Free task-local dynamic memory. */
static void
mm_task_free_chunks(struct mm_task *task)
{
	ENTER();

	while (!mm_list_empty(&task->chunks)) {
		struct mm_list *link = mm_list_head(&task->chunks);
		mm_list_delete(link);
		mm_free(link);
	}

	LEAVE();
}

/* Create a bootstrap task. */
struct mm_task *
mm_task_create_boot(void)
{
	ENTER();

	struct mm_task *task = mm_pool_alloc(&mm_task_pool);
	memset(task, 0, sizeof(struct mm_task));
	task->state = MM_TASK_RUNNING;
	task->name = mm_task_boot_name;

	LEAVE();
	return task;
}

/* Destroy a bootstrap task. */
void
mm_task_destroy_boot(struct mm_task *task)
{
	ENTER();
	ASSERT(task->name == mm_task_boot_name);

	mm_pool_free(&mm_task_pool, task);

	LEAVE();
}

/* Create a new task. */
struct mm_task *
mm_task_create(const char *name, mm_task_flags_t flags,
	       mm_routine start, uintptr_t start_arg)
{
	ENTER();

	struct mm_task *task;

	if (mm_list_empty(&mm_core->dead_list)) {
		/* Allocate a new task. */
		task = mm_pool_alloc(&mm_task_pool);

		/* Initialize the task name. */
		task->name = NULL;

		/* Allocate the task stack. */
		task->stack_size = MM_TASK_STACK_SIZE;
		task->stack_base = mm_stack_create(task->stack_size);
	} else {
		/* Reuse a dead task. */
		struct mm_list *link = mm_list_head(&mm_core->dead_list);
		task = containerof(link, struct mm_task, queue);
		mm_list_delete(link);
	}

	/* Set the task name. */
	mm_task_set_name(task, name);

	/* Initialize the task info. */
	task->state = MM_TASK_CREATED;
	task->flags = flags;
	task->priority = MM_PRIO_DEFAULT;
	task->blocked_on = NULL;
	task->start = start;
	task->start_arg = start_arg;
	task->result = MM_TASK_UNRESOLVED;
	task->cleanup = NULL;
#if ENABLE_TRACE
	task->trace_level = 0;
#endif

	/* Initialize the task ports list. */
	mm_list_init(&task->ports);

	/* Initialize the dynamic memory list. */
	mm_list_init(&task->chunks);

	/* Initialize the task stack. */
	mm_stack_init(&task->stack_ctx, mm_task_entry,
		      task->stack_base, task->stack_size);

	LEAVE();
	return task;
}

/* Destroy a task. The task should not run at the moment and it
 * should be absolutely guaranteed from being used afterwards. */
void
mm_task_destroy(struct mm_task *task)
{
	ENTER();
	ASSERT(task->name != mm_task_boot_name);
	ASSERT(task->state == MM_TASK_INVALID || task->state == MM_TASK_CREATED);
	ASSERT((task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);

	/* Destroy the ports. */
	while (!mm_list_empty(&task->ports)) {
		// TODO: ensure that ports are not referenced from elsewhere.
		struct mm_list *link = mm_list_head(&task->ports);
		struct mm_port *port = containerof(link, struct mm_port, ports);
		mm_port_destroy(port);
	}

	/* Free the dynamic memory. */
	mm_task_free_chunks(task);
	mm_free(task->name);

	/* Free the stack. */
	mm_stack_destroy(task->stack_base, MM_TASK_STACK_SIZE);

	/* At last free the task struct. */
	mm_pool_free(&mm_task_pool, task);

	LEAVE();
}

/* Let the task to be either reused or destroyed. The task may still run
 * at the moment but it should be guaranteed that it is not going to be
 * used after it yields. That is it should not be referenced in any queue. */
void
mm_task_recycle(struct mm_task *task)
{
	ENTER();
	ASSERT(task->state == MM_TASK_INVALID || task->state == MM_TASK_CREATED);
	ASSERT((task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);

	mm_list_append(&mm_core->dead_list, &task->queue);

	LEAVE();
}

/* Finish the current task. */
void
mm_task_exit(mm_result_t result)
{
	ENTER();
	TRACE("exiting task '%s' with status %lu",
	      mm_running_task->name, (unsigned long) result);

	// Set the task result.
	mm_running_task->result = result;

	// TODO: invalidate ports ?

	// Call the cleanup handlers.
	mm_task_cleanup(mm_running_task);

	// Free the dynamic memory.
	mm_task_free_chunks(mm_running_task);

	// Give the control to still running tasks.
	mm_sched_abort();

	LEAVE();
}

/* Set or change the task name. */
void
mm_task_set_name(struct mm_task *task, const char *name)
{
	ENTER();

	if (likely(name != NULL)) {
		if (task->name != NULL) {
			if (strcmp(task->name, name) != 0) {
				mm_free(task->name);
				task->name = mm_strdup(name);
			}
		} else {
			task->name = mm_strdup(name);
		}
	} else if (task->name != NULL) {
		mm_free(task->name);
		task->name = NULL;
	}

	LEAVE();
}

uint32_t
mm_task_id(struct mm_task *task)
{
	return mm_pool_ptr2idx(&mm_task_pool, task);
}

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

void
mm_task_testcancel(void)
{
	if (unlikely(MM_TASK_CANCEL_TEST(mm_running_task->flags))) {
		DEBUG("task canceled!");
		mm_running_task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_TASK_CANCELED);
	}
}

void
mm_task_testcancel_asynchronous(void)
{
	if (unlikely(MM_TASK_CANCEL_TEST_ASYNC(mm_running_task->flags))) {
		DEBUG("task canceled!");
		mm_running_task->flags |= MM_TASK_CANCEL_OCCURRED;
		mm_task_exit(MM_TASK_CANCELED);
	}
}

void
mm_task_setcancelstate(int new_value, int *old_value_ptr)
{
	ENTER();
	ASSERT(new_value == MM_TASK_CANCEL_ENABLE
	       || new_value == MM_TASK_CANCEL_DISABLE);

	int old_value = (mm_running_task->flags & MM_TASK_CANCEL_DISABLE);
	if (likely(old_value != new_value)) {
		if (new_value) {
			mm_running_task->flags |= MM_TASK_CANCEL_DISABLE;
		} else {
			mm_running_task->flags &= ~MM_TASK_CANCEL_DISABLE;
			mm_task_testcancel_asynchronous();
		}
	}

	if (old_value_ptr != NULL) {
		*old_value_ptr = old_value;
	}

	LEAVE();
}

void
mm_task_setcanceltype(int new_value, int *old_value_ptr)
{
	ENTER();
	ASSERT(new_value == MM_TASK_CANCEL_DEFERRED
	       || new_value == MM_TASK_CANCEL_ASYNCHRONOUS);

	int old_value = (mm_running_task->flags & MM_TASK_CANCEL_ASYNCHRONOUS);
	if (likely(old_value != new_value)) {
		if (new_value) {
			mm_running_task->flags |= MM_TASK_CANCEL_ASYNCHRONOUS;
			mm_task_testcancel_asynchronous();
		} else {
			mm_running_task->flags &= ~MM_TASK_CANCEL_ASYNCHRONOUS;
		}
	}

	if (old_value_ptr != NULL) {
		*old_value_ptr = old_value;
	}

	LEAVE();
}

int
mm_task_enter_cancel_point(void)
{
	ENTER();

	int cp = (mm_running_task->flags & MM_TASK_CANCEL_ASYNCHRONOUS);
	if (likely(cp == 0)) {
		mm_running_task->flags |= MM_TASK_CANCEL_ASYNCHRONOUS;
		mm_task_testcancel_asynchronous();
	}

	LEAVE();
	return cp;
}

void
mm_task_leave_cancel_point(int cp)
{
	ENTER();

	if (likely(cp == 0)) {
		mm_running_task->flags &= ~MM_TASK_CANCEL_ASYNCHRONOUS;
	}

	LEAVE();
}

void
mm_task_cancel(struct mm_task *task)
{
	ENTER();

	task->flags |= MM_TASK_CANCEL_REQUIRED;
	if (unlikely(task->state == MM_TASK_RUNNING)) {
		ASSERT(task == mm_running_task);
		mm_task_testcancel_asynchronous();
	} else {
		mm_sched_run(task);
	}

	LEAVE();
}

/**********************************************************************
 * Task event waiting.
 **********************************************************************/

/* Wait queue cleanup handler. */
static void
mm_task_wait_cleanup(struct mm_list *queue __attribute__((unused)))
{
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) != 0);

	mm_list_delete(&mm_running_task->queue);
	mm_running_task->flags &= ~MM_TASK_WAITING;
}

/* Wait for a wakeup signal in the FIFO order. */
void
mm_task_wait_fifo(struct mm_list *queue)
{
	ENTER();
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) == 0);

	// Enqueue the task.
	mm_running_task->flags |= MM_TASK_WAITING;
	mm_list_append(queue, &mm_running_task->queue);

	// Ensure dequeuing on exit.
	mm_task_cleanup_push(mm_task_wait_cleanup, queue);

	// Wait for a wakeup signal.
	mm_sched_block();

	// Dequeue on return.
	mm_task_cleanup_pop(true);

	LEAVE();
}

/* Wait for a wakeup signal in the LIFO order. */
void
mm_task_wait_lifo(struct mm_list *queue)
{
	ENTER();
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) == 0);

	// Enqueue the task.
	mm_running_task->flags |= MM_TASK_WAITING;
	mm_list_insert(queue, &mm_running_task->queue);

	// Ensure dequeuing on exit.
	mm_task_cleanup_push(mm_task_wait_cleanup, queue);

	// Wait for a wakeup signal.
	mm_sched_block();

	// Dequeue on return.
	mm_task_cleanup_pop(true);

	LEAVE();
}

/* Wakeup a task in a wait queue. */
void
mm_task_signal(struct mm_list *queue)
{
	ENTER();

	if (!mm_list_empty(queue)) {
		struct mm_list *link = mm_list_head(queue);
		struct mm_task *task = containerof(link, struct mm_task, queue);
		mm_sched_run(task);
	}

	LEAVE();
}

/* Wakeup all tasks in a wait queue. */
void
mm_task_broadcast(struct mm_list *queue)
{
	ENTER();

	struct mm_list *link = queue;
	while (mm_list_has_next(queue, link)) {
		link = link->next;
		struct mm_task *task = containerof(link, struct mm_task, queue);
		mm_sched_run(task);
	}

	LEAVE();
}

/**********************************************************************
 * Task-local dynamic memory.
 **********************************************************************/

void *
mm_task_alloc(size_t size)
{
	ENTER();
	ASSERT(size > 0);

	/* Allocate the requested memory plus some extra for the list link. */
	void *ptr = mm_alloc(size + sizeof(struct mm_list));

	/* Keep the allocated memory in the task's chunk list. */
	mm_list_append(&mm_running_task->chunks, (struct mm_list *) ptr);

	/* Get the address past the list link. */
	ptr = (void *) (((char *) ptr) + sizeof(struct mm_list));

	LEAVE();
	return ptr;
}

void
mm_task_free(void *ptr)
{
	ENTER();

	if (likely(ptr != NULL)) {
		/* Get the real start address of the chunk. */
		struct mm_list *link = (struct mm_list *) (((char *) ptr) - sizeof(struct mm_list));

		/* Remove it from the task's chunk list. */
		mm_list_delete(link);

		/* Free the memory. */
		mm_free(link);
	}

	LEAVE();
}
