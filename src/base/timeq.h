/*
 * base/timeq.h - MainMemory time queue.
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

#ifndef BASE_TIMEQ_H
#define BASE_TIMEQ_H

#include "common.h"
#include "base/list.h"
#include "base/memory/arena.h"

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

    mm_arena_t arena;
};

/**********************************************************************
 * Time queue creation and destruction.
 **********************************************************************/

struct mm_timeq * NONNULL(1)
mm_timeq_create(mm_arena_t arena);

void NONNULL(1)
mm_timeq_destroy(struct mm_timeq *timeq);

void NONNULL(1, 2)
mm_timeq_prepare(struct mm_timeq *timeq, mm_arena_t arena);

void NONNULL(1)
mm_timeq_cleanup(struct mm_timeq *timeq);

void NONNULL(1)
mm_timeq_set_min_bucket_width(struct mm_timeq *timeq, mm_timeval_t n);
void NONNULL(1)
mm_timeq_set_max_bucket_width(struct mm_timeq *timeq, mm_timeval_t n);
void NONNULL(1)
mm_timeq_set_min_bucket_count(struct mm_timeq *timeq, int n);
void NONNULL(1)
mm_timeq_set_max_bucket_count(struct mm_timeq *timeq, int n);

/**********************************************************************
 * Time queue entry routines.
 **********************************************************************/

#define MM_TIMEQ_INDEX_NO ((mm_timeq_index_t) -1)
#define MM_TIMEQ_INDEX_T2 ((mm_timeq_index_t) -2)
#define MM_TIMEQ_INDEX_FE ((mm_timeq_index_t) -3)

typedef int32_t mm_timeq_index_t;
typedef int32_t mm_timeq_ident_t;

struct mm_timeq_entry
{
	struct mm_link queue;
	mm_timeval_t value;
	mm_timeq_index_t index;
	mm_timeq_ident_t ident;
};

static inline void NONNULL(1)
mm_timeq_entry_prepare(struct mm_timeq_entry *entry, mm_timeq_ident_t ident)
{
	entry->value = MM_TIMEVAL_MAX;
	entry->index = MM_TIMEQ_INDEX_NO;
	entry->ident = ident;
}

static inline void NONNULL(1)
mm_timeq_entry_settime(struct mm_timeq_entry *entry, mm_timeval_t value)
{
	entry->value = value;
}

static inline bool NONNULL(1)
mm_timeq_entry_queued(struct mm_timeq_entry *entry)
{
	return (entry->index != MM_TIMEQ_INDEX_NO);
}

void NONNULL(1)
mm_timeq_insert(struct mm_timeq *timeq, struct mm_timeq_entry *entry);

void NONNULL(1)
mm_timeq_delete(struct mm_timeq *timeq, struct mm_timeq_entry *entry);

struct mm_timeq_entry * NONNULL(1)
mm_timeq_getmin(struct mm_timeq *timeq);

#endif /* BASE_TIMEQ_H */
