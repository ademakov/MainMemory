/*
 * base/lock.c - MainMemory locks.
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

#include "base/lock.h"

/**********************************************************************
 * Lock statistics.
 **********************************************************************/

#if ENABLE_LOCK_STATS

#include "base/hash.h"
#include "base/list.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/alloc.h"
#include "base/mem/cdata.h"
#include "base/thr/thread.h"

#include "base/util/format.h"

#include "core/core.h"

#define MM_LOCK_STAT_TABLE_SIZE		509

// Lock statistics entry for non-core threads.
struct mm_lock_stat_for_thread
{
	// The owner thread.
	struct mm_thread *thread;
	// Statistics collection thread list.
	struct mm_link thread_link;
	// Lock statistics entry itself.
	struct mm_lock_stat stat;
};

// Collection of statistics entries for a lock for all threads.
struct mm_lock_stat_set
{
	// Lock identification.
	const char *location;
	const char *moreinfo;

	// Link in the hash-table bucket.
	struct mm_link bucket_link;
	// Link in the common list of locks.
	struct mm_link common_link;

	// Per-core information.
	MM_CDATA(struct mm_lock_stat, percore_stat);

	// Per-thread information.
	struct mm_link thread_list;

	// Ready for use flag.
	mm_atomic_uint8_t ready;
};

static mm_lock_t mm_lock_stat_lock = MM_LOCK_INIT;
static struct mm_link mm_lock_stat_table[MM_LOCK_STAT_TABLE_SIZE];
static struct mm_link mm_lock_stat_list;

static bool
mm_lock_stat_match(struct mm_lock_stat_set *stat,
		   const char *location, const char *moreinfo)
{
	ASSERT(stat->location != NULL && location != NULL);

	if (stat->location != location) {
		if (strcmp(stat->location, location) != 0)
			return false;
	}

	if (stat->moreinfo != moreinfo) {
		if (stat->moreinfo == NULL || location == NULL)
			return false;
		if (strcmp(stat->moreinfo, moreinfo) != 0)
			return false;
	}

	return true;
}

static struct mm_lock_stat_set *
mm_lock_stat_find(uint32_t bucket, const char *location, const char *moreinfo)
{
	// Go through bucket entries trying to find a match.
	struct mm_link *link = mm_link_shared_head(&mm_lock_stat_table[bucket]);
	while (link != NULL) {
		struct mm_lock_stat_set *stat
			= containerof(link, struct mm_lock_stat_set, bucket_link);
		mm_memory_load_fence();

		// Match the entry.
		if (mm_lock_stat_match(stat, location, moreinfo)) {
			// If the entry is not yet ready then wait a bit until
			// it becomes ready.  It shouldn't take long.  Really.
			uint32_t count = 0;
			while (mm_memory_load(stat->ready) == 0)
				count = mm_backoff(count);
			mm_memory_load_fence();
			return stat;
		}

		link = mm_memory_load(link->next);
	}
	return NULL;
}

static struct mm_lock_stat_set *
mm_lock_stat_get_slowpath(struct mm_lock_stat_info *info)
{
	ASSERT(info->location != NULL);

	uint32_t hash = mm_hash_fnv(info->location, strlen(info->location));
	if (info->moreinfo != NULL)
		hash = mm_hash_fnv_with_seed(info->moreinfo, strlen(info->moreinfo), hash);
	uint32_t bucket = hash % MM_LOCK_STAT_TABLE_SIZE;

	// Try to find statistics optimistically (w/o acquiring a lock).
	struct mm_lock_stat_set *stat
		= mm_lock_stat_find(bucket, info->location, info->moreinfo);
	if (stat != NULL)
		return stat;

	// Copy identification information.
	char *location = mm_global_strdup(info->location);
	char *moreinfo = info->moreinfo == NULL ? NULL : mm_global_strdup(info->moreinfo);

	// Allocate a new statistics entry.
	stat = mm_global_alloc(sizeof(struct mm_lock_stat_set));
	stat->location = location;
	stat->moreinfo = moreinfo; 

	// Mark it as not ready.
	stat->ready = 0;

	// Start critical section.
	mm_global_lock(&mm_lock_stat_lock);

	// Try to find again in case it was added concurrently.
	struct mm_lock_stat_set *recheck_stat
		= mm_lock_stat_find(bucket, location, moreinfo);
	if (unlikely(recheck_stat != NULL)) {
		// Bail out if so.
		mm_global_unlock(&mm_lock_stat_lock);

		mm_global_free(location);
		mm_global_free(moreinfo);
		mm_global_free(stat);

		return recheck_stat;
	}

	// Make the entry globally visible.
	stat->bucket_link.next = mm_lock_stat_table[bucket].next;
	stat->common_link.next = mm_lock_stat_list.next;
	mm_memory_store_fence();
	mm_lock_stat_table[bucket].next = &stat->bucket_link;
	mm_lock_stat_list.next = &stat->common_link;

	// End critical section.
	mm_global_unlock(&mm_lock_stat_lock);

	// Initialize per-core statistics.
	char *name;
	if (moreinfo != NULL)
		name = mm_format(&mm_global_arena, "lock %s (%s)",
				   location, moreinfo);
	else
		name = mm_format(&mm_global_arena, "lock %s",
				   location);
	MM_CDATA_ALLOC(name, stat->percore_stat);
	for (mm_core_t c = 0; c < mm_core_getnum(); c++) {
		struct mm_lock_stat *core_stat =
			MM_CDATA_DEREF(c, stat->percore_stat);
		core_stat->lock_count = 0;
		core_stat->fail_count = 0;
	}
	mm_global_free(name);

	// Initialize non-core thread statistics.
	mm_link_init(&stat->thread_list);

	// Mark it as ready.
	mm_memory_store_fence();
	stat->ready = 1;

	return stat;
}

struct mm_lock_stat *
mm_lock_getstat(struct mm_lock_stat_info *info)
{
	// Find pertinent statistics collection.
	struct mm_lock_stat_set *stat = mm_memory_load(info->stat);
	if (stat == NULL)
		stat = mm_lock_stat_get_slowpath(info);

	// If running on a core thread then use per-core entry.
	mm_core_t core = mm_core_selfid();
	if (core != MM_CORE_NONE)
		return MM_CDATA_DEREF(core, stat->percore_stat);

	// If running on a non-core thread try to find pertinent entry.
	struct mm_lock_stat_for_thread *thr_stat;
	struct mm_thread *thread = mm_thread_self();
	struct mm_link *link = mm_link_shared_head(&stat->thread_list);
	while (link != NULL) {
		thr_stat = containerof(link, struct mm_lock_stat_for_thread, thread_link);
		if (thr_stat->thread == thread)
			return &thr_stat->stat;
		link = mm_memory_load(link->next);
	}

	// If not found create a new entry.
	thr_stat = mm_global_alloc(sizeof(struct mm_lock_stat_for_thread));
	thr_stat->thread = thread;
	thr_stat->stat.lock_count = 0;
	thr_stat->stat.fail_count = 0;

	// Link the entry into list.
	struct mm_link *head = mm_link_shared_head(&stat->thread_list);
	for (uint32_t b = 0; ; b = mm_backoff(b)) {
		thr_stat->thread_link.next = head;
		link = mm_link_cas_head(&stat->thread_list, head, &thr_stat->thread_link);
		if (link == head)
			break;
		head = link;
	}

	return &thr_stat->stat;
}


static void
mm_lock_stat_print(const char *owner,
		   struct mm_lock_stat_set *stat_set,
		   struct mm_lock_stat *stat)
{
	if (stat_set->moreinfo != NULL)
		mm_verbose("lock %s (%s), %s, locked %llu, failed %llu",
			stat_set->location, stat_set->moreinfo, owner,
			stat->lock_count, stat->fail_count);
	else
		mm_verbose("lock %s, %s, locked %llu, failed %llu",
			stat_set->location, owner,
			stat->lock_count, stat->fail_count);
}

#endif

void
mm_lock_stats(void)
{
#if ENABLE_LOCK_STATS
	struct mm_link *link = mm_link_shared_head(&mm_lock_stat_list);
	while (link != NULL) {
		struct mm_lock_stat_set *stat_set
			= containerof(link, struct mm_lock_stat_set, common_link);
		mm_memory_load_fence();

		for (mm_core_t c = 0; c < mm_core_getnum(); c++) {
			struct mm_lock_stat *stat
				= MM_CDATA_DEREF(c, stat_set->percore_stat);
			struct mm_core *core = mm_core_getptr(c);
			const char *owner = mm_thread_getname(core->thread);
			mm_lock_stat_print(owner, stat_set, stat);
		}

		struct mm_link *thr_link = mm_link_shared_head(&stat_set->thread_list);
		while (thr_link != NULL) {
			struct mm_lock_stat_for_thread *thr_stat
				= containerof(thr_link, struct mm_lock_stat_for_thread, thread_link);
			const char *owner = mm_thread_getname(thr_stat->thread);
			mm_lock_stat_print(owner, stat_set, &thr_stat->stat);
			thr_link = mm_memory_load(thr_link->next);
		}

		link = mm_memory_load(link->next);
	}
#endif
}
