/*
 * base/event/task.h - MainMemory asynchronous tasks.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_EVENT_TASK_H
#define BASE_EVENT_TASK_H

#include "common.h"
#include "base/list.h"

/* Forward declaration. */
struct mm_event_listener;
struct mm_event_task;

/**********************************************************************
 * Abstract asynchronous task.
 **********************************************************************/

#if 0
#define MM_EVENT_TASK_VTABLE(name, e, c, r)			\
	static const struct mm_event_task_vtable name = {	\
		.execute = (mm_event_task_execute_t) e,		\
		.complete = (mm_event_task_complete_t) c,	\
		.reassign = (mm_event_task_reassign_t) r,	\
	}
#endif

/* A task routine. */
typedef mm_value_t (*mm_event_task_execute_t)(struct mm_event_task *task, mm_value_t arg);

/* A task completion notification routine. */
typedef void (*mm_event_task_complete_t)(struct mm_event_task *task, mm_value_t result);

/* A task reassignment routine. */
typedef bool (*mm_event_task_reassign_t)(struct mm_event_task *task, struct mm_event_listener *listener);

struct mm_event_task
{
	mm_event_task_execute_t execute;
	mm_event_task_complete_t complete;
	mm_event_task_reassign_t reassign;
};

/**********************************************************************
 * Task ring buffer.
 **********************************************************************/

/* This value must be a power of two. */
#define MM_EVENT_TASK_RING_SIZE (16)

struct mm_event_task_node
{
	struct mm_event_task *task;
	mm_value_t task_arg;
};

struct mm_event_task_ring
{
	uint32_t head;
	uint32_t tail;
	struct mm_qlink link;
	struct mm_event_task_node ring[MM_EVENT_TASK_RING_SIZE];
};

/**********************************************************************
 * Task queue.
 **********************************************************************/

struct mm_event_task_list
{
	/* Task rings. */
	struct mm_queue list;
	/* Statistics. */
	uint64_t total_task_count;
	uint64_t total_ring_count;
};

void NONNULL(1)
mm_event_task_prepare(struct mm_event_task_list *task_list);

void NONNULL(1)
mm_event_task_cleanup(struct mm_event_task_list *task_list);

void NONNULL(1, 2)
mm_event_task_add(struct mm_event_task_list *task_list, struct mm_event_task *task, mm_value_t arg);

bool NONNULL(1, 2, 3)
mm_event_task_get(struct mm_event_task_list *task_list, struct mm_event_task **task, mm_value_t *arg);

#endif /* BASE_EVENT_TASK_H */
