/*
 * core/timer.h - MainMemory timers.
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

#ifndef CORE_TIMER_H
#define CORE_TIMER_H

#include "common.h"
#include "base/clock.h"
#include "base/timeq.h"
#include "base/log/trace.h"
#include "base/mem/arena.h"
#include "base/mem/pool.h"

#define MM_TIMER_ERROR	((mm_timer_t) -1)
#define MM_TIMER_BLOCK	((mm_timer_t) -2)

typedef mm_timeq_ident_t mm_timer_t;

struct mm_time_manager
{
	/* The (almost) current monotonic time. */
	mm_timeval_t time;

	/* The (almost) current real time. */
	mm_timeval_t real_time;

	/* Queue of delayed tasks. */
	struct mm_timeq *time_queue;

	/* Memory pool for timers. */
	struct mm_pool timer_pool;
};

void mm_timer_init(struct mm_time_manager *manager, mm_arena_t arena)
	__attribute__((nonnull(1)));

void mm_timer_term(struct mm_time_manager *manager)
	__attribute__((nonnull(1)));

void mm_timer_tick(struct mm_time_manager *manager)
	__attribute__((nonnull(1)));

mm_timeval_t mm_timer_next(struct mm_time_manager *manager)
	__attribute__((nonnull(1)));

mm_timer_t mm_timer_create(mm_clock_t clock,
			   mm_routine_t start,
			   mm_value_t start_arg)
	__attribute__((nonnull(2)));

void mm_timer_destroy(mm_timer_t timer_id);

void mm_timer_settime(mm_timer_t timer_id, bool abstime,
		      mm_timeval_t value, mm_timeval_t interval);

void mm_timer_block(mm_timeout_t timeout);

static inline void
mm_timer_update_time(struct mm_time_manager *manager)
{
	manager->time = mm_clock_gettime_monotonic();
	TRACE("%lld", (long long) manager->time);
}

static inline void
mm_timer_update_real_time(struct mm_time_manager *manager)
{
	manager->real_time = mm_clock_gettime_realtime();
	TRACE("%lld", (long long) manager->real_time);
}

#endif /* CORE_TIMER_H */
