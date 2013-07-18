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

#include "alloc.h"
#include "core.h"
#include "pool.h"
#include "port.h"
#include "stack.h"
#include "timer.h"
#include "trace.h"

/* Regular task stack size. */
#define MM_TASK_STACK_SIZE		(32 * 1024)

/* Minimal task stack size. */
#define MM_TASK_STACK_SIZE_MIN		(12 * 1024)

/* Bootstrap task stack size. */
#if !defined(PTHREAD_STACK_MIN) || (PTHREAD_STACK_MIN < MM_TASK_STACK_SIZE_MIN)
# define MM_TASK_BOOT_STACK_SIZE	MM_TASK_STACK_SIZE_MIN
#else
# define MM_TASK_BOOT_STACK_SIZE	PTHREAD_STACK_MIN
#endif

// The memory pool for tasks.
static struct mm_pool mm_task_pool;

// Special task mames.
static char mm_task_boot_name[] = "boot";
static char mm_task_dead_name[] = "dead";

// The currently running task.
__thread struct mm_task *mm_running_task = NULL;

/**********************************************************************
 * Global task data initialization and termination.
 **********************************************************************/

void
mm_task_init(void)
{
	ENTER();

	mm_pool_prepare(&mm_task_pool, "task",
			&mm_alloc_global, sizeof (struct mm_task));

	LEAVE();
}

void
mm_task_term(void)
{
	ENTER();

	// TODO: stop and destroy all tasks.

	mm_pool_cleanup(&mm_task_pool);

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
		struct mm_list *link = mm_list_delete_head(&task->chunks);
		mm_core_free(link);
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
	task->stack_base = mm_stack_create(task->stack_size, MM_PAGE_SIZE);

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
	mm_task_set_attr(task, MM_TASK_CANCEL_DISABLE, MM_PRIO_BOOT);
	task->start = NULL;
	task->start_arg = 0;

	LEAVE();
	return task;
}

/* Create a new task. */
struct mm_task *
mm_task_create(const char *name, mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	struct mm_task *task;

	if (mm_list_empty(&mm_core->dead_list)) {
		// Allocate a new task.
		task = mm_task_new(MM_TASK_STACK_SIZE);
	} else {
		// Resurrect a dead task.
		struct mm_list *link = mm_list_delete_head(&mm_core->dead_list);
		task = containerof(link, struct mm_task, queue);
	}

	// Initialize the task stack.
	mm_stack_init(&task->stack_ctx, mm_task_entry,
		      task->stack_base, task->stack_size);

	// Set the task name.
	mm_task_set_name(task, name);

	// Initialize the task info.
	mm_task_set_attr(task, 0, MM_PRIO_DEFAULT);
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
 * Task execution.
 **********************************************************************/

/* Switch to the next task in the run queue. */
static void
mm_task_switch(mm_task_state_t state)
{
	ASSERT(mm_running_task->state == MM_TASK_RUNNING);

	struct mm_task *old_task = mm_running_task;
	if (state == MM_TASK_PENDING)
		mm_runq_put_task(&mm_core->run_queue, old_task);
	else if (state == MM_TASK_INVALID)
		mm_list_append(&mm_core->dead_list, &old_task->queue);
	old_task->state = state;

	// As long as this function is called there is at least a boot task
	// in the run queue. So the next task should never be NULL here.
	struct mm_task *new_task = mm_runq_get_task(&mm_core->run_queue);
	new_task->state = MM_TASK_RUNNING;
	mm_running_task = new_task;

	mm_stack_switch(&old_task->stack_ctx, &new_task->stack_ctx);

	mm_task_testcancel_asynchronous();
}

void
mm_task_run(struct mm_task *task)
{
	ENTER();
	TRACE("enqueue task: [%d %s] %d", mm_task_id(task), task->name, task->state);
	ASSERT(task->state != MM_TASK_INVALID && task->state != MM_TASK_RUNNING);
	ASSERT(task->priority != MM_PRIO_BOOT);

	if (task->state != MM_TASK_PENDING) {
		mm_runq_put_task(&mm_core->run_queue, task);
		task->state = MM_TASK_PENDING;
	}

	LEAVE();
}

void
mm_task_yield(void)
{
	ENTER();

	mm_task_switch(MM_TASK_PENDING);

	LEAVE();
}

void
mm_task_block(void)
{
	ENTER();

	mm_task_switch(MM_TASK_BLOCKED);

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

	// At this point the task must not be in any queue.
	ASSERT((mm_running_task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);

	// Free the dynamic memory.
	mm_task_free_chunks(mm_running_task);

	// Reset the task name.
	ASSERT(sizeof mm_running_task->name > sizeof mm_task_dead_name);
	strcpy(mm_running_task->name, mm_task_dead_name);

	// Add it to the list of dead tasks.

	// Give the control to still running tasks.
	mm_task_switch(MM_TASK_INVALID);

	// Must never get here after the switch above.
	ABORT();

	LEAVE();
}

/**********************************************************************
 * Task cancellation.
 **********************************************************************/

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
		mm_task_run(task);
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

	mm_list_delete(&task->wait_queue);
	task->flags &= ~MM_TASK_WAITING;
}

/* Wait for a wakeup signal in the FIFO order. */
void
mm_task_wait(struct mm_list *queue)
{
	ENTER();
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) == 0);

	// Enqueue the task.
	mm_running_task->flags |= MM_TASK_WAITING;
	mm_list_append(queue, &mm_running_task->wait_queue);

	// Ensure dequeuing on exit & cancel.
	mm_task_cleanup_push(mm_task_wait_delete, mm_running_task);

	// Wait for a wakeup signal.
	mm_task_block();

	// Dequeue on return.
	mm_task_cleanup_pop(true);

	LEAVE();
}

/* Wait for a wakeup signal in the LIFO order. */
void
mm_task_waitfirst(struct mm_list *queue)
{
	ENTER();
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) == 0);

	// Enqueue the task.
	mm_running_task->flags |= MM_TASK_WAITING;
	mm_list_insert(queue, &mm_running_task->wait_queue);

	// Ensure dequeuing on exit & cancel.
	mm_task_cleanup_push(mm_task_wait_delete, mm_running_task);

	// Wait for a wakeup signal.
	mm_task_block();

	// Dequeue on return.
	mm_task_cleanup_pop(true);

	LEAVE();
}

/* Wait for a wakeup signal in the FIFO order with specified timeout. */
void
mm_task_timedwait(struct mm_list *queue, mm_timeout_t timeout)
{
	ENTER();
	ASSERT((mm_running_task->flags & MM_TASK_WAITING) == 0);

	// Enqueue the task.
	mm_running_task->flags |= MM_TASK_WAITING;
	mm_list_append(queue, &mm_running_task->wait_queue);

	// Ensure dequeuing on exit & cancel.
	mm_task_cleanup_push(mm_task_wait_delete, mm_running_task);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

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
		struct mm_task *task = containerof(link, struct mm_task, wait_queue);
		mm_task_run(task);
	}

	LEAVE();
}

/* Wakeup all tasks in a wait queue. */
void
mm_task_broadcast(struct mm_list *queue)
{
	ENTER();

	struct mm_list *link = mm_list_head(queue);
	while (link != queue) {
		struct mm_task *task = containerof(link, struct mm_task, wait_queue);
		link = link->next;
		mm_task_run(task);
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
	void *ptr = mm_core_alloc(size + sizeof(struct mm_list));

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
		mm_core_free(link);
	}

	LEAVE();
}
