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
#if defined(PTHREAD_STACK_MIN)
# define MM_TASK_STACK_SIZE_MIN		PTHREAD_STACK_MIN
#else
# define MM_TASK_STACK_SIZE_MIN		(12 * 1024)
#endif

// The memory pool for tasks.
static struct mm_pool mm_task_pool;

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
 * Task creation attributes.
 **********************************************************************/

void
mm_task_attr_init(struct mm_task_attr *attr)
{
	attr->flags = 0;
	attr->priority = MM_PRIO_WORK;
	attr->stack_size = MM_TASK_STACK_SIZE;
	attr->name[0] = 0;
}

void
mm_task_attr_setflags(struct mm_task_attr *attr, mm_task_flags_t flags)
{
	attr->flags = flags;
}

void
mm_task_attr_setpriority(struct mm_task_attr *attr, mm_priority_t priority)
{
	ASSERT(priority <= MM_PRIO_LOWERMOST);
	ASSERT(priority >= MM_PRIO_UPPERMOST);
	attr->priority = priority;
}

void
mm_task_attr_setstacksize(struct mm_task_attr *attr, uint32_t stack_size)
{
	if (stack_size < MM_TASK_STACK_SIZE_MIN)
		stack_size = MM_TASK_STACK_SIZE_MIN;

	attr->stack_size = stack_size;
}

void
mm_task_attr_setname(struct mm_task_attr *attr, const char *name)
{
	ENTER();

	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof attr->name)
			len = sizeof attr->name - 1;

		memcpy(attr->name, name, len);
	}
	attr->name[len] = 0;

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
mm_task_new(void)
{
	// Allocate a task.
	struct mm_task *task = mm_pool_alloc(&mm_task_pool);

	// Store the core that owns the task.
	task->core = mm_core;

	// Initialize the task stack info.
	task->stack_size = 0;
	task->stack_base = NULL;

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
mm_task_set_attr(struct mm_task *task, const struct mm_task_attr *attr)
{
	task->state = MM_TASK_CREATED;
	task->result = MM_TASK_UNRESOLVED;

	if (unlikely(attr == NULL)) {	
		task->flags = 0;
		task->original_priority = MM_PRIO_WORK;
		task->stack_size = MM_TASK_STACK_SIZE;
		task->name[0] = 0;
	} else {
		task->flags = attr->flags;
		task->original_priority = attr->priority;
		task->stack_size = attr->stack_size;
		strcpy(task->name, attr->name);
	}

	task->priority = task->original_priority;

#if ENABLE_TRACE
	task->trace_level = 0;
#endif
}

/* Create a new task. */
struct mm_task *
mm_task_create(const struct mm_task_attr *attr,
	       mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	// Check to see if called from the bootstrap context.
	bool boot = (mm_core == NULL);

	struct mm_task *task = NULL;
	// Try to reuse a dead task.
	if (likely(!boot) && !mm_list_empty(&mm_core->dead_list)) {
		// Get the last dead task.
		struct mm_list *link = mm_list_head(&mm_core->dead_list);
		struct mm_task *dead = containerof(link, struct mm_task, queue);

		// Check it against the required stack size.
		uint32_t stack_size = (attr != NULL
				       ? attr->stack_size
				       : MM_TASK_STACK_SIZE);
		if (dead->stack_size == stack_size) {
			// The dead task is just good.
			mm_list_delete(link);
			task = dead;
		} else if (dead->stack_size != MM_TASK_STACK_SIZE) {
			// The dead task has an unusual stack, free it.
			mm_stack_destroy(dead->stack_base, dead->stack_size);
			dead->stack_base = NULL;
			// Now use that task.
			mm_list_delete(link);
			task = dead;
		} else {
			// A task with unusual stack size is requested, leave
			// the dead task alone, it is likely to be reused the
			// next time.
		}
	}
	// Allocate a new task if needed.
	if (task == NULL) {
		task = mm_task_new();
	}

	// Initialize the task info.
	mm_task_set_attr(task, attr);
	task->start = start;
	task->start_arg = start_arg;

	// Allocate a new stack if needed.
	if (task->stack_base == NULL) {
		task->stack_base = mm_stack_create(task->stack_size, MM_PAGE_SIZE);
	}

	// Setup the task entry point on its own stack and queue it for
	// execution unless bootstrapping in which case it will be done
	// later in a special way.
	if (likely(!boot)) {
		mm_stack_init(&task->stack_ctx, mm_task_entry,
			      task->stack_base, task->stack_size);
		mm_task_run(task);
	}

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

/**********************************************************************
 * Task utilities.
 **********************************************************************/

/* Set or change the task name. */
void
mm_task_setname(struct mm_task *task, const char *name)
{
	ENTER();

	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof task->name)
			len = sizeof task->name - 1;

		memcpy(task->name, name, len);
	}
	task->name[len] = 0;

	LEAVE();
}

uint32_t
mm_task_getid(struct mm_task *task)
{
	return mm_pool_ptr2idx(&mm_task_pool, task);
}

struct mm_task *
mm_task_getptr(uint32_t id)
{
	return mm_pool_idx2ptr(&mm_task_pool, id);
}

/**********************************************************************
 * Task execution.
 **********************************************************************/

/* Switch to the next task in the run queue. */
static void
mm_task_switch(mm_task_state_t state)
{
	ASSERT(mm_running_task->state == MM_TASK_RUNNING);

	// Move the currently running task to a new state.
	struct mm_task *old_task = mm_running_task;
	old_task->state = state;
	if (unlikely(state == MM_TASK_INVALID)) {
		// Add it to the dead task list.
		mm_list_append(&mm_core->dead_list, &old_task->queue);
	} else {
		// Reset the priority that could have been temporary raised.
		old_task->priority = old_task->original_priority;
		if (state == MM_TASK_PENDING) {
			// Add it to the run queue.
			mm_runq_put_task(&mm_core->run_queue, old_task);
		}
	}

	// Get the next task from the run queue.  As long as this function
	// is called there is at least a boot task in the run queue.  So
	// there should never be a NULL value returned.
	struct mm_task *new_task = mm_runq_get_task(&mm_core->run_queue);
	new_task->state = MM_TASK_RUNNING;
	mm_running_task = new_task;

	// Switch to the new task relinquishing CPU control for a while.
	mm_stack_switch(&old_task->stack_ctx, &new_task->stack_ctx);

	// Resume the task unless it has been canceled and it agrees to be
	// canceled asynchronously. In that case it quits here.
	mm_task_testcancel_asynchronous();
}

/* Queue a task for execution. */
void
mm_task_run(struct mm_task *task)
{
	ENTER();
	TRACE("queue task: [%d %s], state: %d, priority: %d",
	      mm_task_getid(task), task->name,
	      task->state, task->priority);
	ASSERT(task->core == mm_core);
	ASSERT(task->priority < MM_PRIO_BOOT);
	ASSERT(task->state != MM_TASK_INVALID && task->state != MM_TASK_RUNNING);

	if (task->state != MM_TASK_PENDING) {
		task->state = MM_TASK_PENDING;
		mm_runq_put_task(&mm_core->run_queue, task);
	}

	LEAVE();
}

/* Queue a task for execution with temporary raised priority. */
void
mm_task_hoist(struct mm_task *task, mm_priority_t priority)
{
	ENTER();
	TRACE("hoist task: [%d %s], state: %d, priority: %d, %d",
	      mm_task_getid(task), task->name,
	      task->state, task->priority, priority);
	ASSERT(task->core == mm_core);
	ASSERT(task->priority < MM_PRIO_BOOT);
	ASSERT(task->state != MM_TASK_INVALID && task->state != MM_TASK_RUNNING);

	if (task->priority > priority) {
		if (task->state != MM_TASK_PENDING)
			task->state = MM_TASK_PENDING;
		else
			mm_runq_delete_task(&mm_core->run_queue, task);
		task->priority = priority;
		mm_runq_put_task(&mm_core->run_queue, task);
	} else if (task->state != MM_TASK_PENDING) {
		task->state = MM_TASK_PENDING;
		mm_runq_put_task(&mm_core->run_queue, task);
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
	mm_task_setname(mm_running_task, "dead");

	// Give the control to still running tasks.
	mm_task_switch(MM_TASK_INVALID);

	// Must never get here after the switch above.
	ABORT();
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
