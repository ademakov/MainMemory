/*
 * core/core.h - MainMemory core.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef CORE_CORE_H
#define CORE_CORE_H

#include "common.h"

#include "core/pool.h"
#include "core/runq.h"
#include "core/timer.h"
#include "core/wait.h"

#include "base/list.h"
#include "base/mem/chunk.h"
#include "base/mem/space.h"
#include "base/log/debug.h"
#include "base/ring.h"

#include "event/listener.h"

/* Forward declarations. */
struct mm_bitset;
struct mm_chunk;
struct mm_net_server;
struct mm_task;
struct mm_work;

#define MM_CORE_SCHED_RING_SIZE		(1024)
#define MM_CORE_INBOX_RING_SIZE		(1024)
#define MM_CORE_CHUNK_RING_SIZE		(1024)

/* Virtual core state. */
struct mm_core
{
	/* Currently running task. */
	struct mm_task *task;

	/* Queue of ready to run tasks. */
	struct mm_runq runq;

	/* Queue of tasks waiting for work items. */
	struct mm_list idle;

	/* The list of tasks that have finished. */
	struct mm_list dead;

	/* Queue of pending work items. */
	struct mm_queue workq;

	/* The number of items in the work queue. */
	uint32_t nwork;

	/* Current and maximum number of worker tasks. */
	uint32_t nidle;
	uint32_t nworkers;
	uint32_t nworkers_max;

	/* Cache of free wait entries. */
	struct mm_wait_cache wait_cache;

	/* Time-related data. */
	struct mm_time_manager time_manager;

	/* Private memory space. */
	struct mm_private_space space;

	/* Master task. */
	struct mm_task *master;

	/* Dealer task. */
	struct mm_task *dealer;

	/* The bootstrap task. */
	struct mm_task *boot;

	/*
	 * The fields below engage in cross-core communication.
	 */

	/* Stop flag. */
	bool stop;

	/* Event dispatch listener. */
	struct mm_listener listener;

	/* Tasks to be scheduled. */
	MM_RING_SPSC(sched, MM_CORE_SCHED_RING_SIZE);

	/* Submitted work items. */
	MM_RING_SPSC(inbox, MM_CORE_INBOX_RING_SIZE);

	/* The memory chunks freed by other threads. */
	MM_RING_SPSC(chunks, MM_CORE_CHUNK_RING_SIZE);

} __align_cacheline;

void mm_core_init(void);
void mm_core_term(void);

void mm_core_hook_start(void (*proc)(void));
void mm_core_hook_param_start(void (*proc)(void *), void *data);
void mm_core_hook_stop(void (*proc)(void));
void mm_core_hook_param_stop(void (*proc)(void *), void *data);

void mm_core_register_server(struct mm_net_server *srv)
	__attribute__((nonnull(1)));

void mm_core_set_event_affinity(const struct mm_bitset *mask)
	__attribute__((nonnull(1)));

const struct mm_bitset * mm_core_get_event_affinity(void);

void mm_core_start(void);
void mm_core_stop(void);

void mm_core_post(mm_core_t core, mm_routine_t routine, mm_value_t routine_arg)
	__attribute__((nonnull(2)));
void mm_core_post_work(mm_core_t core_id, struct mm_work *work)
	__attribute__((nonnull(2)));

void mm_core_run_task(struct mm_task *task)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Core Information.
 **********************************************************************/

extern mm_core_t mm_core_num;
extern struct mm_core *mm_core_set;

extern __thread struct mm_core *mm_core;

static inline mm_core_t
mm_core_getnum(void)
{
#if ENABLE_SMP
	return mm_core_num;
#else
	return 1;
#endif
}

static inline mm_core_t
mm_core_getid(struct mm_core *core)
{
	if (unlikely(core == NULL))
		return MM_CORE_NONE;
	return (mm_core_t) (core - mm_core_set);
}

static inline struct mm_core *
mm_core_getptr(mm_core_t core)
{
	if (core == MM_CORE_NONE)
		return NULL;
	if (core == MM_CORE_SELF)
		return mm_core;
	ASSERT(core < mm_core_num);
	return &mm_core_set[core];
}

static inline mm_arena_t
mm_core_getarena(mm_core_t core)
{
	if (core == MM_CORE_NONE)
		return NULL;
	if (core == MM_CORE_SELF)
		return &mm_core->space.arena;
	return &mm_core_set[core].space.arena;
}

static inline struct mm_core *
mm_core_self(void)
{
	return mm_core;
}

static inline mm_core_t
mm_core_selfid(void)
{
	return mm_core_getid(mm_core_self());
}

/**********************************************************************
 * Local Core Memory Allocation Routines.
 **********************************************************************/

static inline void *
mm_local_alloc(size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_private_space_xalloc(&core->space, size);
}

static inline void *
mm_local_aligned_alloc(size_t align, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_private_space_aligned_xalloc(&core->space, align, size);
}

static inline void *
mm_local_calloc(size_t count, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_private_space_xcalloc(&core->space, count, size);
}

static inline void *
mm_local_realloc(void *ptr, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_private_space_xrealloc(&core->space, ptr, size);
}

static inline void
mm_local_free(void *ptr)
{
	struct mm_core *core = mm_core_self();
	mm_private_space_free(&core->space, ptr);
}

/**********************************************************************
 * Local Core Memory Allocation Utilities.
 **********************************************************************/

static inline void *
mm_local_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_local_alloc(size), ptr, size);
}

static inline char *
mm_local_strdup(const char *ptr)
{
	return mm_local_memdup(ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Cross-Core Memory Allocation Routines.
 **********************************************************************/

#if ENABLE_SMP
# define MM_CHUNK_SHARED	(mm_shared_chunk_type)
extern struct mm_common_space mm_shared_space;
extern mm_chunk_t mm_shared_chunk_tag;
#else
# define MM_CHUNK_SHARED	(0)
#endif

static inline void *
mm_shared_alloc(size_t size)
{
#if ENABLE_SMP
	return mm_common_space_xalloc(&mm_shared_space, size);
#else
	return mm_local_alloc(size);
#endif
}

static inline void *
mm_shared_aligned_alloc(size_t align, size_t size)
{
#if ENABLE_SMP
	return mm_common_space_aligned_xalloc(&mm_shared_space, align, size);
#else
	return mm_local_aligned_alloc(align, size);
#endif
}

static inline void *
mm_shared_calloc(size_t count, size_t size)
{
#if ENABLE_SMP
	return mm_common_space_xcalloc(&mm_shared_space, count, size);
#else
	return mm_local_calloc(count, size);
#endif
}

static inline void *
mm_shared_realloc(void *ptr, size_t size)
{
#if ENABLE_SMP
	return mm_common_space_xrealloc(&mm_shared_space, ptr, size);
#else
	return mm_local_realloc(ptr, size);
#endif
}

static inline void
mm_shared_free(void *ptr)
{
#if ENABLE_SMP
	mm_common_space_free(&mm_shared_space, ptr);
#else
	mm_local_free(ptr);
#endif
}

/**********************************************************************
 * Cross Core Memory Allocation Utilities.
 **********************************************************************/

static inline void *
mm_shared_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_shared_alloc(size), ptr, size);
}

static inline char *
mm_shared_strdup(const char *ptr)
{
	return mm_shared_memdup(ptr, strlen(ptr) + 1);
}

#endif /* CORE_CORE_H */
