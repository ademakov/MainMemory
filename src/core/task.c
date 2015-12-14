/*
 * core/task.c - MainMemory tasks.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "core/task.h"
#include "core/core.h"
#include "core/port.h"
#include "core/timer.h"

#include "base/bitops.h"
#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/memory/cstack.h"
#include "base/memory/pool.h"
#include "base/thread/thread.h"

/* Regular task stack size. */
#define MM_TASK_STACK_DEFAULT		(7 * MM_PAGE_SIZE)

/* Minimum task stack size. */
#define MM_TASK_STACK_MIN		(1 * MM_PAGE_SIZE)

// The memory pool for tasks.
static struct mm_pool mm_task_pool;

/**********************************************************************
 * Global task data initialization and termination.
 **********************************************************************/

void
mm_task_init(void)
{
	ENTER();

	mm_pool_prepare_global(&mm_task_pool, "task", sizeof (struct mm_task));

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

void NONNULL(1)
mm_task_attr_init(struct mm_task_attr *attr)
{
	memset(attr, 0, sizeof(*attr));
	attr->priority = MM_PRIO_WORK;
}

void NONNULL(1)
mm_task_attr_setflags(struct mm_task_attr *attr, mm_task_flags_t flags)
{
	attr->flags = flags;
}

void NONNULL(1)
mm_task_attr_setpriority(struct mm_task_attr *attr, mm_priority_t priority)
{
	ASSERT(priority <= MM_PRIO_LOWERMOST);
	ASSERT(priority >= MM_PRIO_UPPERMOST);
	attr->priority = priority;
}

void NONNULL(1)
mm_task_attr_setstacksize(struct mm_task_attr *attr, uint32_t stack_size)
{
	attr->stack_size = stack_size;
}

void NONNULL(1)
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
	struct mm_task *task = mm_task_selfptr();

#if ENABLE_TRACE
	mm_trace_context_prepare(&task->trace, "[%s][%d %s]",
				 mm_thread_getname(mm_thread_selfptr()),
				 mm_task_getid(task),
				 mm_task_getname(task));
#endif

	TRACE("enter task %s", mm_task_getname(task));

	// Execute the task routine on an empty stack.
	mm_value_t result = task->start(task->start_arg);

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
		struct mm_link *link = mm_list_remove_head(&task->chunks);
		mm_private_free(link);
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
	task->core = mm_core_selfptr();

	// Initialize the task stack info.
	task->stack_size = 0;
	task->stack_base = NULL;

	// Initialize the task ports list.
	mm_list_prepare(&task->ports);

	// Initialize the cleanup handler list.
	task->cleanup = NULL;

	// Initialize the dynamic memory list.
	mm_list_prepare(&task->chunks);

	return task;
}

static uint32_t
mm_task_attr_getstacksize(const struct mm_task_attr *attr)
{
	/* Handle default stack size cases. */
	if (attr == NULL)
		return MM_TASK_STACK_DEFAULT;
	if (!attr->stack_size) {
		if (!(attr->flags & MM_TASK_BOOT))
			return MM_TASK_STACK_DEFAULT;
		return 0;
	}

	/* Sanitize specified stack size value. */
	if (attr->stack_size < MM_TASK_STACK_MIN)
		return MM_TASK_STACK_MIN;
	return mm_round_up(attr->stack_size, MM_PAGE_SIZE);
}

/* Initialize a task. */
static void
mm_task_set_attr(struct mm_task *task, const struct mm_task_attr *attr)
{
	task->result = MM_RESULT_NOTREADY;

	if (unlikely(attr == NULL)) {	
		task->flags = 0;
		task->original_priority = MM_PRIO_WORK;
		strcpy(task->name, "unnamed");
	} else {
		task->flags = attr->flags;
		task->original_priority = attr->priority;
		if (attr->name[0])
			memcpy(task->name, attr->name, MM_TASK_NAME_SIZE);
		else
			strcpy(task->name, "unnamed");
	}

	task->priority = task->original_priority;

#if ENABLE_TASK_LOCATION
	task->location = "<not set yet>";
	task->function = "<not set yet>";
#endif
}

/* Create a new task. */
struct mm_task *
mm_task_create(const struct mm_task_attr *attr, mm_routine_t start, mm_value_t start_arg)
{
	ENTER();
	struct mm_task *task = NULL;

	// Determine the required stack size.
	uint32_t stack_size = mm_task_attr_getstacksize(attr);

	// Try to reuse a dead task.
	struct mm_core *core = mm_core_selfptr();
	if (core != NULL && !mm_list_empty(&core->dead)) {
		// Get the last dead task.
		struct mm_link *link = mm_list_head(&core->dead);
		struct mm_task *dead = containerof(link, struct mm_task, queue);

		// Check it against the required stack size.
		if (dead->stack_size == stack_size) {
			// The dead task is just good.
			mm_list_delete(link);
			task = dead;
		} else if (dead->stack_size != MM_TASK_STACK_DEFAULT) {
			// The dead task has an unusual stack, free it.
			mm_cstack_destroy(dead->stack_base, dead->stack_size);
			dead->stack_size = 0;
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
	if (task == NULL)
		task = mm_task_new();

	// Initialize the task info.
	mm_task_set_attr(task, attr);
	task->start = start;
	task->start_arg = start_arg;

	// Add it to the blocked task list.
	if (core != NULL) {
		task->state = MM_TASK_BLOCKED;
		mm_list_append(&core->block, &task->queue);
	} else {
		task->state = MM_TASK_INVALID;
	}

	if (stack_size) {
		// Determine combined stack and guard page size.
		uint32_t total_size = stack_size + MM_PAGE_SIZE;

		// Allocate a new stack if needed.
		if (task->stack_base == NULL)
			task->stack_base = mm_cstack_create(total_size,
							    MM_PAGE_SIZE);
		task->stack_size = stack_size;

		// Setup the task entry point on the stack and queue
		// it for execution.
		mm_cstack_prepare(&task->stack_ctx, mm_task_entry,
				  task->stack_base, total_size);
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
	ASSERT(task->state == MM_TASK_INVALID || task->state == MM_TASK_BLOCKED);
#if ENABLE_TASK_IO_FLAGS
	ASSERT((task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);
#else
	ASSERT((task->flags & MM_TASK_WAITING) == 0);
#endif

	// Destroy the ports.
	while (!mm_list_empty(&task->ports)) {
		// TODO: ensure that ports are not referenced from elsewhere.
		struct mm_link *link = mm_list_head(&task->ports);
		struct mm_port *port = containerof(link, struct mm_port, ports);
		mm_port_destroy(port);
	}

	// Free the dynamic memory.
	mm_task_free_chunks(task);

	// Free the stack.
	if (task->stack_base != NULL)
		mm_cstack_destroy(task->stack_base, task->stack_size);

	// At last free the task struct.
	mm_pool_free(&mm_task_pool, task);

	LEAVE();
}

/**********************************************************************
 * Task utilities.
 **********************************************************************/

struct mm_task *
mm_task_getptr(mm_task_t id)
{
	return mm_pool_idx2ptr(&mm_task_pool, id);
}

mm_task_t NONNULL(1)
mm_task_getid(const struct mm_task *task)
{
	return mm_pool_ptr2idx(&mm_task_pool, task);
}

/* Set or change the task name. */
void NONNULL(1, 2)
mm_task_setname(struct mm_task *task, const char *name)
{
	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof task->name)
			len = sizeof task->name - 1;

		memcpy(task->name, name, len);
	}
	task->name[len] = 0;
}

void NONNULL(1)
mm_task_print_status(const struct mm_task *task)
{
	static char *state[] = { "blocked", "pending", "running", "invalid" };
	mm_log_fmt("%s: %s", task->name, state[task->state]);
#if ENABLE_TASK_LOCATION
	if (task->state == MM_TASK_BLOCKED || task->state == MM_TASK_PENDING)
		mm_log_fmt(" at %s(%s)", task->function, task->location);
#endif
	mm_log_fmt("\n");
}

/**********************************************************************
 * Task execution.
 **********************************************************************/

/* Switch to the next task in the run queue. */
static void
mm_task_switch(mm_task_state_t state)
{
	struct mm_core *core = mm_core_selfptr();

	// Move the currently running task to a new state.
	struct mm_task *old_task = core->task;
	ASSERT(old_task->state == MM_TASK_RUNNING);
	old_task->state = state;

	if (unlikely(state == MM_TASK_INVALID)) {
		// Add it to the dead task list.
		mm_list_append(&core->dead, &old_task->queue);
	} else {
		// Reset the priority that could have been temporary raised.
		old_task->priority = old_task->original_priority;
		if (state == MM_TASK_BLOCKED) {
			// Add it to the blocked task list.
			mm_list_append(&core->block, &old_task->queue);
		} else {
			// Add it to the run queue.
			mm_runq_put(&core->runq, old_task);
		}
	}

	// Execute requests associated with the core.
	mm_core_execute_requests(core, 1);

	// Get the next task from the run queue.  As long as this function
	// is called there is at least a boot task in the run queue.  So
	// there should never be a NULL value returned.
	struct mm_task *new_task = mm_runq_get(&core->runq);

	new_task->state = MM_TASK_RUNNING;
	core->task = new_task;

	// Switch to the new task relinquishing CPU control for a while.
	mm_cstack_switch(&old_task->stack_ctx, &new_task->stack_ctx);
	core->cswitch_count++;

	// Resume the task unless it has been canceled and it agrees to be
	// canceled asynchronously. In that case it quits here.
	mm_task_testcancel_asynchronous();
}

/* Queue a task for execution. */
void NONNULL(1)
mm_task_run(struct mm_task *task)
{
	ENTER();
	TRACE("queue task: [%d %s], state: %d, priority: %d",
	      mm_task_getid(task), mm_task_getname(task),
	      task->state, task->priority);
	ASSERT(task->core == mm_core_selfptr());
	ASSERT(task->priority < MM_PRIO_BOOT);

	if (task->state == MM_TASK_BLOCKED) {
		// Remove it from the blocked task list.
		mm_list_delete(&task->queue);
		// Add it to the run queue.
		task->state = MM_TASK_PENDING;
		mm_runq_put(&task->core->runq, task);
	}

	LEAVE();
}

/* Queue a task for execution with temporary raised priority. */
void NONNULL(1)
mm_task_hoist(struct mm_task *task, mm_priority_t priority)
{
	ENTER();
	TRACE("hoist task: [%d %s], state: %d, priority: %d, %d",
	      mm_task_getid(task), mm_task_getname(task),
	      task->state, task->priority, priority);
	ASSERT(task->core == mm_core_selfptr());
	ASSERT(task->priority < MM_PRIO_BOOT);

	if (task->state == MM_TASK_BLOCKED
	    || (task->state == MM_TASK_PENDING && task->priority > priority)) {

		if (task->state == MM_TASK_BLOCKED) {
			// Remove it from the blocked task list.
			mm_list_delete(&task->queue);
			task->state = MM_TASK_PENDING;
		} else {
			// Remove it from the run queue.
			mm_runq_delete(&task->core->runq, task);
		}

		if (task->priority > priority)
			task->priority = priority;

		// Add it to the run queue with raised priority.
		mm_runq_put(&task->core->runq, task);
	}

	LEAVE();
}

#if ENABLE_TASK_LOCATION

void NONNULL(1, 2)
mm_task_yield_at(const char *location, const char *function)
{
	ENTER();

	struct mm_task *task = mm_task_selfptr();
	task->location = location;
	task->function = function;

	mm_task_switch(MM_TASK_PENDING);

	LEAVE();
}

void NONNULL(1, 2)
mm_task_block_at(const char *location, const char *function)
{
	ENTER();

	struct mm_task *task = mm_task_selfptr();
	task->location = location;
	task->function = function;

	mm_task_switch(MM_TASK_BLOCKED);

	LEAVE();
}

#else

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

#endif

/* Finish the current task. */
void
mm_task_exit(mm_value_t result)
{
	struct mm_task *task = mm_task_selfptr();
	TRACE("exiting task '%s' with status %lu", task->name, (unsigned long) result);

	// Set the task result.
	task->result = result;

	// TODO: invalidate ports ?

	// Call the cleanup handlers.
	mm_task_cleanup(task);

	// At this point the task must not be in any queue.
#if ENABLE_TASK_IO_FLAGS
	ASSERT((task->flags & (MM_TASK_WAITING | MM_TASK_READING | MM_TASK_WRITING)) == 0);
#else
	ASSERT((task->flags & MM_TASK_WAITING) == 0);
#endif

	// Free the dynamic memory.
	mm_task_free_chunks(task);

	// Reset the task name.
	mm_task_setname(task, "dead");

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

	struct mm_task *task = mm_task_selfptr();
	int old_value = (task->flags & MM_TASK_CANCEL_DISABLE);
	if (likely(old_value != new_value)) {
		if (new_value) {
			task->flags |= MM_TASK_CANCEL_DISABLE;
		} else {
			task->flags &= ~MM_TASK_CANCEL_DISABLE;
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

	struct mm_task *task = mm_task_selfptr();
	int old_value = (task->flags & MM_TASK_CANCEL_ASYNCHRONOUS);
	if (likely(old_value != new_value)) {
		if (new_value) {
			task->flags |= MM_TASK_CANCEL_ASYNCHRONOUS;
			mm_task_testcancel_asynchronous();
		} else {
			task->flags &= ~MM_TASK_CANCEL_ASYNCHRONOUS;
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

	struct mm_task *task = mm_task_selfptr();
	int cp = (task->flags & MM_TASK_CANCEL_ASYNCHRONOUS);
	if (likely(cp == 0)) {
		task->flags |= MM_TASK_CANCEL_ASYNCHRONOUS;
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
		struct mm_task *task = mm_task_selfptr();
		task->flags &= ~MM_TASK_CANCEL_ASYNCHRONOUS;
	}

	LEAVE();
}

void NONNULL(1)
mm_task_cancel(struct mm_task *task)
{
	ENTER();

	task->flags |= MM_TASK_CANCEL_REQUIRED;
	if (unlikely(task->state == MM_TASK_RUNNING)) {
		ASSERT(task == mm_task_selfptr());
		mm_task_testcancel_asynchronous();
	} else {
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
	void *ptr = mm_private_alloc(size + sizeof(struct mm_list));

	/* Keep the allocated memory in the task's chunk list. */
	struct mm_task *task = mm_task_selfptr();
	mm_list_append(&task->chunks, (struct mm_link *) ptr);

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
		struct mm_link *link = (struct mm_link *) (((char *) ptr) - sizeof(struct mm_list));

		/* Remove it from the task's chunk list. */
		mm_list_delete(link);

		/* Free the memory. */
		mm_private_free(link);
	}

	LEAVE();
}
