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
#include "base/counter.h"
#include "base/list.h"

/* Forward declarations. */
struct mm_event_listener;

/**********************************************************************
 * Abstract asynchronous task.
 **********************************************************************/

/* A task execution routine. */
typedef mm_value_t (*mm_event_execute_t)(mm_value_t arg);

/* A task completion routine. */
typedef void (*mm_event_complete_t)(mm_value_t arg, mm_value_t result);

/* A task reassignment routine. */
typedef bool (*mm_event_reassign_t)(mm_value_t arg, struct mm_event_listener *listener);

/* A set of routines for a task. */
struct mm_event_task
{
	mm_event_execute_t execute;
	mm_event_complete_t complete;
	mm_event_reassign_t reassign;
};

typedef const struct mm_event_task *mm_event_task_t;

/* Deferred task invocation information. */
struct mm_event_task_slot
{
	mm_event_task_t task;
	mm_value_t task_arg;
};

/**********************************************************************
 * Task ring buffer.
 **********************************************************************/

/* This value must be a power of two. */
#define MM_EVENT_TASK_RING_SIZE (256)

/* Fixed size ring buffer for task storage. */
struct mm_event_task_ring
{
	uint32_t head;
	uint32_t tail;
	struct mm_qlink link;
	struct mm_event_task_slot ring[MM_EVENT_TASK_RING_SIZE];
};

/**********************************************************************
 * Task queue.
 **********************************************************************/

/* The maximum number of tasks that could be sent to another listener at once. */
#define MM_EVENT_TASK_SEND_MAX	(3)

/* Flexible task storage that normally contains one ring buffer but might add more on demand. */
struct mm_event_task_list
{
	/* Task rings. */
	struct mm_queue list;
	/* Statistics. */
	mm_counter_t head_count;
	mm_counter_t tail_count;
	mm_counter_t ring_count;
	mm_counter_t send_count[MM_EVENT_TASK_SEND_MAX + 1];
};

void NONNULL(1)
mm_event_task_list_prepare(struct mm_event_task_list *list);

void NONNULL(1)
mm_event_task_list_cleanup(struct mm_event_task_list *list);

struct mm_event_task_ring * NONNULL(1)
mm_event_task_list_add_ring(struct mm_event_task_list *list);

struct mm_event_task_ring * NONNULL(1)
mm_event_task_list_get_ring(struct mm_event_task_list *list);

static inline size_t NONNULL(1)
mm_event_task_list_size(struct mm_event_task_list *list)
{
	uint64_t head = mm_counter_local_load(&list->head_count);
	uint64_t tail = mm_counter_local_load(&list->tail_count);
	return head - tail;
}

static inline size_t NONNULL(1)
mm_event_task_peer_list_size(struct mm_event_task_list *list)
{
	uint64_t head = mm_counter_shared_load(&list->head_count);
	uint64_t tail = mm_counter_shared_load(&list->tail_count);
	return head - tail;
}

static inline bool NONNULL(1)
mm_event_task_list_empty(struct mm_event_task_list *list)
{
	uint64_t head = mm_counter_local_load(&list->head_count);
	uint64_t tail = mm_counter_local_load(&list->tail_count);
	return head == tail;
}

static inline void NONNULL(1, 2)
mm_event_task_list_add(struct mm_event_task_list *list, mm_event_task_t task, mm_value_t arg)
{
	struct mm_qlink *link = mm_queue_tail(&list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	if ((ring->tail - ring->head) == MM_EVENT_TASK_RING_SIZE)
		ring = mm_event_task_list_add_ring(list);

	uint32_t index = ring->tail & (MM_EVENT_TASK_RING_SIZE - 1);
	ring->ring[index].task = task;
	ring->ring[index].task_arg = arg;

	ring->tail++;
	mm_counter_local_inc(&list->tail_count);
}

static inline bool NONNULL(1, 2)
mm_event_task_list_get(struct mm_event_task_list *list, struct mm_event_task_slot *slot)
{
	struct mm_qlink *link = mm_queue_head(&list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	if (ring->tail == ring->head) {
		if (mm_queue_is_tail(&ring->link))
			return false;
		ring = mm_event_task_list_get_ring(list);
	}

	uint32_t index = ring->head & (MM_EVENT_TASK_RING_SIZE - 1);
	*slot = ring->ring[index];

	ring->head++;
	mm_counter_local_inc(&list->head_count);

	return true;
}

bool NONNULL(1, 2)
mm_event_task_list_reassign(struct mm_event_task_list *list, struct mm_event_listener *target);

/**********************************************************************
 * Task initialization.
 **********************************************************************/

#define MM_EVENT_TASK(name, e, c, r)				\
	static const struct mm_event_task name = {		\
		.execute = (mm_event_execute_t) e,		\
		.complete = (mm_event_complete_t) c,		\
		.reassign = (mm_event_reassign_t) r,		\
	}

void mm_event_complete_noop(mm_value_t arg, mm_value_t result);
bool mm_event_reassign_on(mm_value_t arg, struct mm_event_listener* listener);
bool mm_event_reassign_off(mm_value_t arg, struct mm_event_listener* listener);

static inline void NONNULL(1, 2, 3, 4)
mm_event_task_prepare(struct mm_event_task *task, mm_event_execute_t execute,
		      mm_event_complete_t complete, mm_event_reassign_t reassign)
{
	task->execute = execute;
	task->complete = complete;
	task->reassign = reassign;
}

static inline void NONNULL(1, 2)
mm_event_task_prepare_simple(struct mm_event_task *task, mm_event_execute_t execute, bool reassign)
{
	mm_event_task_prepare(task, execute, mm_event_complete_noop,
			      reassign ? mm_event_reassign_on : mm_event_reassign_off);
}

#endif /* BASE_EVENT_TASK_H */
