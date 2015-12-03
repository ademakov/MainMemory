/*
 * core/timer.h - MainMemory timers.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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
#include "base/memory/arena.h"
#include "base/memory/pool.h"

#define MM_TIMER_ERROR	((mm_timer_t) -1)
#define MM_TIMER_BLOCK	((mm_timer_t) -2)

typedef mm_timeq_ident_t mm_timer_t;

struct mm_time_manager
{
	/* The (almost) current monotonic time. */
	mm_timeval_t clock_value;

	/* The (almost) current real time. */
	mm_timeval_t real_clock_value;

	/* Flags indicating if the clock values are stale. */
	bool clock_needs_update;
	bool real_clock_needs_update;

	/* Queue of delayed tasks. */
	struct mm_timeq *time_queue;

	/* Memory pool for timers. */
	struct mm_pool timer_pool;
};

void NONNULL(1)
mm_timer_prepare(struct mm_time_manager *manager, mm_arena_t arena);

void NONNULL(1)
mm_timer_cleanup(struct mm_time_manager *manager);

void NONNULL(1)
mm_timer_tick(struct mm_time_manager *manager);

mm_timeval_t NONNULL(1)
mm_timer_next(struct mm_time_manager *manager);

mm_timer_t NONNULL(2)
mm_timer_create(mm_clock_t clock, mm_routine_t start, mm_value_t start_arg);

void
mm_timer_destroy(mm_timer_t timer_id);

void
mm_timer_settime(mm_timer_t timer_id, bool abstime, mm_timeval_t value, mm_timeval_t interval);

void
mm_timer_block(mm_timeout_t timeout);

static inline void
mm_timer_resetclocks(struct mm_time_manager *manager)
{
	manager->clock_needs_update = true;
	manager->real_clock_needs_update = true;
}

static inline void
mm_timer_updateclock(struct mm_time_manager *manager)
{
	manager->clock_needs_update = false;
	manager->clock_value = mm_clock_gettime_monotonic();
	TRACE("%lld", (long long) manager->clock_value);
}

static inline void
mm_timer_updaterealclock(struct mm_time_manager *manager)
{
	manager->real_clock_needs_update = false;
	manager->real_clock_value = mm_clock_gettime_realtime();
	TRACE("%lld", (long long) manager->real_clock_value);
}

static inline mm_timeval_t
mm_timer_getclocktime(struct mm_time_manager *manager)
{
	if (manager->clock_needs_update)
		mm_timer_updateclock(manager);
	return manager->clock_value;
}

static inline mm_timeval_t
mm_timer_getrealclocktime(struct mm_time_manager *manager)
{
	if (manager->real_clock_needs_update)
		mm_timer_updaterealclock(manager);
	return manager->real_clock_value;
}

#endif /* CORE_TIMER_H */
