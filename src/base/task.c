/*
 * base/task.h - MainMemory asynchronous tasks.
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

#include "base/task.h"

#include "base/logger.h"
#include "base/report.h"
#include "base/context.h"
#include "base/memory/memory.h"

/**********************************************************************
 * Task submission.
 **********************************************************************/

static void
mm_task_add_1(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	mm_task_list_add(&context->tasks, (mm_task_t) arguments[0], arguments[1]);

	LEAVE();
}

static void
mm_task_add_2(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	mm_task_list_add(&context->tasks, (mm_task_t) arguments[0], arguments[1]);
	mm_task_list_add(&context->tasks, (mm_task_t) arguments[2], arguments[3]);

	LEAVE();
}

static void
mm_task_add_3(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	mm_task_list_add(&context->tasks, (mm_task_t) arguments[0], arguments[1]);
	mm_task_list_add(&context->tasks, (mm_task_t) arguments[2], arguments[3]);
	mm_task_list_add(&context->tasks, (mm_task_t) arguments[4], arguments[5]);

	LEAVE();
}

static void
mm_task_submit(struct mm_context *context, struct mm_task_slot* tasks, uint32_t count)
{
	ENTER();
	DEBUG("count: %d", count);

	switch (count)
	{
	case 1:
		mm_event_call_2(context, mm_task_add_1,
				(uintptr_t) tasks[0].task,
				tasks[0].task_arg);
		break;
	case 2:
		mm_event_call_4(context, mm_task_add_2,
				(uintptr_t) tasks[0].task,
				tasks[0].task_arg,
				(uintptr_t) tasks[1].task,
				tasks[1].task_arg);
		break;
	case 3:
		mm_event_call_6(context, mm_task_add_3,
				(uintptr_t) tasks[0].task,
				tasks[0].task_arg,
				(uintptr_t) tasks[1].task,
				tasks[1].task_arg,
				(uintptr_t) tasks[2].task,
				tasks[2].task_arg);
		break;
	}

	LEAVE();
}

/**********************************************************************
 * Task management.
 **********************************************************************/

void NONNULL(1)
mm_task_stats(struct mm_task_stats *stats)
{
	mm_log_fmt(" tasks=%llu task-rings=%llu reassign-send=[%llu %llu %llu %llu]\n",
		   (unsigned long long) mm_counter_shared_load(&stats->tail_count),
		   (unsigned long long) mm_counter_shared_load(&stats->ring_count),
		   (unsigned long long) mm_counter_shared_load(&stats->send_count[0]),
		   (unsigned long long) mm_counter_shared_load(&stats->send_count[1]),
		   (unsigned long long) mm_counter_shared_load(&stats->send_count[2]),
		   (unsigned long long) mm_counter_shared_load(&stats->send_count[3]));
}

void NONNULL(1)
mm_task_list_prepare(struct mm_task_list *list)
{
	mm_queue_prepare(&list->list);
	mm_counter_prepare(&list->stats.head_count, 0);
	mm_counter_prepare(&list->stats.tail_count, 0);
	mm_counter_prepare(&list->stats.ring_count, 0);

	for (int i = 0; i < MM_TASK_SEND_MAX; i++)
		mm_counter_prepare(&list->stats.send_count[i], 0);

	mm_task_list_add_ring(list);
}

void NONNULL(1)
mm_task_list_cleanup(struct mm_task_list *list)
{
	do {
		struct mm_qlink *link = mm_queue_remove(&list->list);
		struct mm_task_ring *ring = containerof(link, struct mm_task_ring, link);
		mm_common_free(ring);
	} while (!mm_queue_empty(&list->list));
}

struct mm_task_ring * NONNULL(1)
mm_task_list_add_ring(struct mm_task_list *list)
{
	struct mm_task_ring *ring = mm_common_alloc(sizeof(struct mm_task_ring));
	ring->head = 0;
	ring->tail = 0;
	mm_queue_append(&list->list, &ring->link);
	mm_counter_local_inc(&list->stats.ring_count);
	return ring;
}

struct mm_task_ring * NONNULL(1)
mm_task_list_get_ring(struct mm_task_list *list)
{
	struct mm_qlink *link = mm_queue_remove(&list->list);
	struct mm_task_ring *ring = containerof(link, struct mm_task_ring, link);
	mm_common_free(ring);
	return containerof(mm_queue_head(&list->list), struct mm_task_ring, link);
}

bool NONNULL(1, 2)
mm_task_list_reassign(struct mm_task_list *list, struct mm_context *target)
{
	ENTER();

	uint32_t count = 0;
	struct mm_task_slot tasks[MM_TASK_SEND_MAX];

	struct mm_qlink *link = mm_queue_head(&list->list);
	struct mm_task_ring *ring = containerof(link, struct mm_task_ring, link);
	do {
		if (ring->tail == ring->head) {
			if (mm_queue_is_tail(&ring->link))
				break;
			ring = mm_task_list_get_ring(list);
		}

		uint32_t index = ring->head & (MM_TASK_RING_SIZE - 1);
		struct mm_task_slot *slot = &ring->ring[index];
		if (!(slot->task->reassign)(slot->task_arg, target))
			break;

		tasks[count++] = *slot;
		ring->head++;

	} while (count < MM_TASK_SEND_MAX);

	mm_counter_local_add(&list->stats.head_count, count);
	mm_counter_local_inc(&list->stats.send_count[count]);
	mm_task_submit(target, tasks, count);

	LEAVE();
	return count == MM_TASK_SEND_MAX;
}

/**********************************************************************
 * Task initialization.
 **********************************************************************/

void
mm_task_complete_noop(mm_value_t arg UNUSED, mm_value_t result UNUSED)
{
}

bool
mm_task_reassign_on(mm_value_t arg UNUSED, struct mm_context *context UNUSED)
{
	return true;
}

bool
mm_task_reassign_off(mm_value_t arg UNUSED, struct mm_context *context UNUSED)
{
	return false;
}
