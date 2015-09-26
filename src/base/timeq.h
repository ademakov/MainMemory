/*
 * base/timeq.h - MainMemory time queue.
 *
 * Copyright (C) 2013-2015  Ivan Demakov, Aleksey Demakov
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

/* Declare opaque time queue type. */
struct mm_timeq;

/**********************************************************************
 * Time queue creation and destruction.
 **********************************************************************/

struct mm_timeq * mm_timeq_create(mm_arena_t arena);

void mm_timeq_destroy(struct mm_timeq *timeq);

void mm_timeq_set_min_bucket_width(struct mm_timeq *timeq, mm_timeval_t n);
void mm_timeq_set_max_bucket_width(struct mm_timeq *timeq, mm_timeval_t n);
void mm_timeq_set_min_bucket_count(struct mm_timeq *timeq, int n);
void mm_timeq_set_max_bucket_count(struct mm_timeq *timeq, int n);

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

static inline void
mm_timeq_entry_init(struct mm_timeq_entry *entry,
		    mm_timeval_t value,
		    mm_timeq_ident_t ident)
{
	entry->value = value;
	entry->index = MM_TIMEQ_INDEX_NO;
	entry->ident = ident;
}

void mm_timeq_insert(struct mm_timeq *timeq, struct mm_timeq_entry *entry);

void mm_timeq_delete(struct mm_timeq *timeq, struct mm_timeq_entry *entry);

struct mm_timeq_entry *mm_timeq_getmin(struct mm_timeq *timeq);

#endif /* BASE_TIMEQ_H */
