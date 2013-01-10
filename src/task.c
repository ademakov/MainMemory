/*
 * task.c - MainMemory tasks.
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

#include "task.h"

#include "arch.h"
#include "pool.h"
#include "port.h"
#include "sched.h"
#include "stack.h"
#include "util.h"

#define MM_TASK_STACK_SIZE (28 * 1024)

/* The memory pool for tasks. */
static struct mm_pool mm_task_pool;

/* The list of tasks that were finished. */
static struct mm_list mm_dead_list;

/**********************************************************************
 * Global task data initialization and termination.
 **********************************************************************/

void
mm_task_init(void)
{
	ENTER();

	mm_pool_init(&mm_task_pool, "task", sizeof (struct mm_task));
	mm_list_init(&mm_dead_list);

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

	/* Execute the task using a fresh stack. */
	(mm_running_task)->start(mm_running_task->start_arg);

	/* Make sure that there is no return from this call as there is no
	 * valid stack frame after it. */
	mm_task_exit(0);
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
struct mm_task *
mm_task_create(const char *name, uint16_t flags, mm_routine start, uintptr_t start_arg)
{
	ENTER();

	struct mm_task *task;

	if (mm_list_empty(&mm_dead_list)) {
		/* Allocate a new task. */
		task = mm_pool_alloc(&mm_task_pool);

		/* Initialize the task name. */
		task->name = NULL;

		/* Allocate the task stack. */
		task->stack_size = MM_TASK_STACK_SIZE;
		task->stack_base = mm_stack_create(task->stack_size);
	} else {
		/* Reuse a dead task. */
		struct mm_list *link = mm_list_head(&mm_dead_list);
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
	ASSERT(task->state != MM_TASK_RUNNING);

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
	ASSERT(task->state != MM_TASK_PENDING);

	mm_list_append(&mm_dead_list, &task->queue);

	LEAVE();
}

/* Finish the current task. */
void
mm_task_exit(int status)
{
	ENTER();
	TRACE("exiting task '%s' with status %d", mm_running_task->name, status);

	// TODO: invalidate ports ?

	/* Free the dynamic memory. */
	mm_task_free_chunks(mm_running_task);

	/* Give the control to still running tasks. */
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
