/*
 * memcache/action.h - MainMemory memcache table actions.
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

#ifndef MEMCACHE_ACTION_H
#define	MEMCACHE_ACTION_H

#include "common.h"
#include "memcache/table.h"
#include "base/hash.h"
#include "base/cksum.h"

#if ENABLE_MEMCACHE_COMBINER
# include "base/combiner.h"
#endif
#if ENABLE_MEMCACHE_DELEGATE
# include "base/fiber/future.h"
#endif

struct mc_action
{
	uint32_t hash;
	uint16_t key_len;

	union
	{
		uint8_t binary_opcode;
		bool ascii_noreply;
		bool ascii_get_last;
	};

#if ENABLE_MEMCACHE_COMBINER
	uint8_t ready;
#endif

	union
	{
		struct
		{
			uint32_t binary_opaque;
			union
			{
				uint32_t binary_exp_time;
				uint16_t binary_status;
			};
		};
		uint64_t ascii_delta;
		uint32_t ascii_stats;
		uint32_t ascii_exp_time;
		uint32_t ascii_level;
	};

	/* The entry key. */
	const char *key;
	/* The table partition corresponding to the key. */
	struct mc_tpart *part;

	/* A matching table entry. */
	struct mc_entry *old_entry;

#if ENABLE_MEMCACHE_DELEGATE
	struct mm_future future;
#endif
};

struct mc_action_storage
{
	struct mc_action base;

	/* If not zero then match it against old_entry stamp. */
	uint64_t stamp;

	/* A newly created table entry. */
	struct mc_entry *new_entry;

	/* The alter action value. */
	const char *alter_value;

	/* The value length. */
	uint32_t value_len;

	/* Action value memory is owned by the command. */
	bool own_alter_value;

	/* Output flag indicating if the entry match succeeded. */
	bool entry_match;
};

void NONNULL(1)
mc_action_lookup_low(struct mc_action *action);

void NONNULL(1)
mc_action_finish_low(struct mc_action *action);

void NONNULL(1)
mc_action_delete_low(struct mc_action *action);

void NONNULL(1)
mc_action_create_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_resize_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_cancel_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_insert_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_update_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_upsert_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_alter_low(struct mc_action_storage *action);

void NONNULL(1)
mc_action_stride_low(struct mc_action *action);

void NONNULL(1)
mc_action_evict_low(struct mc_action *action);

void NONNULL(1)
mc_action_flush_low(struct mc_action *action);

static inline void NONNULL(1)
mc_action_cleanup(struct mc_action *action UNUSED)
{
#if ENABLE_MEMCACHE_DELEGATE
	mm_future_unique_cleanup(&action->future);
#endif
}

static inline void NONNULL(1)
mc_action_hash(struct mc_action *action)
{
	action->hash = mc_hash(action->key, action->key_len);
	action->part = mc_table_part(action->hash);
}

static inline void NONNULL(1, 2)
mc_action_set_key(struct mc_action *action, const char *key, uint16_t key_len)
{
	action->key_len = key_len;
	action->key = key;
	mc_action_hash(action);
}

static inline void NONNULL(1)
mc_action_wait(struct mc_action *action UNUSED)
{
#if ENABLE_MEMCACHE_COMBINER
	while (!action->ready)
		mm_spin_pause();
	mm_memory_load_fence();
#elif ENABLE_MEMCACHE_DELEGATE
	mm_future_unique_wait(&action->future);
#endif
}

#if ENABLE_MEMCACHE_COMBINER
static inline void NONNULL(1, 2)
mc_combiner_execute(struct mc_action *action, void (*routine)(struct mc_action *))
{
	action->ready = 0;
	mm_memory_fence();
	mm_combiner_execute(action->part->combiner, (mm_combiner_routine_t) routine, (uintptr_t) action);
	mc_action_wait(action);
}
#endif

#if ENABLE_MEMCACHE_DELEGATE
static inline void NONNULL(1, 2)
mc_delegate_execute(struct mc_action *action, void (*routine)(struct mc_action *))
{
	mm_future_unique_prepare(&action->future, (mm_routine_t) routine, (mm_value_t) action);
	mm_future_unique_start(&action->future, action->part->target);
	mc_action_wait(action);
}
#endif

/* Find an entry. */
static inline void NONNULL(1)
mc_action_lookup(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_lookup_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_lookup_low);
#else
	mc_action_lookup_low(action);
#endif
}

/* Finish using a found entry. */
static inline void NONNULL(1)
mc_action_finish(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_finish_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_finish_low);
#else
	mc_action_finish_low(action);
#endif
}

/* Delete a matching entry if any. */
static inline void NONNULL(1)
mc_action_delete(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_delete_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_delete_low);
#else
	mc_action_delete_low(action);
#endif
}

/* Create a new entry. */
static inline void NONNULL(1)
mc_action_create(struct mc_action_storage *action, uint32_t value_len)
{
	action->value_len = value_len;
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_create_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_create_low);
#else
	mc_action_create_low(action);
#endif
}

/* Resize a new entry. */
static inline void NONNULL(1)
mc_action_resize(struct mc_action_storage *action, uint32_t value_len)
{
	action->value_len = value_len;
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_resize_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_resize_low);
#else
	mc_action_resize_low(action);
#endif
}

/* Abandon a newly created entry. */
static inline void NONNULL(1)
mc_action_cancel(struct mc_action_storage *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_cancel_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_cancel_low);
#else
	mc_action_cancel_low(action);
#endif
}

/* Insert a newly created entry. */
static inline void NONNULL(1)
mc_action_insert(struct mc_action_storage *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_insert_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_insert_low);
#else
	mc_action_insert_low(action);
#endif
}

/* Replace a matching entry if any. */
static inline void NONNULL(1)
mc_action_update(struct mc_action_storage *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_update_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_update_low);
#else
	mc_action_update_low(action);
#endif
}

/* Either replace a matching entry or insert a new one. */
static inline void NONNULL(1)
mc_action_upsert(struct mc_action_storage *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_upsert_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_upsert_low);
#else
	mc_action_upsert_low(action);
#endif
}

/* Replace a matching entry with conflict detection. */
static inline void NONNULL(1)
mc_action_alter(struct mc_action_storage *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_alter_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_alter_low);
#else
	mc_action_alter_low(action);
#endif
}

static inline void NONNULL(1)
mc_action_stride(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_stride_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_stride_low);
#else
	mc_action_stride_low(action);
#endif
}

static inline void NONNULL(1)
mc_action_evict(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_evict_low);
#elif ENABLE_MEMCACHE_DELEGATE
	mc_delegate_execute(action, mc_action_evict_low);
#else
	mc_action_evict_low(action);
#endif
}

static inline void NONNULL(1)
mc_action_flush(struct mc_action *action)
{
#if ENABLE_MEMCACHE_COMBINER
	mc_combiner_execute(action, mc_action_flush_low);
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
