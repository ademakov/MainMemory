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
#define MM_TASK_BOOT_STACK_SIZE PTHREAD_STACK_MIN

// The memory pool for tasks.
static struct mm_pool mm_task_pool;

// Special task mames.
static char mm_task_boot_name[] = "boot";
static char mm_task_dead_name[] = "dead";

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

/* Create a new task. */
static struct mm_task *
mm_task_new(uint32_t stack_size)
{
	// Allocate a task.
	struct mm_task *task = mm_pool_alloc(&mm_task_pool);

	// Allocate a task stack.
	task->stack_size = stack_size;
	task->stack_base = mm_stack_create(task->stack_size);

	// Initialize the task ports list.
	mm_list_init(&task->ports);

	// Initialize the cleanup handler list.
	task->cleanup = NULL;

	// Initialize the dynamic memory list.
	mm_list_init(&task->chunks);

	return task;
}

/* Initialize a task. */
static void
mm_task_set_attr(struct mm_task *task,
		 mm_task_flags_t flags,
		 uint8_t priority)
{
	task->state = MM_TASK_CREATED;
	task->flags = flags;
	task->priority = priority;

	task->blocked_on = NULL;

	task->result = MM_TASK_UNRESOLVED;

#if ENABLE_TRACE
	task->trace_level = 0;
#endif
}

/* Create a bootstrap task. */
struct mm_task *
mm_task_create_boot(void)
{
	ENTER();

	// Create a new task object.
	struct mm_task *task = mm_task_new(MM_TASK_BOOT_STACK_SIZE);

	// Set the task name.
	ASSERT(sizeof task->name > sizeof mm_task_boot_name);
	strcpy(task->name, mm_task_boot_name);

	// Initialize the task info.
	mm_task_set_attr(task, 0, MM_PRIO_DEFAULT);
	task->start = NULL;
	task->start_arg = 0;

	LEAVE();
	return task;
}

/* Create a new task. */
struct mm_task *
mm_task_create(const char *name, mm_task_flags_t flags,
	       mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	struct mm_task *task;

	if (mm_list_empty(&mm_core->dead_list)) {
		// Allocate a new task.
		task = mm_task_new(MM_TASK_STACK_SIZE);
	} else {
		// Resurrect a dead task.
		struct mm_list *link = mm_list_head(&mm_core->dead_list);
		task = containerof(link, struct mm_task, queue);
		mm_list_delete(link);
	}

	// Initialize the task stack.
	mm_stack_init(&task->stack_ctx, mm_task_entry,
		      task->stack_base, task->stack_size);

	// Set the task name.
	mm_task_set_name(task, name);

	// Initialize the task info.
	mm_task_set_attr(task, flags, MM_PRIO_DEFAULT);
	task->start = start;
	task->start_arg = start_arg;

	LEAVE();
	return task;
}

/* Destroy a task. The task should not run at the moment and it
 * should be absolutely guaranteed from being used afterwards. */
void
mm_task_destroy(struct mm_task *task)
{
	ENTER();
	ASSERT(task->state == MM_TASK_INVALID || task->state == MM_TASK_CREATED);
	ASSERT((task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);

	// Destroy the ports.
	while (!mm_list_empty(&task->ports)) {
		// TODO: ensure that ports are not referenced from elsewhere.
		struct mm_list *link = mm_list_head(&task->ports);
		struct mm_port *port = containerof(link, struct mm_port, ports);
		mm_port_destroy(port);
	}

	// Free the dynamic memory.
	mm_task_free_chunks(task);

	// Free the stack.
	mm_stack_destroy(task->stack_base, task->stack_size);

	// At last free the task struct.
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

	ASSERT(sizeof task->name > sizeof mm_task_dead_name);
	strcpy(task->name, mm_task_dead_name);

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
		size_t len = strlen(name);
		if (len >= sizeof task->name)
			len = sizeof task->name - 1;
		memcpy(task->name, name, len);
		task->name[len] = 0;
	} else {
		task->name[0] = 0;
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
		mm_task_wakeup(task);
	}

	LEAVE();
}

/**********************************************************************
 * Task event waiting.
 **********************************************************************/

/* Delete a task from a wait queue. */
static void
mm_task_wait_delete(struct mm_task *task)
{
	ASSERT((task->flags & MM_TASK_WAITING) != 0);

	mm_list_delete(&task->queue);
	task->flags &= ~MM_TASK_WAITING;
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

	// Ensure dequeuing on exit & cancel.
	mm_task_cleanup_push(mm_task_wait_delete, mm_running_task);

	// Wait for a wakeup signal.
	mm_sched_block();

	// Dequeue on return.
	mm_task_cleanup_pop((mm_running_task->flags & MM_TASK_WAITING) != 0);

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

	// Ensure dequeuing on exit & cancel.
	mm_task_cleanup_push(mm_task_wait_delete, mm_running_task);

	// Wait for a wakeup signal.
	mm_sched_block();

	// Dequeue on return.
	mm_task_cleanup_pop((mm_running_task->flags & MM_TASK_WAITING) != 0);

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
		mm_task_wait_delete(task);
		mm_sched_run(task);
	}

	LEAVE();
}

/* Wakeup all tasks in a wait queue. */
void
mm_task_broadcast(struct mm_list *queue)
{
	ENTER();

	while (!mm_list_empty(queue)) {
		struct mm_list *link = mm_list_head(queue);
		struct mm_task *task = containerof(link, struct mm_task, queue);
		mm_task_wait_delete(task);
		mm_sched_run(task);
	}

	LEAVE();
}

void
mm_task_wakeup(struct mm_task *task)
{
	ENTER();

	if ((task->flags & MM_TASK_WAITING) != 0)
		mm_task_wait_delete(task);

	mm_sched_run(task);

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
