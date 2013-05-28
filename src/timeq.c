/*
 * timeq.c - MainMemory time queue.
 *
 * Copyright (C) 2013  Ivan Demakov, Aleksey Demakov
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

#include "timeq.h"

#include "alloc.h"
#include "trace.h"

#define MM_TIMEQ_T1_WIDTH_MIN	1
#define MM_TIMEQ_T1_COUNT_MIN	4

/*
 * The algorithm here is similar to the one described in the following paper:
 *
 * Rick SM Goh and I L-J Thng,
 * “MList: An Efficient Pending Event Set Structure For Discrete Event Simulation”
 *
 * The first intent was to implement the Ladder Queue algorithm, but so far it
 * seems an overkill. If the current algorithm does not work well then either
 * the Ladder Queue or some alternative like implicit heap should be tried.
 */

struct mm_timeq
{
    struct mm_list fe;		/* front end entries */
    int fe_num;			/* number of entries in the front end */

    struct mm_list *t1;		/* tier 1 entries */
    mm_timeval_t t1_start;	/* T1 buckets base */
    mm_timeval_t t1_width;	/* T1 bucket width */
    int t1_count;		/* number of all T1 buckets */
    int t1_index;		/* index of the first used T1 bucket */

    struct mm_list t2;		/* tier 2 entries */
    mm_timeval_t t2_start;	/* t2 buckets base */
    mm_timeval_t t2_min;	/* minimum timestamp of all events in T2 */
    mm_timeval_t t2_max;	/* maximum timestamp of all events in T2 */
    int t2_num;			/* number of entries in the tier 2 */

    mm_timeval_t t1_width_min;
    mm_timeval_t t1_width_max;
    int t1_count_min;
    int t1_count_max;
};


struct mm_timeq *
mm_timeq_create(void)
{
	ENTER();

	struct mm_timeq *timeq = mm_core_alloc(sizeof(struct mm_timeq));

	mm_list_init(&timeq->fe);

	timeq->t1 = NULL;
	timeq->t1_start = MM_TIMEVAL_MIN;
	timeq->t1_width = 0;
	timeq->t1_count = 0;
	timeq->t1_index = 0;

	mm_list_init(&timeq->t2);
	timeq->t2_start = MM_TIMEVAL_MIN;
	timeq->t2_min = MM_TIMEVAL_MAX;
	timeq->t2_max = MM_TIMEVAL_MIN;
	timeq->t2_num = 0;

	timeq->t1_width_min = MM_TIMEQ_T1_WIDTH_MIN;
	timeq->t1_width_max = 0;
	timeq->t1_count_min = MM_TIMEQ_T1_COUNT_MIN;
	timeq->t1_count_max = 0;

	LEAVE();
	return timeq;
}

void
mm_timeq_destroy(struct mm_timeq *timeq)
{
	ENTER();
	ASSERT(mm_list_empty(&timeq->fe));
	ASSERT(timeq->t1_index <= timeq->t1_count);
	ASSERT(mm_list_empty(&timeq->t2));

	mm_core_free(timeq->t1);
	mm_core_free(timeq);

	LEAVE();
}

void
mm_timeq_set_min_bucket_width(struct mm_timeq *timeq, mm_timeval_t n)
{
	timeq->t1_width_min = max(MM_TIMEQ_T1_WIDTH_MIN, n);
}

void
mm_timeq_set_max_bucket_width(struct mm_timeq *timeq, mm_timeval_t n)
{
	timeq->t1_width_max = n;
}

void
mm_timeq_set_min_bucket_count(struct mm_timeq *timeq, int n)
{
	timeq->t1_count_min = max(MM_TIMEQ_T1_COUNT_MIN, n);
}

void
mm_timeq_set_max_bucket_count(struct mm_timeq *timeq, int n)
{
	timeq->t1_count_max = n;
}

static void
mm_timeq_insert_fe(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	struct mm_list *fe_queue_link = &timeq->fe;
	while (mm_list_has_prev(&timeq->fe, fe_queue_link)) {
		struct mm_list *prev_link = fe_queue_link->prev;
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

void
mm_timeq_insert(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	ENTER();
	ASSERT(entry->index == MM_TIMEQ_INDEX_NO);

	if (timeq->t2_start <= entry->value) {
		mm_timeq_insert_t2(timeq, entry);
	} else if (timeq->t1_start <= entry->value) {
		mm_timeq_insert_t1(timeq, entry);
	} else {
		mm_timeq_insert_fe(timeq, entry);
	}

	LEAVE();
}

void
mm_timeq_delete(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	ENTER();
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

	LEAVE();
}

struct mm_timeq_entry *
mm_timeq_getmin(struct mm_timeq *timeq)
{
	ENTER();

	struct mm_timeq_entry *entry;

restart:
	if (!mm_list_empty(&timeq->fe)) {

		struct mm_list *link = mm_list_head(&timeq->fe);
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
			struct mm_list *head = mm_list_head(&timeq->t1[timeq->t1_index]);
			struct mm_list *tail = mm_list_tail(&timeq->t1[timeq->t1_index]);

			/* If the bucket has exactly one item, return the item. */
			/* In other case, move all items in front end structure. */
			if (head == tail) {

				entry = containerof(head, struct mm_timeq_entry, queue);
				DEBUG("entry: %p, t1 index: %d", entry, timeq->t1_index);

			} else {
				DEBUG("cleave t1 index: %d", timeq->t1_index);

				timeq->t1_index++;
				timeq->t1_start += timeq->t1_width;

				mm_list_cleave(head, tail);

				for (;;) {
					struct mm_list *next = head->next;

					entry = containerof(head, struct mm_timeq_entry, queue);
					mm_timeq_insert_fe(timeq, entry);

					if (head == tail)
						goto restart;

					head = next;
				}
			}

		} else if (timeq->t2_num == 1) {

			/* All buckets are empty and only one item in T2 */
			struct mm_list *link = mm_list_head(&timeq->t2);
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
				timeq->t1 = mm_core_realloc(timeq->t1, count * sizeof(struct mm_list));
				// After realloc all lists need to be initialized
				// including re-initialization of the lists that
				// there were before as the next and prev fields
				// for them point to the old locations. Assuming
				// that the lists are empty.
				for (int i = 0; i < count; ++i)
					mm_list_init(&timeq->t1[i]);
				timeq->t1_count = count;
			}

			timeq->t1_width = width;
			timeq->t1_start = timeq->t2_min;
			timeq->t1_index = timeq->t1_count - count;

			timeq->t2_start = timeq->t1_start + width * count;
			timeq->t2_min = MM_TIMEVAL_MAX;
			timeq->t2_max = MM_TIMEVAL_MIN;
			timeq->t2_num = 0;

			struct mm_list *head = mm_list_head(&timeq->t2);
			struct mm_list *tail = mm_list_tail(&timeq->t2);
			mm_list_cleave(head, tail);
			DEBUG("t2 cleave");

			for (;;) {
				struct mm_list *next = head->next;

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

	LEAVE();
	return entry;
}
