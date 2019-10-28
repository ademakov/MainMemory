/*
 * memcache/action.c - MainMemory memcache table actions.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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
#include "base/report.h"

#define MC_TABLE_STRIDE		64

/**********************************************************************
 * Entry expiration timer.
 **********************************************************************/

#if 0

static uint32_t mc_action_exp_time;
static mm_timer_t mc_action_exp_timer;

static uint32_t
mc_action_get_exp_time(void)
{
	return mm_memory_load(mc_action_exp_time);
}

static mm_value_t
mc_action_exp_time_handler(mm_value_t value UNUSED)
{
	ENTER();

	mm_timeval_t real_time = mm_event_getrealtime(mm_context_selfptr());
	mc_action_exp_time = real_time / 1000000; // useconds -> seconds.

	LEAVE();
	return 0;
}

static void
mc_action_exp_time_start(void)
{
	mc_action_exp_timer = mm_timer_create(MM_CLOCK_REALTIME,
					      mc_action_exp_time_handler,
					      0);
	mm_timer_settime(mc_action_exp_timer, false, 0, 1000000);
}

static void
mc_action_exp_time_stop(void)
{
	mm_timer_destroy(mc_action_exp_timer);
}

#else

static uint32_t
mc_action_get_exp_time(void)
{
	mm_timeval_t real_time = mm_event_getrealtime(mm_context_selfptr());
	return real_time / 1000000; // useconds -> seconds.
}

static void
mc_action_exp_time_start(void)
{
}

static void
mc_action_exp_time_stop(void)
{
}

#endif

/**********************************************************************
 * Helper routines.
 **********************************************************************/

static bool
mc_action_is_expired_entry(struct mc_tpart *part, struct mc_entry *entry, uint32_t time)
{
	if (entry->exp_time && entry->exp_time <= time) {
		TRACE("expired entry");
		return true;
	}
	if (entry->stamp < part->flush_stamp) {
		TRACE("flushed entry");
		return true;
	}
	return false;
}

static bool
mc_action_is_eviction_victim(struct mc_tpart *part, struct mc_entry *entry, uint32_t time)
{
	if (entry->state == MC_ENTRY_USED_MIN) {
		TRACE("rarely used entry");
		return true;
	}
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
mc_action_unlink_entry(struct mc_tpart *part, struct mm_slink *pred, struct mc_entry *entry)
{
	ASSERT(entry->state >= MC_ENTRY_USED_MIN);
	ASSERT(entry->state <= MC_ENTRY_USED_MAX);
	ASSERT(pred->next == &entry->link);
	mm_stack_remove_next(pred);
	entry->state = MC_ENTRY_NOT_USED;
	part->volume -= mc_entry_size(entry);
}

static void
mc_action_remove_entry(struct mc_tpart *part, struct mm_slink *pred, struct mc_entry *entry)
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
	mm_stack_insert(&part->free_list, &entry->link);
	part->nentries_free++;
}

static void
mc_action_alloc_chunks(struct mc_tpart *part, struct mc_entry *entry)
{
	size_t size = entry->key_len + entry->value_len;
	entry->data = mm_private_space_alloc(&part->data_space, size);
	if (unlikely(entry->data == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
}

static void
mc_action_free_chunks(struct mc_tpart *part, struct mc_entry *entry)
{
	if (likely(entry->data != NULL)) {
		mm_private_space_free(&part->data_space, entry->data);
		entry->data = NULL;
	}
}

static void
mc_action_free_entries(struct mc_tpart *part, struct mm_stack *victims)
{
	while (!mm_stack_empty(victims)) {
		struct mm_slink *link = mm_stack_remove(victims);
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_unref_entry(entry)) {
			mc_action_free_chunks(part, entry);
			mc_action_free_entry(part, entry);
		}
	}
}

static bool
mc_action_find_victims(struct mc_tpart *part,
		       struct mm_stack *victims,
		       uint32_t nrequired)
{
	uint32_t nvictims = 0;
	mm_stack_prepare(victims);

	mm_timeval_t real_time = mm_event_getrealtime(mm_context_selfptr());
	uint32_t time = real_time / 1000000; // useconds -> seconds.

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
				struct mm_stack *bucket = &part->buckets[index];
				mc_action_remove_entry(part, &bucket->head, hand);
				mm_stack_insert(victims, &hand->link);
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
mc_action_bucket_insert(struct mc_action_storage *action,
			struct mm_stack *bucket,
			uint8_t state)
{
	ASSERT(action->new_entry->state == MC_ENTRY_NOT_USED);
	ASSERT(state != MC_ENTRY_NOT_USED || state != MC_ENTRY_FREE);
	action->new_entry->state = state;
	action->new_entry->stamp = action->base.part->stamp;
	mm_stack_insert(bucket, &action->new_entry->link);
	action->base.part->stamp += mc_table.nparts;
	action->base.part->volume += mc_entry_size(action->new_entry);

	// Store stamp value needed for binary protocol response.
	action->stamp = action->new_entry->stamp;
}

static void
mc_action_bucket_lookup(struct mc_action *action,
			struct mm_stack *bucket,
			struct mm_stack *freelist)
{
	struct mc_tpart *part = action->part;
	struct mm_slink *pred = &bucket->head;
	uint32_t time = mc_action_get_exp_time();

	while (!mm_stack_is_tail(pred)) {
		struct mm_slink *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_is_expired_entry(part, entry, time)) {
			mc_action_unlink_entry(part, pred, entry);
			mm_stack_insert(freelist, &entry->link);
		} else {
			if (mc_action_match_entry(action, entry)) {
				ASSERT(entry->state >= MC_ENTRY_USED_MIN);
				ASSERT(entry->state <= MC_ENTRY_USED_MAX);
				action->old_entry = entry;
				return;
			}
			pred = link;
		}
	}

	action->old_entry = NULL;
}

static void
mc_action_bucket_delete(struct mc_action *action,
			struct mm_stack *bucket,
			struct mm_stack *freelist)
{
	struct mc_tpart *part = action->part;
	struct mm_slink *pred = &bucket->head;
	uint32_t time = mc_action_get_exp_time();

	while (!mm_stack_is_tail(pred)) {
		struct mm_slink *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_is_expired_entry(part, entry, time)) {
			mc_action_unlink_entry(part, pred, entry);
			mm_stack_insert(freelist, &entry->link);
		} else {
			if (mc_action_match_entry(action, entry)) {
				mc_action_unlink_entry(action->part, pred, entry);
				mm_stack_insert(freelist, &entry->link);
				action->old_entry = entry;
				return;
			}
			pred = link;
		}
	}

	action->old_entry = NULL;
}

static void
mc_action_bucket_update(struct mc_action_storage *action,
			struct mm_stack *bucket,
			struct mm_stack *freelist)
{
	struct mc_tpart *part = action->base.part;
	struct mm_slink *pred = &bucket->head;
	uint32_t time = mc_action_get_exp_time();

	while (!mm_stack_is_tail(pred)) {
		struct mm_slink *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		if (mc_action_is_expired_entry(part, entry, time)) {
			mc_action_unlink_entry(part, pred, entry);
			mm_stack_insert(freelist, &entry->link);
		} else {
			if (mc_action_match_entry(&action->base, entry)) {
				action->base.old_entry = entry;
				action->entry_match = (!action->stamp
						       || action->stamp == entry->stamp);
				if (action->entry_match) {
					uint8_t state = entry->state;
					mc_action_unlink_entry(action->base.part, pred, entry);
					mm_stack_insert(freelist, &entry->link);
					mc_action_bucket_insert(action, bucket, state);
				}
				return;
			}
			pred = link;
		}
	}

	action->base.old_entry = NULL;
	action->entry_match = false;
}

static struct mm_stack *
mm_action_bucket_start(struct mc_action *action, struct mm_stack *freelist)
{
	mm_stack_prepare(freelist);

	mc_table_lookup_lock(action->part);

	uint32_t index = mc_table_index(action->part, action->hash);
	return &action->part->buckets[index];
}

static void
mc_action_bucket_finish(struct mc_action *action, struct mm_stack *freelist)
{
	mc_table_lookup_unlock(action->part);

	if (!mm_stack_empty(freelist)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, freelist);
		mc_table_freelist_unlock(action->part);
	}
}

static void
mc_action_complete(struct mc_action *action UNUSED)
{
#if ENABLE_MEMCACHE_COMBINER
	mm_memory_store_fence();
	action->ready = 1;
#endif
}

/**********************************************************************
 * Table actions.
 **********************************************************************/

void
mc_action_lookup_low(struct mc_action *action)
{
	ENTER();

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(action, &freelist);

	mc_action_bucket_lookup(action, bucket, &freelist);
	if (action->old_entry != NULL) {
		mc_action_ref_entry(action->old_entry);
		mc_action_access_entry(action->old_entry);
	}

	mc_action_bucket_finish(action, &freelist);

	mc_action_complete(action);

	LEAVE();
}

void
mc_action_finish_low(struct mc_action *action)
{
	ENTER();

	if (mc_action_unref_entry(action->old_entry)) {
		mc_table_freelist_lock(action->part);
		mc_action_free_chunks(action->part, action->old_entry);
		mc_action_free_entry(action->part, action->old_entry);
		mc_table_freelist_unlock(action->part);
	}

	mc_action_complete(action);

	LEAVE();
}

void
mc_action_delete_low(struct mc_action *action)
{
	ENTER();

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(action, &freelist);

	mc_action_bucket_delete(action, bucket, &freelist);

	mc_action_bucket_finish(action, &freelist);

	mc_action_complete(action);

	LEAVE();
}

void
mc_action_create_low(struct mc_action_storage *action)
{
	ENTER();

	struct mc_tpart *const part = action->base.part;
	mc_table_freelist_lock(part);

	for (;;) {
		if (!mm_stack_empty(&part->free_list)) {
			struct mm_slink *link = mm_stack_remove(&part->free_list);
			action->new_entry = containerof(link, struct mc_entry, link);
			ASSERT(part->nentries_free);
			part->nentries_free--;
			break;
		}

		if (part->nentries_void) {
			action->new_entry = part->entries_end++;
			part->nentries_void--;
			break;
		}
		if (mc_table_expand(part, mc_table.nentries_increment)) {
			ASSERT(part->nentries_void);
			action->new_entry = part->entries_end++;
			part->nentries_void--;
			break;
		}

		mc_table_freelist_unlock(part);

		struct mm_stack victims;
		mc_table_lookup_lock(part);
		mc_action_find_victims(part, &victims, 1);
		mc_table_lookup_unlock(part);

		mc_table_freelist_lock(part);
		mc_action_free_entries(part, &victims);
	}

	ASSERT(action->new_entry->state == MC_ENTRY_FREE);
	action->new_entry->state = MC_ENTRY_NOT_USED;
	action->new_entry->ref_count = 1;

	action->new_entry->hash = action->base.hash;
	action->new_entry->key_len = action->base.key_len;
	action->new_entry->value_len = action->value_len;
	mc_action_alloc_chunks(part, action->new_entry);

	mc_table_freelist_unlock(part);
	mc_table_reserve_entries(part);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_resize_low(struct mc_action_storage *action)
{
	ENTER();

	action->new_entry->value_len = action->value_len;

	struct mc_tpart *const part = action->base.part;
	mc_table_freelist_lock(part);
	mc_action_free_chunks(part, action->new_entry);
	mc_action_alloc_chunks(part, action->new_entry);
	mc_table_freelist_unlock(part);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_cancel_low(struct mc_action_storage *action)
{
	ENTER();

	struct mc_tpart *const part = action->base.part;
	mc_table_freelist_lock(part);
	mc_action_free_chunks(part, action->new_entry);
	mc_action_free_entry(part, action->new_entry);
	mc_table_freelist_unlock(part);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_insert_low(struct mc_action_storage *action)
{
	ENTER();

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(&action->base, &freelist);

	mc_action_bucket_lookup(&action->base, bucket, &freelist);
	if (action->base.old_entry == NULL)
		mc_action_bucket_insert(action, bucket, MC_ENTRY_USED_MIN);

	mc_action_bucket_finish(&action->base, &freelist);

	if (action->base.old_entry == NULL)
		mc_table_reserve_volume(action->base.part);
	else
		mc_action_cancel_low(action);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_update_low(struct mc_action_storage *action)
{
	ENTER();

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(&action->base, &freelist);

	mc_action_bucket_update(action, bucket, &freelist);
	if (action->entry_match)
		mc_action_access_entry(action->new_entry);

	mc_action_bucket_finish(&action->base, &freelist);

	if (action->entry_match)
		mc_table_reserve_volume(action->base.part);
	else
		mc_action_cancel_low(action);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_upsert_low(struct mc_action_storage *action)
{
	ENTER();

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(&action->base, &freelist);

	mc_action_bucket_delete(&action->base, bucket, &freelist);
	mc_action_bucket_insert(action, bucket, MC_ENTRY_USED_MIN);

	mc_action_bucket_finish(&action->base, &freelist);

	mc_table_reserve_volume(action->base.part);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_alter_low(struct mc_action_storage *action)
{
	ENTER();

	uint32_t flags = action->base.old_entry->flags;
	uint32_t exp_time = action->base.old_entry->exp_time;
	mc_action_finish_low(&action->base);

	struct mm_stack freelist;
	struct mm_stack *bucket = mm_action_bucket_start(&action->base, &freelist);

	mc_action_bucket_update(action, bucket, &freelist);
	if (action->entry_match) {
		mc_action_access_entry(action->new_entry);
		action->new_entry->flags = flags;
		action->new_entry->exp_time = exp_time;
	} else if (action->base.old_entry != NULL) {
		mc_action_ref_entry(action->base.old_entry);
	}

	mc_action_bucket_finish(&action->base, &freelist);

	if (action->entry_match)
		mc_table_reserve_volume(action->base.part);
	else if (action->base.old_entry == NULL)
		mc_action_cancel_low(action);

	mc_action_complete(&action->base);

	LEAVE();
}

void
mc_action_stride_low(struct mc_action *action)
{
	ENTER();

	mc_table_lookup_lock(action->part);

	uint32_t used = action->part->nbuckets;
	uint32_t half_size = mm_lower_pow2(used);
	if (unlikely(used == half_size))
		mc_table_buckets_resize(action->part, used, used * 2);

	uint32_t target = used;
	uint32_t source = used - half_size;
	uint32_t mask = half_size + half_size - 1;

	for (uint32_t count = 0; count < MC_TABLE_STRIDE; count++) {
		struct mm_stack s_entries, t_entries;
		mm_stack_prepare(&s_entries);
		mm_stack_prepare(&t_entries);

		struct mm_slink *link = mm_stack_head(&action->part->buckets[source]);
		while (link != NULL) {
			struct mm_slink *next = link->next;

			struct mc_entry *entry = containerof(link, struct mc_entry, link);
			uint32_t index = (entry->hash >> mc_table.part_bits) & mask;
			if (index == source) {
				mm_stack_insert(&s_entries, link);
			} else {
				ASSERT(index == target);
				mm_stack_insert(&t_entries, link);
			}

			link = next;
		}

		action->part->buckets[source++] = s_entries;
		action->part->buckets[target++] = t_entries;
	}

	used += MC_TABLE_STRIDE;
	action->part->nbuckets = used;

	mc_table_lookup_unlock(action->part);

	mc_action_complete(action);

	LEAVE();
}

void
mc_action_evict_low(struct mc_action *action)
{
	ENTER();

	struct mm_stack victims;
	mc_table_lookup_lock(action->part);
	bool found = mc_action_find_victims(action->part, &victims, 32);
	mc_table_lookup_unlock(action->part);

	if (found) {
		mc_table_freelist_lock(action->part);
		mc_action_free_entries(action->part, &victims);
		mc_table_freelist_unlock(action->part);
	}

	mc_action_complete(action);

	LEAVE();
}

void
mc_action_flush_low(struct mc_action *action)
{
	ENTER();

	mc_table_lookup_lock(action->part);
	action->part->flush_stamp = action->part->stamp;
	mc_table_lookup_unlock(action->part);

	mc_action_complete(action);

	LEAVE();
}

/**********************************************************************
 * Memcache table action initialization and termination.
 **********************************************************************/

void
mc_action_start(void)
{
	ENTER();

	mc_action_exp_time_start();

	LEAVE();
}

void
mc_action_stop(void)
{
	ENTER();

	mc_action_exp_time_stop();

	LEAVE();
}
