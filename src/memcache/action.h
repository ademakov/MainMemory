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

	/* Create a new entry. */
	MC_ACTION_CREATE,
	/* Resize a new entry. */
	MC_ACTION_RESIZE,
	/* Abandon a newly created entry. */
	MC_ACTION_CANCEL,
	/* Insert a newly created entry. */
	MC_ACTION_INSERT,
	/* Replace an existing entry if any. */
	MC_ACTION_UPDATE,
	/* Either insert a new or replace an existing entry. */
	MC_ACTION_UPSERT,
	/* Replace an existing entry with conflict detection. */
	MC_ACTION_ALTER,

	MC_ACTION_STRIDE,

	MC_ACTION_EVICT,

	MC_ACTION_FLUSH,

} mc_action_t;
#endif

typedef enum
{
	MC_ACTION_ALTER_OTHER,
	MC_ACTION_ALTER_APPEND,
	MC_ACTION_ALTER_PREPEND,
} mc_action_alter_t;

struct mc_action
{
	uint32_t hash;
	uint32_t key_len;

	/* If not zero then match it against old_entry stamp. */
	uint64_t stamp;

	/* The entry key. */
	const char *key;

	/* The table partition corresponding to the key. */
	struct mc_tpart *part;

	struct mc_entry *new_entry;
	struct mc_entry *old_entry;

	/* The value length. */
	uint32_t value_len;

	/* Output flag indicating if the entry match succeeded. */
	bool entry_match;

	/* The alter action type. */
	mc_action_alter_t alter_type;

	/* The alter action value. */
	const char *alter_value;

#if ENABLE_MEMCACHE_COMBINER
	mc_action_t action;
#endif
#if ENABLE_MEMCACHE_DELEGATE
	struct mm_future future;
#endif
};

void __attribute__((nonnull(1)))
mc_action_lookup_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_finish_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_delete_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_create_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_resize_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_cancel_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_insert_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_update_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_upsert_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_alter_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_stride_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_evict_low(struct mc_action *action);

void __attribute__((nonnull(1)))
mc_action_flush_low(struct mc_action *action);

static inline void __attribute__((nonnull(1)))
mc_action_cleanup(struct mc_action *action __mm_unused__)
{
#if ENABLE_MEMCACHE_DELEGATE
	mm_future_unique_cleanup(&action->future);
#endif
}

static inline void __attribute__((nonnull(1)))
mc_action_hash(struct mc_action *action)
{
	action->hash = mc_hash(action->key, action->key_len);
	action->part = mc_table_part(action->hash);
}

static inline void __attribute__((nonnull(1, 2)))
mc_action_set_key(struct mc_action *action, const char *key, uint32_t key_len)
{
	action->key_len = key_len;
	action->key = key;
	mc_action_hash(action);
}

static inline void __attribute__((nonnull(1)))
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
static inline void __attribute__((nonnull(1)))
mc_combiner_execute(struct mc_action *action,
		    mc_action_t action_tag)
{
	action->action = action_tag;
	mm_combiner_execute(action->part->combiner, (uintptr_t) action);
	mc_action_wait(action);
}
#endif

#if ENABLE_MEMCACHE_DELEGATE
static inline void __attribute__((nonnull(1, 2)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
mc_action_resize(struct mc_action *action, uint32_t value_len)
{
	action->value_len = value_len;
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_RESIZE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_resize_low);
#else
	mc_action_resize_low(action);
#endif
}

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
mc_action_update(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_UPDATE);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_update_low);
#else
	mc_action_update_low(action);
#endif
}

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
mc_action_alter(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, MC_ACTION_ALTER);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_alter_low);
#else
	mc_action_alter_low(action);
#endif
}

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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

static inline void __attribute__((nonnull(1)))
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
