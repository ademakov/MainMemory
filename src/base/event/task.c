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

void NONNULL(1)
mm_event_task_list_prepare(struct mm_event_task_list *task_list)
{
	mm_queue_prepare(&task_list->list);
	task_list->head_count = 0;
	task_list->tail_count = 0;
	task_list->ring_count = 0;

	mm_event_task_append_ring(task_list);
}

void NONNULL(1)
mm_event_task_list_cleanup(struct mm_event_task_list *task_list)
{
	do {
		struct mm_qlink *link = mm_queue_remove(&task_list->list);
		struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
		mm_common_free(ring);
	} while (!mm_queue_empty(&task_list->list));
}

struct mm_event_task_ring * NONNULL(1)
mm_event_task_append_ring(struct mm_event_task_list *task_list)
{
	struct mm_event_task_ring *ring = mm_common_alloc(sizeof(struct mm_event_task_ring));
	ring->head = 0;
	ring->tail = 0;
	mm_queue_append(&task_list->list, &ring->link);
	task_list->ring_count++;
	return ring;
}

struct mm_event_task_ring * NONNULL(1)
mm_event_task_next_ring(struct mm_event_task_list *task_list)
{
	struct mm_qlink *link = mm_queue_remove(&task_list->list);
	struct mm_event_task_ring *ring = containerof(link, struct mm_event_task_ring, link);
	mm_common_free(ring);
	return containerof(mm_queue_head(&task_list->list), struct mm_event_task_ring, link);
}

/**********************************************************************
 * Task initialization.
 **********************************************************************/

void
mm_event_complete_noop(mm_value_t arg UNUSED, mm_value_t result UNUSED)
{
}

bool
mm_event_reassign_on(mm_value_t arg UNUSED, struct mm_event_listener* listener UNUSED)
{
	return true;
}

bool
mm_event_reassign_off(mm_value_t arg UNUSED, struct mm_event_listener* listener UNUSED)
{
	return false;
}
