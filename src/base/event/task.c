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

#include "base/event/task.h"

#include "base/report.h"
#include "base/memory/memory.h"

static struct mm_event_task_ring * NONNULL(1)
mm_event_task_append_ring(struct mm_event_task_list *task_list)
{
	ENTER();

	struct mm_event_task_ring *ring = mm_private_alloc(sizeof(struct mm_event_task_ring));
	ring->head = 0;
	ring->tail = 0;
	mm_queue_append(&task_list->list, &ring->link);

	task_list->total_ring_count++;

	LEAVE();
	return ring;
}

static struct mm_event_task_ring * NONNULL(1)
mm_event_task_next_ring(struct mm_event_task_list *task_list)
{
	struct mm_qlink *link = mm_queue_remove(&task_list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	mm_private_free(ring);
	return containerof(mm_queue_head(&task_list->list), struct mm_event_task_ring, link);
}

void NONNULL(1)
mm_event_task_prepare(struct mm_event_task_list *task_list)
{
	ENTER();

	mm_queue_prepare(&task_list->list);
	task_list->total_task_count = 0;
	task_list->total_ring_count = 0;

	mm_event_task_append_ring(task_list);

	LEAVE();
}

void NONNULL(1)
mm_event_task_cleanup(struct mm_event_task_list *task_list)
{
	ENTER();

	do {
		struct mm_qlink *link = mm_queue_remove(&task_list->list);
		struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
		mm_private_free(ring);
	} while (!mm_queue_empty(&task_list->list));

	LEAVE();
}

void NONNULL(1, 2)
mm_event_task_add(struct mm_event_task_list *task_list, struct mm_event_task *task, mm_value_t arg)
{
	ENTER();

	struct mm_qlink *link = mm_queue_tail(&task_list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	if ((ring->tail - ring->head) == MM_EVENT_TASK_RING_SIZE)
		ring = mm_event_task_append_ring(task_list);
	task_list->total_task_count++;

	uint32_t index = ring->tail++ & (MM_EVENT_TASK_RING_SIZE - 1);
	ring->ring[index].task = task;
	ring->ring[index].task_arg = arg;

	LEAVE();
}

bool NONNULL(1, 2, 3)
mm_event_task_get(struct mm_event_task_list *task_list, struct mm_event_task **task, mm_value_t *arg)
{
	struct mm_qlink *link = mm_queue_head(&task_list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	if (ring->tail == ring->head) {
		if (mm_queue_is_tail(&ring->link))
			return false;
		ring = mm_event_task_next_ring(task_list);
	}

	uint32_t index = ring->head++ & (MM_EVENT_TASK_RING_SIZE - 1);
	*task = ring->ring[index].task;
	*arg = ring->ring[index].task_arg;
	return true;
}
