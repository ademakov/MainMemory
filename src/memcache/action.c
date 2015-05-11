/*
 * memcache/action.c - MainMemory memcache table actions.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "memcache/action.h"
#include "memcache/entry.h"

#include "base/bitops.h"
#include "base/log/trace.h"

#define MC_TABLE_STRIDE		64

/**********************************************************************
 * Helper Routines.
 **********************************************************************/

static bool
mc_action_is_expired_entry(struct mc_tpart *part, struct mc_entry *entry, mm_timeval_t time)
{
	if (entry->exp_time && entry->exp_time <= time)
		return true;
	if (entry->stamp < part->flush_stamp)
		return true;
	return false;
}

static bool
mc_action_is_eviction_victim(struct mc_tpart *part, struct mc_entry *entry, mm_timeval_t time)
{
	if (entry->state == MC_ENTRY_USED_MIN)
		return true;
	return mc_action_is_expired_entry(part, entry, time);
}

static void
mc_action_ref_entry(struct mc_entry *entry)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	uint16_t test = mm_atomic_uint16_inc_and_test(&entry->ref_count);
#else
	uint16_t test = ++(entry->ref_count);
#endif
	// Integer overflow check.
	if (unlikely(test == 0))
		ABORT();
}

static bool
mc_action_unref_entry(struct mc_entry *entry)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	uint16_t test = mm_atomic_uint16_dec_and_test(&entry->ref_count);
#else
	uint16_t test = --(entry->ref_count);
#endif
	return (test == 0);
}

static void
mc_action_access_entry(struct mc_entry *entry)
{
	uint8_t state = entry->state;
	if (state >= MC_ENTRY_USED_MIN && state < MC_ENTRY_USED_MAX)
		mm_memory_store(entry->state, state + 1);
}

static void
mc_action_unlink_entry(struct mc_tpart *part, struct mm_link *pred, struct mc_entry *entry)
{
	ASSERT(entry->state >= MC_ENTRY_USED_MIN);
	ASSERT(entry->state <= MC_ENTRY_USED_MAX);
	mm_link_cleave(pred, entry->link.next);
	entry->state = MC_ENTRY_NOT_USED;
	part->volume -= mc_entry_size(entry);
}

static void
mc_action_remove_entry(struct mc_tpart *part, struct mm_link *pred, struct mc_entry *entry)
{
	while (likely(pred != NULL)) {
		if (pred->next == &entry->link) {
			mc_action_unlink_entry(part, pred, entry);
			return;
		}
		pred = pred->next;
	}
	ABORT();
}

static void
mc_action_free_entry(struct mc_tpart *part, struct mc_entry *entry)
{
	ASSERT(entry->state == MC_ENTRY_NOT_USED);
	entry->state = MC_ENTRY_FREE;
	mm_link_insert(&part->free_list, &entry->link);
	part->nentries_free++;
}

static void
mc_action_free_entries(struct mc_tpart *part, struct mm_link *victims)
{
	while (!mm_link_empty(victims)) {
		struct mm_link *link = mm_link_delete_head(victims);
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_unref_entry(entry)) {
			mc_entry_free_chunks(entry);
			mc_action_free_entry(part, entry);
		}
	}
}

static void
mc_action_drop_expired(struct mc_tpart *part,
		       struct mm_link *bucket,
		       struct mm_link *expired)
{
	mm_timeval_t time = mm_core->time_manager.time;
	mm_link_init(expired);

	struct mm_link *pred = bucket;
	while (!mm_link_is_last(pred)) {
		struct mm_link *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_is_expired_entry(part, entry, time)) {
			mc_action_unlink_entry(part, pred, entry);
			mm_link_insert(expired, &entry->link);
		} else {
			pred = link;
		}
	}
}

static bool
mc_action_find_victims(struct mc_tpart *part,
		       struct mm_link *victims,
		       uint32_t nrequired)
{
	mm_timeval_t time = mm_core_self()->time_manager.time;
	uint32_t nvictims = 0;
	mm_link_init(victims);

	bool end = false;
	while (nvictims < nrequired) {
		struct mc_entry *hand = part->clock_hand;
		if (unlikely(hand == part->entries_end)) {
			// Prevent infinite loop.
			if (end)
				break;
			else
				end = true;
			hand = part->entries;
		}

		uint8_t state = hand->state;
		if (state >= MC_ENTRY_USED_MIN && state <= MC_ENTRY_USED_MAX) {
			if (mc_action_is_eviction_victim(part, hand, time)) {
				uint32_t index = mc_table_index(part, hand->hash);
				struct mm_link *bucket = &part->buckets[index];
				mc_action_remove_entry(part, bucket, hand);
				mm_link_insert(victims, &hand->link);
				++nvictims;
			} else {
				hand->state--;
			}
		}

		part->clock_hand = hand + 1;
	}

	return (nvictims > 0 && nvictims == nrequired);
}

static bool
mc_action_match_entry(struct mc_action *action, struct mc_entry *entry)
{
	if (action->hash != entry->hash)
		return false;
	if (action->key_len != entry->key_len)
		return false;
	return !memcmp(action->key, mc_entry_getkey(entry), action->key_len);
}

static void
mc_action_bucket_insert(struct mc_action *action,
			struct mm_link *bucket,
			uint8_t state)
{
	ASSERT(action->new_entry->state == MC_ENTRY_NOT_USED);
	ASSERT(state != MC_ENTRY_NOT_USED || state != MC_ENTRY_FREE);
	action->new_entry->state = state;
	action->new_entry->stamp = action->part->stamp;
	mm_link_insert(bucket, &action->new_entry->link);
	action->part->stamp += mc_table.nparts;
	action->part->volume += mc_entry_size(action->new_entry);
}

static void
mc_action_bucket_lookup(struct mc_action *action,
			struct mm_link *bucket,
			struct mm_link *freelist)
{
	mc_action_drop_expired(action->part, bucket, freelist);
	struct mm_link *link = mm_link_head(bucket);
	while (link != NULL) {
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_match_entry(action, entry)) {
			ASSERT(entry->state >= MC_ENTRY_USED_MIN);
			ASSERT(entry->state <= MC_ENTRY_USED_MAX);
			action->old_entry = entry;
			return;
		}
		link = link->next;
	}
	action->old_entry = NULL;
}

static void
mc_action_bucket_delete(struct mc_action *action,
			struct mm_link *bucket,
			struct mm_link *freelist)
{
	mc_action_drop_expired(action->part, bucket, freelist);
	struct mm_link *pred = bucket;
	while (!mm_link_is_last(pred)) {
		struct mm_link *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_match_entry(action, entry)) {
			mc_action_unlink_entry(action->part, pred, entry);
			mm_link_insert(freelist, &entry->link);
			action->old_entry = entry;
			return;
		}
		pred = link;
	}
	action->old_entry = NULL;
}

static void
mc_action_bucket_update(struct mc_action *action,
			struct mm_link *bucket,
			struct mm_link *freelist,
			bool match_stamp)
{
	mc_action_drop_expired(action->part, bucket, freelist);
	struct mm_link *pred = bucket;
	while (!mm_link_is_last(pred)) {
		struct mm_link *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_match_entry(action, entry)) {
			action->entry_match = !match_stamp || action->stamp == entry->stamp;
			action->old_entry = entry;
			if (action->entry_match) {
				uint8_t state = entry->state;
				mc_action_unlink_entry(action->part, pred, entry);
				mm_link_insert(freelist, &entry->link);
				mc_action_bucket_insert(action, bucket, state);
			}
			return;
		}
		pred = link;
	}
	action->entry_match = false;
	action->old_entry = NULL;
}

/**********************************************************************
 * Table Actions.
 **********************************************************************/

void
mc_action_lookup_low(struct mc_action *action)
{
	ENTER();

	struct mm_link freelist;
	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	struct mm_link *bucket = &action->part->buckets[index];

	mc_action_bucket_lookup(action, bucket, &freelist);
	if (action->old_entry != NULL) {
		mc_action_ref_entry(action->old_entry);
		mc_action_access_entry(action->old_entry);
	}

	mc_table_lookup_unlock(action->part);
	if (!mm_link_empty(&freelist)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &freelist);
		mc_table_freelist_unlock(action->part);
	}

	LEAVE();
}

void
mc_action_finish_low(struct mc_action *action)
{
	ENTER();

	struct mc_entry *entry = action->old_entry;
	if (mc_action_unref_entry(entry)) {
		mc_entry_free_chunks(entry);

		mc_table_freelist_lock(action->part);
		mc_action_free_entry(action->part, action->old_entry);
		mc_table_freelist_unlock(action->part);
	}

	LEAVE();
}

void
mc_action_delete_low(struct mc_action *action)
{
	ENTER();

	struct mm_link finish_list;
	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	struct mm_link *bucket = &action->part->buckets[index];

	mc_action_bucket_delete(action, bucket, &finish_list);

	mc_table_lookup_unlock(action->part);
	if (!mm_link_empty(&finish_list)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &finish_list);
		mc_table_freelist_unlock(action->part);
	}

	LEAVE();
}

void
mc_action_create_low(struct mc_action *action)
{
	ENTER();

	mc_table_freelist_lock(action->part);

	for (;;) {
		if (!mm_link_empty(&action->part->free_list)) {
			struct mm_link *link = mm_link_delete_head(&action->part->free_list);
			action->new_entry = containerof(link, struct mc_entry, link);
			ASSERT(action->part->nentries_free);
			action->part->nentries_free--;
			break;
		}

		if (action->part->nentries_void) {
			action->new_entry = action->part->entries_end++;
			action->part->nentries_void--;
			break;
		}
		if (mc_table_expand(action->part, mc_table.nentries_increment)) {
			ASSERT(action->part->nentries_void);
			action->new_entry = action->part->entries_end++;
			action->part->nentries_void--;
			break;
		}

		mc_table_freelist_unlock(action->part);

		struct mm_link victims;
		mc_table_lookup_lock(action->part);
		mc_action_find_victims(action->part, &victims, 1);
		mc_table_lookup_unlock(action->part);

		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &victims);
	}

	ASSERT(action->new_entry->state == MC_ENTRY_FREE);
	action->new_entry->state = MC_ENTRY_NOT_USED;
	action->new_entry->ref_count = 1;

	mc_table_freelist_unlock(action->part);
	mc_table_reserve_entries(action->part);

	action->new_entry->hash = action->hash;
	mm_link_init(&action->new_entry->chunks);

	LEAVE();
}

void
mc_action_cancel_low(struct mc_action *action)
{
	ENTER();

	mc_entry_free_chunks(action->new_entry);

	mc_table_freelist_lock(action->part);
	mc_action_free_entry(action->part, action->new_entry);
	mc_table_freelist_unlock(action->part);

	LEAVE();
}

void
mc_action_insert_low(struct mc_action *action)
{
	ENTER();

	struct mm_link freelist;
	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	struct mm_link *bucket = &action->part->buckets[index];

	mc_action_bucket_lookup(action, bucket, &freelist);
	if (action->old_entry == NULL) {
		mc_action_bucket_insert(action, bucket, MC_ENTRY_USED_MIN);
	}

	mc_table_lookup_unlock(action->part);
	if (!mm_link_empty(&freelist)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &freelist);
		mc_table_freelist_unlock(action->part);
	}

	if (action->old_entry == NULL) {
		mc_table_reserve_volume(action->part);
	} else {
		mc_action_cancel_low(action);
	}

	LEAVE();
}

void
mc_action_update_low(struct mc_action *action)
{
	ENTER();

	if (action->old_entry != NULL)
		mc_action_finish_low(action);

	struct mm_link freelist;
	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	struct mm_link *bucket = &action->part->buckets[index];

	mc_action_bucket_update(action, bucket, &freelist, action->match_stamp);
	if (action->entry_match) {
		if (action->use_new_entry)
			mc_action_ref_entry(action->new_entry);
		mc_action_access_entry(action->new_entry);
	} else {
		if (action->use_old_entry && action->old_entry != NULL)
			mc_action_ref_entry(action->old_entry);
	}

	mc_table_lookup_unlock(action->part);
	if (!mm_link_empty(&freelist)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &freelist);
		mc_table_freelist_unlock(action->part);
	}

	if (action->entry_match) {
		mc_table_reserve_volume(action->part);
	} else if (!action->use_old_entry || action->old_entry == NULL) {
		mc_action_cancel_low(action);
	}

	LEAVE();
}

void
mc_action_upsert_low(struct mc_action *action)
{
	ENTER();

	struct mm_link freelist;
	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	struct mm_link *bucket = &action->part->buckets[index];

	mc_action_bucket_delete(action, bucket, &freelist);
	mc_action_bucket_insert(action, bucket, MC_ENTRY_USED_MIN);

	mc_table_lookup_unlock(action->part);
	if (!mm_link_empty(&freelist)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &freelist);
		mc_table_freelist_unlock(action->part);
	}

	mc_table_reserve_volume(action->part);

	LEAVE();
}

void
mc_action_stride_low(struct mc_action *action)
{
	ENTER();

	mc_table_lookup_lock(action->part);

	uint32_t used = action->part->nbuckets;

	uint32_t half_size;
	if (unlikely(mm_is_pow2z(used))) {
		half_size = used;
		mc_table_buckets_resize(action->part, used, used * 2);
	} else {
		half_size = 1 << (31 - mm_clz(used));
	}

	uint32_t target = used;
	uint32_t source = used - half_size;
	uint32_t mask = half_size + half_size - 1;

	for (uint32_t count = 0; count < MC_TABLE_STRIDE; count++) {
		struct mm_link s_entries, t_entries;
		mm_link_init(&s_entries);
		mm_link_init(&t_entries);

		struct mm_link *link = mm_link_head(&action->part->buckets[source]);
		while (link != NULL) {
			struct mm_link *next = link->next;

			struct mc_entry *entry = containerof(link, struct mc_entry, link);
			uint32_t index = (entry->hash >> mc_table.part_bits) & mask;
			if (index == source) {
				mm_link_insert(&s_entries, link);
			} else {
				ASSERT(index == target);
				mm_link_insert(&t_entries, link);
			}

			link = next;
		}

		action->part->buckets[source++] = s_entries;
		action->part->buckets[target++] = t_entries;
	}

	used += MC_TABLE_STRIDE;
	action->part->nbuckets = used;

	mc_table_lookup_unlock(action->part);

	LEAVE();
}

void
mc_action_evict_low(struct mc_action *action)
{
	ENTER();

	struct mm_link victims;
	mc_table_lookup_lock(action->part);
	bool found = mc_action_find_victims(action->part, &victims, 32);
	mc_table_lookup_unlock(action->part);

	if (found) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &victims);
		mc_table_freelist_unlock(action->part);
	}

	LEAVE();
}

void
mc_action_flush_low(struct mc_action *action)
{
	ENTER();

	mc_table_lookup_lock(action->part);
	action->part->flush_stamp = action->part->stamp;
	mc_table_lookup_unlock(action->part);

	LEAVE();
}

#if ENABLE_MEMCACHE_COMBINER

void
mc_action_perform(uintptr_t data)
{
	struct mc_action *action = (struct mc_action *) data;
	switch (action->action) {
	case MC_ACTION_LOOKUP:
		mc_action_lookup_low(action);
		break;
	case MC_ACTION_FINISH:
		mc_action_finish_low(action);
		break;
	case MC_ACTION_DELETE:
		mc_action_delete_low(action);
		break;
	case MC_ACTION_CREATE:
		mc_action_create_low(action);
		break;
	case MC_ACTION_CANCEL:
		mc_action_cancel_low(action);
		break;
	case MC_ACTION_INSERT:
		mc_action_insert_low(action);
		break;
	case MC_ACTION_UPDATE:
		mc_action_update_low(action);
		break;
	case MC_ACTION_UPSERT:
		mc_action_upsert_low(action);
		break;
	case MC_ACTION_STRIDE:
		mc_action_stride_low(action);
		break;
	case MC_ACTION_EVICT:
		mc_action_evict_low(action);
		break;
	case MC_ACTION_FLUSH:
		mc_action_flush_low(action);
		break;
	default:
		ABORT();
	}

	mm_memory_store_fence();
	action->action = MC_ACTION_DONE;
}

#endif
