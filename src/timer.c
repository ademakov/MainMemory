/*
 * timer.c - MainMemory timers.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#include "timer.h"

#include "core.h"
#include "pool.h"
#include "sched.h"
#include "timeq.h"
#include "util.h"
#include "work.h"

/* Generic timer. */
struct mm_timer
{
	struct mm_timeq_entry entry;

	/* Clock type. */
	mm_clock_t clock;

	/* Task parameters. */
	mm_task_flags_t flags;
	mm_routine start;
	uintptr_t start_arg;

	/* Absolute time flag. */
	bool abstime;

	/* Expiration time. */
	mm_timeval_t value;

	/* Time interval for periodical timers. */
	mm_timeval_t interval;
};

/* Stripped down timer used just to resume paused task. */
struct mm_timer_pause
{
	struct mm_timeq_entry entry;

	/* The task to schedule. */
	struct mm_task *task;
};

/* The memory pool for timers. */
static struct mm_pool mm_timer_pool;

static bool
mm_timer_is_armed(struct mm_timer *timer)
{
	return (timer->entry.index == MM_TIMEQ_INDEX_NO);
}

static void
mm_timer_fire(struct mm_timeq_entry *entry)
{
	ENTER();

	if (entry->ident == MM_TIMER_SLEEP) {
		struct mm_timer_pause *pause =
			containerof(entry, struct mm_timer_pause, entry);
		mm_sched_run(pause->task);
	} else {
		struct mm_timer *timer =
			containerof(entry, struct mm_timer, entry);

		if (likely(timer->start)) {
			mm_work_add(timer->flags, timer->start, timer->start_arg);
		}

		if (timer->interval) {
			entry->value = mm_core->time_value + timer->interval;
			mm_timeq_insert(mm_core->time_queue, entry);
		}
	}

	LEAVE();
}

void
mm_timer_init(void)
{
	ENTER();

	mm_pool_init(&mm_timer_pool, "timer", sizeof (struct mm_timer));

	LEAVE();
}

void
mm_timer_term(void)
{
	ENTER();

	mm_pool_discard(&mm_timer_pool);

	LEAVE();
}

void
mm_timer_tick(void)
{
	ENTER();

	mm_core_update_time();

	struct mm_timeq_entry *entry = mm_timeq_getmin(mm_core->time_queue);
	while (entry->value <= mm_core->time_value) {
		mm_timeq_delete(mm_core->time_queue, entry);
		mm_timer_fire(entry);

		entry = mm_timeq_getmin(mm_core->time_queue);
	}

	LEAVE();
}

mm_timer_t
mm_timer_create(mm_clock_t clock, mm_task_flags_t flags,
		mm_routine start, uintptr_t start_arg)
{
	ENTER();

	struct mm_timer *timer = mm_pool_alloc(&mm_timer_pool);
	mm_timer_t timer_id = mm_pool_ptr2idx(&mm_timer_pool, timer);

	// Check for timer_id overflow over the MM_TIMER_SLEEP value.
	if (unlikely(timer_id == MM_TIMER_SLEEP)) {
		mm_pool_free(&mm_timer_pool, timer);

		timer_id = MM_TIMER_ERROR;
		errno = EAGAIN;
		goto done;
	}

	mm_timeq_entry_init(&timer->entry, MM_TIMEVAL_MAX, timer_id);
	timer->clock = clock;
	timer->flags = flags;
	timer->start = start;
	timer->start_arg = start_arg;
	timer->value = MM_TIMEVAL_MAX;
	timer->interval = 0;

done:
	LEAVE();
	return timer_id;
}

void
mm_timer_destroy(mm_timer_t timer_id)
{
	ENTER();

	struct mm_timer *timer = mm_pool_idx2ptr(&mm_timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timer_is_armed(timer))
		mm_timeq_delete(mm_core->time_queue, &timer->entry);

	mm_pool_free(&mm_timer_pool, timer);

	LEAVE();
}

void
mm_timer_settime(mm_timer_t timer_id, bool abstime,
		 mm_timeval_t value, mm_timeval_t interval)
{
	ENTER();

	struct mm_timer *timer = mm_pool_idx2ptr(&mm_timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timer_is_armed(timer))
		mm_timeq_delete(mm_core->time_queue, &timer->entry);

	timer->abstime = abstime;
	timer->value = value;
	timer->interval = interval;

	if (value != 0) {
		if (timer->clock == MM_CLOCK_MONOTONIC) {
			if (abstime) {
				timer->entry.value = value;
			} else {
				timer->entry.value = value + mm_core->time_value;
			}
		} else {
			if (abstime) {
				mm_core_update_real_time();
				timer->entry.value = value - mm_core->real_time_value + mm_core->time_value;
			} else {
				timer->entry.value = value + mm_core->time_value;
			}
		}

		mm_timeq_insert(mm_core->time_queue, &timer->entry);
	}

	LEAVE();
}

void
mm_timer_sleep(mm_timeout_t timeout)
{
	ENTER();

	struct mm_timer_pause timer;
	mm_timeq_entry_init(&timer.entry,
			    mm_core->time_value + timeout,
			    MM_TIMER_SLEEP);
	timer.task = mm_running_task;

	mm_timeq_insert(mm_core->time_queue, &timer.entry);

	mm_sched_block();

	LEAVE();
}
