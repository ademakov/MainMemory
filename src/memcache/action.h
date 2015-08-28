/*
 * memcache/action.h - MainMemory memcache table actions.
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

#ifndef MEMCACHE_ACTION_H
#define	MEMCACHE_ACTION_H

#include "common.h"
#include "memcache/table.h"
#include "base/hash.h"
#include "base/cksum.h"
#include "core/core.h"

#if ENABLE_MEMCACHE_COMBINER
# include "base/combiner.h"
#endif
#if ENABLE_MEMCACHE_DELEGATE
# include "core/future.h"
#endif

#if ENABLE_MEMCACHE_COMBINER
typedef enum {

	MC_ACTION_DONE,

	/* Search for an entry. */
	MC_ACTION_LOOKUP,
	/* Finish using found entry. */
	MC_ACTION_FINISH,

	/* Delete existing entry if any. */
	MC_ACTION_DELETE,

	/* Create a fresh entry. */
	MC_ACTION_CREATE,
	/* Abandon a created entry. */
	MC_ACTION_CANCEL,
	/* Insert newly created entry. */
	MC_ACTION_INSERT,
	/* Replace existing entry if any. */
	MC_ACTION_UPDATE,
	/* Either insert new or replace existing entry. */
	MC_ACTION_UPSERT,

	MC_ACTION_STRIDE,

	MC_ACTION_EVICT,

	MC_ACTION_FLUSH,

} mc_action_t;
#endif

struct mc_action
{
	const char *key;
	uint32_t key_len;
	uint32_t hash;

	uint32_t value_len;

	/* Input flags indicating if update should retain old and new
	   entry references after the action. */
	bool use_old_entry;
	/* Output flag indicating if the entry match succeeded. */
	bool entry_match;

	struct mc_tpart *part;
	struct mc_entry *new_entry;
	struct mc_entry *old_entry;

	/* If not zero then match it against old_entry stamp. */
	uint64_t stamp;

#if ENABLE_MEMCACHE_COMBINER
	mc_action_t action;
#endif
#if ENABLE_MEMCACHE_DELEGATE
	struct mm_future future;
#endif
};

void mc_action_lookup_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_finish_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_delete_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_create_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_cancel_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_insert_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_update_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_upsert_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_stride_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_evict_low(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_action_flush_low(struct mc_action *action)
	__attribute__((nonnull(1)));

static inline void __attribute__((nonnull(1)))
mc_action_cleanup(struct mc_action *action __mm_unused__)
{
#if ENABLE_MEMCACHE_DELEGATE
	mm_future_unique_cleanup(&action->future);
#endif
}

static inline void
mc_action_hash(struct mc_action *action)
{
	action->hash = mc_hash(action->key, action->key_len);
	action->part = mc_table_part(action->hash);
}

static inline void
mc_action_set_key(struct mc_action *action, const char *key, uint32_t key_len)
{
	action->key_len = key_len;
	action->key = key;
	mc_action_hash(action);
}

static inline void
mc_action_wait(struct mc_action *action __mm_unused__)
{
#if ENABLE_MEMCACHE_COMBINER
	while (action->action != MC_ACTION_DONE)
		mm_spin_pause();
	mm_memory_load_fence();
#elif ENABLE_MEMCACHE_DELEGATE
	mm_future_unique_wait(&action->future);
#endif
}

#if ENABLE_MEMCACHE_COMBINER
static inline void
mc_combiner_execute(struct mc_action *action,
		    mc_action_t action_tag)
{
	action->action = action_tag;
	mm_combiner_execute(action->part->combiner, (uintptr_t) action);
	mc_action_wait(action);
}
#endif

#if ENABLE_MEMCACHE_DELEGATE
static inline void
mc_delegate_execute(struct mc_action *action,
		    void (*action_routine)(struct mc_action *))
{
	mm_future_unique_prepare(&action->future,
				 (mm_routine_t) action_routine,
				 (mm_value_t) action);
	mm_future_unique_start(&action->future, action->part->core);
	mc_action_wait(action);
}
#endif

static inline void
mc_action_lookup(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_LOOKUP);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_lookup_low);
#else
	mc_action_lookup_low(action);
#endif
}

static inline void
mc_action_finish(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_FINISH);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_finish_low);
#else
	mc_action_finish_low(action);
#endif
}

static inline void
mc_action_delete(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_DELETE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_delete_low);
#else
	mc_action_delete_low(action);
#endif
}

static inline void
mc_action_create(struct mc_action *action, uint32_t value_len)
{
	action->value_len = value_len;
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_CREATE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_create_low);
#else
	mc_action_create_low(action);
#endif
}

static inline void
mc_action_cancel(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_CANCEL);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_cancel_low);
#else
	mc_action_cancel_low(action);
#endif
}

static inline void
mc_action_insert(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_INSERT);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_insert_low);
#else
	mc_action_insert_low(action);
#endif
}

static inline void
mc_action_update(struct mc_action *action, bool use_old_entry)
{
	action->use_old_entry = use_old_entry;
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_UPDATE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_update_low);
#else
	mc_action_update_low(action);
#endif
}

static inline void
mc_action_upsert(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_UPSERT);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_upsert_low);
#else
	mc_action_upsert_low(action);
#endif
}

static inline void
mc_action_stride(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_STRIDE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_stride_low);
#else
	mc_action_stride_low(action);
#endif
}

static inline void
mc_action_evict(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_EVICT);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_evict_low);
#else
	mc_action_evict_low(action);
#endif
}

static inline void
mc_action_flush(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_FLUSH);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_flush_low);
#else
	mc_action_flush_low(action);
#endif
}

#if ENABLE_MEMCACHE_COMBINER
void mc_action_perform(uintptr_t data);
#endif

#endif /* MEMCACHE_ACTION_H */
