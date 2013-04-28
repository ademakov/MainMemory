/*
 * timeq.c - MainMemory time queue.
 *
 * Copyright (C) 2013  Ivan Demakov, Aleksey Demakov.
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

#include "util.h"


struct mm_timeq
{
    struct mm_list fe;		/* front end list */
    int fe_num;

    struct mm_list *t1;		/* T1 structure */
    mm_timeval_t t1_start;	/* used to calculate the bucket */
    mm_timeval_t t1_cur;	/* minimum timestamp threshold of events in T1 */
    int t1_size;		/* size of bucket array */
    int t1_used;		/* first used bucket */

    struct mm_list t2;		/* t2 list */
    mm_timeval_t t2_max;	/* maximum timestamp of all events in T2 */
    mm_timeval_t t2_min;	/* minimum timestamp of all events in T2 */
    mm_timeval_t t2_cur;	/* minimum timestamp threshold of events in T2 */
    int t2_num;			/* number of events in T2 */
};


struct mm_timeq *
mm_timeq_create(void)
{
	ENTER();

	struct mm_timeq *timeq = mm_alloc(sizeof(struct mm_timeq));

	mm_list_init(&timeq->fe);

	timeq->t1 = NULL;
	timeq->t1_start = 0;
	timeq->t1_cur = MM_TIMEVAL_MIN;
	timeq->t1_size = 0;
	timeq->t1_used = 0;

	mm_list_init(&timeq->t2);
	timeq->t2_max = MM_TIMEVAL_MIN;
	timeq->t2_min = MM_TIMEVAL_MAX;
	timeq->t2_cur = MM_TIMEVAL_MIN;
	timeq->t2_num = 0;

	LEAVE();
	return timeq;
}

void
mm_timeq_destroy(struct mm_timeq *timeq)
{
	ENTER();
	ASSERT(mm_list_empty(timeq->fe));
	ASSERT(timeq->t1_used >= timeq->t1_size);
	ASSERT(mm_list_empty(timeq->t2));

	mm_free(timeq->t1);
	mm_free(timeq);

	LEAVE();
}

static void
mm_timeq_insert_fe(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	struct mm_list *fe_queue_link = &timeq->fe;
	while (mm_list_has_prev(&timeq->fe, fe_queue_link)) {
		struct mm_list *prev_link = fe_queue_link->prev;
		struct mm_timeq_entry *prev_entry = containerof(prev_link, struct mm_timeq_entry, queue);
		if (prev_entry->value <= entry->value)
			break;
		fe_queue_link = prev_link;
	}

	mm_list_insert_prev(fe_queue_link, &entry->queue);
	entry->index = MM_TIMEQ_INDEX_FE;
	timeq->fe_num++;
}

static void
mm_timeq_insert_t1(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	int index = (entry->value - timeq->t1_start) / timeq->t1_size;
	mm_list_append(&timeq->t1[index], &entry->queue);
	entry->index = index;

	if (timeq->t1_used > index) {
		timeq->t1_used = index;
	}
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
}

void
mm_timeq_insert(struct mm_timeq *timeq, struct mm_timeq_entry *entry)
{
	ENTER();
	ASSERT(item->idx == MM_TIMEQ_INDEX_NO);

	if (timeq->t2_cur <= entry->value) {
		mm_timeq_insert_t2(timeq, entry);
	} else if (timeq->t1_cur <= entry->value) {
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
	ASSERT(entry->index != MM_TIMEQ_INDEX_NO);

	if (entry->index == MM_TIMEQ_INDEX_FE) {
		timeq->fe_num--;
	} else if (entry->index == MM_TIMEQ_INDEX_T2) {
		timeq->t2_num--;
	}

	mm_list_delete(&entry->queue);
	entry->index = MM_TIMEQ_INDEX_NO;

	LEAVE();
}

struct mm_timeq_entry *
mm_timeq_getmin(struct mm_timeq *timeq)
{
	ENTER();

	struct mm_timeq_entry *entry = NULL;

restart:
	if (!mm_list_empty(&timeq->fe)) {

		struct mm_list *link = mm_list_head(&timeq->fe);
		entry = containerof(link, struct mm_timeq_entry, queue);

	} else {
		while (timeq->t1_used < timeq->t1_size
		       && mm_list_empty(&timeq->t1[timeq->t1_used])) {
			timeq->t1_used++;
		}

		if (timeq->t1_used < timeq->t1_size) {

			/* The bucket is not empty */
			int used = timeq->t1_used++;

			/* If the bucket has exactly one item, return the item. */
			/* In other case, move all items in front end structure. */
			struct mm_list *link = mm_list_head(&timeq->t1[used]);
			if (!mm_list_has_next(&timeq->t1[used], link)) {
				entry = containerof(link, struct mm_timeq_entry, queue);
			} else {

				while (!mm_list_empty(&timeq->t1[used])) {
					struct mm_list *link = mm_list_head(&timeq->t1[used]);
					mm_list_delete(link);

					entry = containerof(link, struct mm_timeq_entry, queue);
					mm_timeq_insert_fe(timeq, entry);
				}

				entry = NULL;
				goto restart;
			}

		} else if (timeq->t2_num == 1) {

			/* All buckets empty and only one item in T2 */
			struct mm_list *link = mm_list_head(&timeq->t2);
			entry = containerof(link, struct mm_timeq_entry, queue);

		} else if (timeq->t2_num > 1) {

			/* All buckets empty, move all items in T1 */
			int sizeT1 = (timeq->t2_max - timeq->t2_min) / timeq->t2_num;
			if (sizeT1 < 64) {
				sizeT1 = 16;
			}

			if (timeq->t1_size < sizeT1) {
				sizeT1 *= 2;
				timeq->t1 = mm_realloc(timeq->t1, sizeT1 * sizeof(struct mm_list));
				for (int i = timeq->t1_size; i < sizeT1; ++i) {
					mm_list_init(&timeq->t1[i]);
				}
				timeq->t1_size = sizeT1;
			}

			timeq->t1_used = sizeT1;
			timeq->t1_start = timeq->t1_cur = timeq->t2_min;
			timeq->t2_min = timeq->t2_cur = timeq->t2_max;
			timeq->t2_num = 0;

			while (!mm_list_empty(&timeq->t2)) {
				struct mm_list *link = mm_list_head(&timeq->t2);
				mm_list_delete(link);

				entry = containerof(link, struct mm_timeq_entry, queue);
				mm_timeq_insert_t1(timeq, entry);
			}

			entry = NULL;
			goto restart;
		}
	}

	LEAVE();
	return entry;
}
