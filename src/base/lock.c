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

#include "base/format.h"
#include "base/hash.h"
#include "base/list.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/memory/alloc.h"
#include "base/thread/domain.h"
#include "base/thread/local.h"
#include "base/thread/thread.h"

#define MM_LOCK_STAT_TABLE_SIZE		509

// Lock statistics entry for domain threads.
struct mm_lock_domain_stat
{
	// The owner domain.
	struct mm_domain *domain;
	// Statistics for next domain.
	struct mm_slink link;
	// Per-thread lock statistics.
	MM_THREAD_LOCAL(struct mm_lock_stat, stat);
	// Ready for use flag.
	mm_atomic_uint8_t ready;
};

// Lock statistics entry for non-domain threads.
struct mm_lock_thread_stat
{
	// The owner thread.
	struct mm_thread *thread;
	// Statistics for next thread.
	struct mm_slink link;
	// Lock statistics itself.
	struct mm_lock_stat stat;
};

// Collection of statistics entries for a lock for all threads.
struct mm_lock_stat_set
{
	// Lock identification.
	const char *location;
	const char *moreinfo;

	// Link in the hash-table bucket.
	struct mm_slink bucket_link;
	// Link in the common list of locks.
	struct mm_slink common_link;

	// Domain statistics.
	struct mm_stack domain_list;
	mm_lock_t domain_lock;

	// Thread statistics.
	struct mm_stack thread_list;
};

static mm_lock_t mm_lock_stat_lock = MM_LOCK_INIT;
static struct mm_stack mm_lock_stat_table[MM_LOCK_STAT_TABLE_SIZE];
static struct mm_stack mm_lock_stat_list;

static bool
mm_lock_match_stat_set(struct mm_lock_stat_set *stat,
		       const char *location,
		       const char *moreinfo)
{
	ASSERT(stat->location != NULL && location != NULL);

	if (stat->location != location) {
		if (strcmp(stat->location, location) != 0)
			return false;
	}

	if (stat->moreinfo != moreinfo) {
		if (stat->moreinfo == NULL || moreinfo == NULL)
			return false;
		if (strcmp(stat->moreinfo, moreinfo) != 0)
			return false;
	}

	return true;
}

static struct mm_lock_stat_set *
mm_lock_find_stat_set(uint32_t bucket,
		      const char *location,
		      const char *moreinfo)
{
	// Go through bucket entries trying to find a match.
	struct mm_slink *link = mm_stack_atomic_load_head(&mm_lock_stat_table[bucket]);
	while (link != NULL) {
		struct mm_lock_stat_set *stat_set
			= containerof(link, struct mm_lock_stat_set, bucket_link);
		mm_memory_load_fence();
		// Match the current entry.
		if (mm_lock_match_stat_set(stat_set, location, moreinfo))
			return stat_set;
		link = mm_memory_load(link->next);
	}
	return NULL;
}

static struct mm_lock_stat_set *
mm_lock_get_stat_set(struct mm_lock_stat_info *info)
{
	ASSERT(info->location != NULL);

	// Find the pertinent hash table bucket.
	uint32_t hash = mm_hash_fnv(info->location, strlen(info->location));
	if (info->moreinfo != NULL)
		hash = mm_hash_fnv_with_seed(info->moreinfo, strlen(info->moreinfo), hash);
	uint32_t bucket = hash % MM_LOCK_STAT_TABLE_SIZE;

	// Try to find statistics optimistically (w/o acquiring a lock).
	struct mm_lock_stat_set *stat_set
		= mm_lock_find_stat_set(bucket, info->location, info->moreinfo);
	if (likely(stat_set != NULL))
		return stat_set;

	// Copy identification information.
	char *location = mm_memory_fixed_strdup(info->location);
	char *moreinfo = info->moreinfo == NULL ? NULL : mm_memory_fixed_strdup(info->moreinfo);

	// Allocate a new statistics collection entry.
	stat_set = mm_memory_fixed_xalloc(sizeof(struct mm_lock_stat_set));
	stat_set->location = location;
	stat_set->moreinfo = moreinfo; 

	// Initialize thread statistics.
	mm_stack_prepare(&stat_set->domain_list);
	mm_stack_prepare(&stat_set->thread_list);
	stat_set->domain_lock = (mm_lock_t) MM_LOCK_INIT;

	// Start critical section.
	mm_global_lock(&mm_lock_stat_lock);

	// Try to find it again in case it was added concurrently.
	struct mm_lock_stat_set *recheck_stat
		= mm_lock_find_stat_set(bucket, location, moreinfo);
	if (unlikely(recheck_stat != NULL)) {
		// Bail out if so.
		mm_global_unlock(&mm_lock_stat_lock);
		mm_memory_fixed_free(location);
		mm_memory_fixed_free(moreinfo);
		mm_memory_fixed_free(stat_set);
		return recheck_stat;
	}

	// Make the entry globally visible.
	stat_set->common_link.next = mm_lock_stat_list.head.next;
	stat_set->bucket_link.next = mm_lock_stat_table[bucket].head.next;
	mm_memory_store_fence();
	mm_lock_stat_list.head.next = &stat_set->common_link;
	mm_lock_stat_table[bucket].head.next = &stat_set->bucket_link;

	// End critical section.
	mm_global_unlock(&mm_lock_stat_lock);

	return stat_set;
}

struct mm_lock_domain_stat *
mm_lock_find_domain_stat(struct mm_lock_stat_set *stat_set,
			 struct mm_domain *domain)
{
	// Go through domain entries trying to find a match.
	struct mm_slink *link = mm_stack_atomic_load_head(&stat_set->domain_list);
	while (link != NULL) {
		struct mm_lock_domain_stat *dom_stat
			= containerof(link, struct mm_lock_domain_stat, link);
		if (dom_stat->domain == domain) {
			// If the entry is not yet ready then wait a bit until
			// it becomes ready.  It shouldn't take long.  Really.
			while (mm_memory_load(dom_stat->ready) == 0)
				mm_cpu_backoff();
			mm_memory_load_fence();
			return dom_stat;
		}
		link = mm_memory_load(link->next);
	}
	return NULL;
}

struct mm_lock_stat *
mm_lock_get_domain_stat(struct mm_lock_stat_set *stat_set,
			struct mm_thread *thread,
			struct mm_domain *domain)
{
	mm_thread_t dom_index = mm_thread_getnumber(thread);

	// Try to find domain entry optimistically (w/o acquiring a lock).
	struct mm_lock_domain_stat *dom_stat
		= mm_lock_find_domain_stat(stat_set, domain);
	if (likely(dom_stat != NULL))
		return MM_THREAD_LOCAL_DEREF(dom_index, dom_stat->stat);

	// Allocate a new statistics entry.
	dom_stat = mm_memory_fixed_xalloc(sizeof(struct mm_lock_domain_stat));
	dom_stat->domain = domain;

	// Mark it as not ready.
	dom_stat->ready = 0;

	// Start critical section.
	mm_global_lock(&stat_set->domain_lock);

	// Try to find it again in case it was added concurrently.
	struct mm_lock_domain_stat *recheck_stat
		= mm_lock_find_domain_stat(stat_set, domain);
	if (unlikely(recheck_stat != NULL)) {
		// Bail out if so.
		mm_global_unlock(&stat_set->domain_lock);
		mm_memory_fixed_free(dom_stat);
		return MM_THREAD_LOCAL_DEREF(dom_index, recheck_stat->stat);
	}

	mm_stack_insert(&stat_set->domain_list, &dom_stat->link);

	// End critical section.
	mm_global_unlock(&stat_set->domain_lock);

	// Initialize per-thread data.
	char *name;
	if (stat_set->moreinfo != NULL)
		name = mm_format(&mm_memory_xarena, "lock %s (%s)",
				 stat_set->location, stat_set->moreinfo);
	else
		name = mm_format(&mm_memory_xarena, "lock %s",
				 stat_set->location);

	MM_THREAD_LOCAL_ALLOC(domain, name, dom_stat->stat);
	for (mm_thread_t c = 0; c < domain->nthreads; c++) {
		struct mm_lock_stat *stat = MM_THREAD_LOCAL_DEREF(c, dom_stat->stat);
		stat->lock_count = 0;
		stat->fail_count = 0;
	}
	mm_memory_free(name);

	// Mark it as ready.
	mm_memory_store_fence();
	dom_stat->ready = 1;

	return MM_THREAD_LOCAL_DEREF(dom_index, dom_stat->stat);
}

struct mm_lock_stat *
mm_lock_get_thread_stat(struct mm_lock_stat_set *stat_set,
			struct mm_thread *thread)
{
	struct mm_lock_thread_stat *thr_stat;

	// Look for a matching thread entry.
	struct mm_slink *link = mm_stack_atomic_load_head(&stat_set->thread_list);
	while (link != NULL) {
		thr_stat = containerof(link, struct mm_lock_thread_stat, link);
		if (thr_stat->thread == thread)
			return &thr_stat->stat;
		link = mm_memory_load(link->next);
	}

	// If not found create a new entry.
	thr_stat = mm_memory_fixed_xalloc(sizeof(struct mm_lock_thread_stat));
	thr_stat->thread = thread;
	thr_stat->stat.lock_count = 0;
	thr_stat->stat.fail_count = 0;

	// Link the entry into list.
	struct mm_slink *head = mm_stack_atomic_load_head(&stat_set->thread_list);
	for (uint32_t b = 0; ; b = mm_thread_backoff(b)) {
		thr_stat->link.next = head;
		link = mm_stack_atomic_cas_head(&stat_set->thread_list, head, &thr_stat->link);
		if (link == head)
			break;
		head = link;
	}

	return &thr_stat->stat;
}

struct mm_lock_stat * NONNULL(1)
mm_lock_getstat(struct mm_lock_stat_info *info)
{
	// Get statistics collection pertinent to the lock in question.
	struct mm_lock_stat_set *stat_set = mm_memory_load(info->stat);
	if (stat_set == NULL)
		stat_set = mm_lock_get_stat_set(info);

	// Get a statistic entry specific to the calling thread.
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_domain *domain = mm_thread_getdomain(thread);
	if (domain != NULL)
		return mm_lock_get_domain_stat(stat_set, thread, domain);
	else
		return mm_lock_get_thread_stat(stat_set, thread);
}


static void
mm_lock_print_stat(const struct mm_thread *thread,
		   const struct mm_lock_stat_set *stat_set,
		   const struct mm_lock_stat *stat)
{
	const char *name = mm_thread_getname(thread);
	if (stat_set->moreinfo != NULL)
		mm_verbose("lock %s (%s), %s, locked %llu, failed %llu",
			   stat_set->location, stat_set->moreinfo, name,
			   (unsigned long long) stat->lock_count,
			   (unsigned long long) stat->fail_count);
	else
		mm_verbose("lock %s, %s, locked %llu, failed %llu",
			   stat_set->location, name,
			   (unsigned long long) stat->lock_count,
			   (unsigned long long) stat->fail_count);
}

#endif

void
mm_lock_stats(void)
{
#if ENABLE_LOCK_STATS
	struct mm_slink *set_link = mm_stack_atomic_load_head(&mm_lock_stat_list);
	while (set_link != NULL) {
		struct mm_lock_stat_set *stat_set
			= containerof(set_link, struct mm_lock_stat_set, common_link);
		mm_memory_load_fence();

		struct mm_slink *dom_link = mm_stack_atomic_load_head(&stat_set->domain_list);
		while (dom_link != NULL) {
			struct mm_lock_domain_stat *dom_stat
				= containerof(dom_link, struct mm_lock_domain_stat, link);
			struct mm_domain *domain = dom_stat->domain;
			for (mm_thread_t c = 0; c < domain->nthreads; c++) {
				struct mm_lock_stat *stat
					= MM_THREAD_LOCAL_DEREF(c, dom_stat->stat);
				struct mm_thread *thread = domain->threads[c];
				mm_lock_print_stat(thread, stat_set, stat);
			}
			dom_link = mm_memory_load(dom_link->next);
		}

		struct mm_slink *thr_link = mm_stack_atomic_load_head(&stat_set->thread_list);
		while (thr_link != NULL) {
			struct mm_lock_thread_stat *thr_stat
				= containerof(thr_link, struct mm_lock_thread_stat, link);
			struct mm_thread *thread = thr_stat->thread;
			mm_lock_print_stat(thread, stat_set, &thr_stat->stat);
			thr_link = mm_memory_load(thr_link->next);
		}

		set_link = mm_memory_load(set_link->next);
	}
#endif
}
