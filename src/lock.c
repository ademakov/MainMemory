/*
 * lock.c - MainMemory locks.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "lock.h"

#include "alloc.h"
#include "core.h"
#include "hash.h"
#include "util.h"

/**********************************************************************
 * Lock statistics.
 **********************************************************************/

#if ENABLE_LOCK_STATS

#define MM_LOCK_STAT_TABLE_SIZE		509

struct mm_task_lock_statistics
{
	struct mm_link bucket_link;
	struct mm_link common_link;

	MM_CDATA(struct mm_task_lock_core_stat, per_core_stat);

	const char *file;
	int line;
};

static mm_thread_lock_t mm_lock_stat_lock = MM_THREAD_LOCK_INIT;
static struct mm_link mm_lock_stat_table[MM_LOCK_STAT_TABLE_SIZE];
static struct mm_link mm_lock_stat_list;

static struct mm_task_lock_statistics *
mm_task_lock_findstat(uint32_t hash, const char *file, int line)
{
	struct mm_link *link = mm_link_shared_head(&mm_lock_stat_table[hash]);
	while (link != NULL) {
		struct mm_task_lock_statistics *stat =
			containerof(link, struct mm_task_lock_statistics, bucket_link);
		mm_memory_load_fence();

		if (stat->line == line && strcmp(stat->file, file) == 0)
			return stat;

		link = mm_memory_load(link->next);
	}
	return NULL;
}

static struct mm_task_lock_statistics *
mm_task_lock_getstat_slowpath(mm_task_lock_t *lock)
{
	const char *file = lock->file;
	int line = lock->line;
	ASSERT(file != NULL);

	uint32_t hash = mm_hash_fnv(file, strlen(file)) + (uint32_t) line;
	hash %= MM_LOCK_STAT_TABLE_SIZE;

	// Try to find optimistically (w/o acquiring a lock).
	struct mm_task_lock_statistics *stat = mm_task_lock_findstat(hash, file, line);
	if (stat != NULL)
		return stat;

	// Allocate a new statistics entry.
	stat = mm_global_alloc(sizeof(struct mm_task_lock_statistics));
	stat->file = file;
	stat->line = line;

	char *name = mm_asprintf(&mm_alloc_global, "lock %s:%d", file, line);
	mm_thread_lock(&mm_lock_stat_lock);

	// Try to find again in case it was added concurrently.
	struct mm_task_lock_statistics *recheck_stat =
		mm_task_lock_findstat(hash, file, line);
	if (unlikely(recheck_stat != NULL)) {
		mm_thread_unlock(&mm_lock_stat_lock);
		mm_global_free(stat);
		mm_global_free(name);
		return recheck_stat;
	}

	// Initialize per-core statistics.
	// TODO: Do this outside the critical section.
	MM_CDATA_ALLOC(name, stat->per_core_stat);
	for (mm_core_t c = 0; c < mm_core_getnum(); c++) {
		struct mm_task_lock_core_stat *core_stat =
			MM_CDATA_DEREF(c, stat->per_core_stat);
		core_stat->lock_count = 0;
		core_stat->fail_count = 0;
	}

	// Make the entry globally visible.
	stat->bucket_link.next = mm_lock_stat_table[hash].next;
	stat->common_link.next = mm_lock_stat_list.next;
	mm_memory_store_fence();
	mm_lock_stat_table[hash].next = &stat->bucket_link;
	mm_lock_stat_list.next = &stat->common_link;

	mm_thread_unlock(&mm_lock_stat_lock);
	mm_global_free(name);

	return stat;
}

struct mm_task_lock_core_stat *
mm_task_lock_getstat(mm_task_lock_t *lock)
{
	struct mm_task_lock_statistics *stat = mm_memory_load(lock->stat);
	if (stat == NULL)
		stat = mm_task_lock_getstat_slowpath(lock);
	return MM_CDATA_DEREF(mm_core_self(), stat->per_core_stat);
}

#endif
