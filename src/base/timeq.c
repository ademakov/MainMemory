/*
 * base/timeq.c - MainMemory time queue.
 *
 * Copyright (C) 2013-2015,2019  Ivan Demakov, Aleksey Demakov
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

#include "base/timeq.h"

#include "base/report.h"

#define MM_TIMEQ_T1_WIDTH_MIN	1
#define MM_TIMEQ_T1_COUNT_MIN	4

struct mm_timeq * NONNULL(1)
mm_timeq_create(mm_arena_t arena)
{
	struct mm_timeq* timeq = mm_arena_alloc(arena, sizeof(struct mm_timeq));
	mm_timeq_prepare(timeq, arena);
	return timeq;
}

void NONNULL(1)
mm_timeq_destroy(struct mm_timeq *timeq)
{
	mm_arena_t arena = timeq->arena;
	mm_timeq_cleanup(timeq);
	mm_arena_free(arena, timeq);
}

void NONNULL(1)
mm_timeq_prepare(struct mm_timeq *timeq, mm_arena_t arena)
{
	mm_list_prepare(&timeq->fe);

	timeq->t1 = NULL;
	timeq->t1_start = MM_TIMEVAL_MIN;
	timeq->t1_width = 0;
	timeq->t1_count = 0;
	timeq->t1_index = 0;

	mm_list_prepare(&timeq->t2);
	timeq->t2_start = MM_TIMEVAL_MIN;
	timeq->t2_min = MM_TIMEVAL_MAX;
	timeq->t2_max = MM_TIMEVAL_MIN;
	timeq->t2_num = 0;

	timeq->t1_width_min = MM_TIMEQ_T1_WIDTH_MIN;
	timeq->t1_width_max = 0;
	timeq->t1_count_min = MM_TIMEQ_T1_COUNT_MIN;
	timeq->t1_count_max = 0;

	timeq->arena = arena;
}

void NONNULL(1)
mm_timeq_cleanup(struct mm_timeq *timeq)
{
	mm_arena_free(timeq->arena, timeq->t1);
}

void NONNULL(1)
mm_timeq_set_min_bucket_width(struct mm_timeq *timeq, mm_timeval_t n)
{
	timeq->t1_width_min = max(MM_TIMEQ_T1_WIDTH_MIN, n);
}

void NONNULL(1)
mm_timeq_set_max_bucket_width(struct mm_timeq *timeq, mm_timeval_t n)
{
	timeq->t1_width_max = n;
}

void NONNULL(1)
mm_timeq_set_min_bucket_count(struct mm_timeq *timeq, int n)
{
	timeq->t1_count_min = max(MM_TIMEQ_T1_COUNT_MIN, n);
}

void NONNULL(1)
mm_timeq_set_max_bucket_count(struct mm_timeq *timeq, int n)
{
	timeq->t1_count_max = n;
}

static void
mm_timeq_insert_fe(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	struct mm_link *fe_queue_link = &timeq->fe.base;
	while (!mm_list_is_head(&timeq->fe, fe_queue_link)) {
		struct mm_link *prev_link = fe_queue_link->prev;
		struct mm_timeq_entry *prev_entry
			= containerof(prev_link, struct mm_timeq_entry, queue);
		if (prev_entry->value <= entry->value)
			break;
		fe_queue_link = prev_link;
	}

	mm_list_insert_prev(fe_queue_link, &entry->queue);
	entry->index = MM_TIMEQ_INDEX_FE;
	timeq->fe_num++;

	DEBUG("entry: %p, fe num: %d", entry, timeq->fe_num);
}

static void
mm_timeq_insert_t1(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	int index = timeq->t1_index + (entry->value - timeq->t1_start) / timeq->t1_width;
	ASSERT(index < timeq->t1_count);

	mm_list_append(&timeq->t1[index], &entry->queue);
	entry->index = index;

	DEBUG("entry: %p, t1 index: %d", entry, index);
}

static void
mm_timeq_insert_t2(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	mm_list_append(&timeq->t2, &entry->queue);
	entry->index = MM_TIMEQ_INDEX_T2;
	timeq->t2_num++;

	if (timeq->t2_min > entry->value) {
		timeq->t2_min = entry->value;
	}
	if (timeq->t2_max < entry->value) {
		timeq->t2_max = entry->value;
	}

	DEBUG("entry: %p, t2 num: %d", entry, timeq->t2_num);
}

void NONNULL(1)
mm_timeq_insert(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	ASSERT(entry->index == MM_TIMEQ_INDEX_NO);

	if (timeq->t2_start <= entry->value) {
		mm_timeq_insert_t2(timeq, entry);
	} else if (timeq->t1_start <= entry->value) {
		mm_timeq_insert_t1(timeq, entry);
	} else {
		mm_timeq_insert_fe(timeq, entry);
	}
}

void NONNULL(1)
mm_timeq_delete(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	DEBUG("entry: %p", entry);
	ASSERT(entry->index != MM_TIMEQ_INDEX_NO);

	if (entry->index == MM_TIMEQ_INDEX_FE) {
		timeq->fe_num--;
	} else if (entry->index == MM_TIMEQ_INDEX_T2) {
		timeq->t2_num--;
		// TODO: take into account that t2_min and t2_max might be
		// changed after this.
	}

	mm_list_delete(&entry->queue);
	entry->index = MM_TIMEQ_INDEX_NO;
}

struct mm_timeq_entry * NONNULL(1)
mm_timeq_getmin(struct mm_timeq *timeq)
{
	struct mm_timeq_entry *entry;

restart:
	if (!mm_list_empty(&timeq->fe)) {

		struct mm_link *link = mm_list_head(&timeq->fe);
		entry = containerof(link, struct mm_timeq_entry, queue);
		DEBUG("fe entry: %p", entry);

	} else {
		while (timeq->t1_index < timeq->t1_count
		       && mm_list_empty(&timeq->t1[timeq->t1_index])) {
			timeq->t1_index++;
			timeq->t1_start += timeq->t1_width;
		}

		if (timeq->t1_index < timeq->t1_count) {

			/* The bucket is not empty. */
			struct mm_link *head = mm_list_head(&timeq->t1[timeq->t1_index]);
			struct mm_link *tail = mm_list_tail(&timeq->t1[timeq->t1_index]);

			/* If the bucket has exactly one item, return the item. */
			/* In other case, move all items in front end structure. */
			if (head == tail) {

				entry = containerof(head, struct mm_timeq_entry, queue);
				DEBUG("entry: %p, t1 index: %d", entry, timeq->t1_index);

			} else {

				DEBUG("erase t1 index: %d", timeq->t1_index);
				mm_list_prepare(&timeq->t1[timeq->t1_index]);

				timeq->t1_index++;
				timeq->t1_start += timeq->t1_width;

				for (;;) {
					struct mm_link *next = head->next;

					entry = containerof(head, struct mm_timeq_entry, queue);
					mm_timeq_insert_fe(timeq, entry);

					if (head == tail)
						goto restart;

					head = next;
				}
			}

		} else if (timeq->t2_num == 1) {

			/* All buckets are empty and only one item in T2 */
			struct mm_link *link = mm_list_head(&timeq->t2);
			entry = containerof(link, struct mm_timeq_entry, queue);
			DEBUG("t2 entry: %p", entry);

		} else if (timeq->t2_num > 1) {

			/* All buckets are empty, move all items in T1 (as much as allowed) */
			mm_timeval_t width = (timeq->t2_max - timeq->t2_min) / timeq->t2_num;
			if (width < timeq->t1_width_min)
				width = timeq->t1_width_min;
			else if (timeq->t1_width_max && width > timeq->t1_width_max)
				width = timeq->t1_width_max;
			DEBUG("width: %d", (int) width);

			int count = (timeq->t2_max - timeq->t2_min) / width;
			if (count < timeq->t1_count_min)
				count = timeq->t1_count_min;
			else if (timeq->t1_count_max && count > timeq->t1_count_max)
				count = timeq->t1_count_max;
			DEBUG("count: %d", count);

			if (timeq->t1_count < count) {
				DEBUG("t1 resize %d to %d", timeq->t1_count, count);
				timeq->t1 = mm_arena_realloc(
					timeq->arena,
					timeq->t1, count * sizeof(struct mm_list));
				// After realloc all lists need to be initialized
				// including re-initialization of the lists that
				// there were before as the next and prev fields
				// for them point to the old locations. Assuming
				// that the lists are empty.
				for (int i = 0; i < count; ++i)
					mm_list_prepare(&timeq->t1[i]);
				timeq->t1_count = count;
			}

			timeq->t1_width = width;
			timeq->t1_start = timeq->t2_min;
			timeq->t1_index = timeq->t1_count - count;

			timeq->t2_start = timeq->t1_start + width * count;
			timeq->t2_min = MM_TIMEVAL_MAX;
			timeq->t2_max = MM_TIMEVAL_MIN;
			timeq->t2_num = 0;

			struct mm_link *head = mm_list_head(&timeq->t2);
			struct mm_link *tail = mm_list_tail(&timeq->t2);
			mm_list_prepare(&timeq->t2);
			DEBUG("t2 erase");

			for (;;) {
				struct mm_link *next = head->next;

				entry = containerof(head, struct mm_timeq_entry, queue);
				if (timeq->t2_start <= entry->value) {
					mm_timeq_insert_t2(timeq, entry);
				} else {
					mm_timeq_insert_t1(timeq, entry);
				}

				if (head == tail)
					goto restart;

				head = next;
			}

		} else {
			entry = NULL;
		}
	}

	return entry;
}
