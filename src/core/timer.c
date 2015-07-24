/*
 * core/timer.c - MainMemory timers.
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

#include "core/timer.h"

#include "core/core.h"
#include "core/task.h"

#define MM_TIMER_QUEUE_MAX_WIDTH	500
#define MM_TIMER_QUEUE_MAX_COUNT	2000

/* Generic timer. */
struct mm_timer
{
	struct mm_timeq_entry entry;

	/* Clock type. */
	mm_clock_t clock;

	/* Absolute time flag. */
	bool abstime;

	/* Task parameters. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* Expiration time. */
	mm_timeval_t value;

	/* Time interval for periodical timers. */
	mm_timeval_t interval;
};

/* Stripped down timer used just to resume a sleeping task. */
struct mm_timer_resume
{
	struct mm_timeq_entry entry;

	/* The time manager the timer belongs to. */
	struct mm_time_manager *manager;

	/* The task to schedule. */
	struct mm_task *task;
};


static bool
mm_timer_is_armed(struct mm_timeq_entry *entry)
{
	return (entry->index != MM_TIMEQ_INDEX_NO);
}

static void
mm_timer_fire(struct mm_time_manager *manager, struct mm_timeq_entry *entry)
{
	ENTER();

	if (entry->ident == MM_TIMER_BLOCK) {
		struct mm_timer_resume *resume =
			containerof(entry, struct mm_timer_resume, entry);
		mm_task_run(resume->task);
	} else {
		struct mm_timer *timer =
			containerof(entry, struct mm_timer, entry);

		if (likely(timer->start)) {
			mm_core_post(MM_CORE_SELF, timer->start, timer->start_arg);
		}

		if (timer->interval) {
			entry->value = manager->time + timer->interval;
			mm_timeq_insert(manager->time_queue, entry);
		}
	}

	LEAVE();
}

void
mm_timer_init(struct mm_time_manager *manager, mm_arena_t arena)
{
	ENTER();

	// Update the time.
	mm_timer_update_time(manager);
	mm_timer_update_real_time(manager);

	// Create the time queue.
	manager->time_queue = mm_timeq_create(arena);
	mm_timeq_set_max_bucket_width(manager->time_queue, MM_TIMER_QUEUE_MAX_WIDTH);
	mm_timeq_set_max_bucket_count(manager->time_queue, MM_TIMER_QUEUE_MAX_COUNT);

	mm_pool_prepare(&manager->timer_pool, "timer", arena, sizeof (struct mm_timer));

	LEAVE();
}

void
mm_timer_term(struct mm_time_manager *manager)
{
	ENTER();

	mm_timeq_destroy(manager->time_queue);
	mm_pool_cleanup(&manager->timer_pool);

	LEAVE();
}

void
mm_timer_tick(struct mm_time_manager *manager)
{
	ENTER();

	struct mm_timeq_entry *entry = mm_timeq_getmin(manager->time_queue);
	while (entry != NULL && entry->value <= manager->time) {
		mm_timeq_delete(manager->time_queue, entry);
		mm_timer_fire(manager, entry);

		entry = mm_timeq_getmin(manager->time_queue);
	}

	LEAVE();
}

mm_timeval_t
mm_timer_next(struct mm_time_manager *manager)
{
	ENTER();

	mm_timeval_t value = MM_TIMEVAL_MAX;
	struct mm_timeq_entry *entry = mm_timeq_getmin(manager->time_queue);
	if (entry != NULL)
		value = entry->value;

	LEAVE();
	return value;
}

mm_timer_t
mm_timer_create(mm_clock_t clock, mm_routine_t start, mm_value_t start_arg)
{
	ENTER();

	struct mm_core *core = mm_core_selfptr();
	struct mm_time_manager *manager = &core->time_manager;
	struct mm_timer *timer = mm_pool_alloc(&manager->timer_pool);
	mm_timer_t timer_id = mm_pool_ptr2idx(&manager->timer_pool, timer);

	// Check for timer_id overflow over the MM_TIMER_BLOCK value.
	if (unlikely(timer_id == MM_TIMER_BLOCK)) {
		mm_pool_free(&manager->timer_pool, timer);

		timer_id = MM_TIMER_ERROR;
		errno = EAGAIN;
		goto leave;
	}

	mm_timeq_entry_init(&timer->entry, MM_TIMEVAL_MAX, timer_id);
	timer->clock = clock;
	timer->start = start;
	timer->start_arg = start_arg;
	timer->value = MM_TIMEVAL_MAX;
	timer->interval = 0;

leave:
	LEAVE();
	return timer_id;
}

void
mm_timer_destroy(mm_timer_t timer_id)
{
	ENTER();

	struct mm_core *core = mm_core_selfptr();
	struct mm_time_manager *manager = &core->time_manager;
	struct mm_timer *timer = mm_pool_idx2ptr(&manager->timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timer_is_armed(&timer->entry))
		mm_timeq_delete(manager->time_queue, &timer->entry);

	mm_pool_free(&manager->timer_pool, timer);

	LEAVE();
}

void
mm_timer_settime(mm_timer_t timer_id, bool abstime,
		 mm_timeval_t value, mm_timeval_t interval)
{
	ENTER();

	struct mm_core *core = mm_core_selfptr();
	struct mm_time_manager *manager = &core->time_manager;
	struct mm_timer *timer = mm_pool_idx2ptr(&manager->timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timer_is_armed(&timer->entry))
		mm_timeq_delete(manager->time_queue, &timer->entry);

	timer->abstime = abstime;
	timer->value = value;
	timer->interval = interval;

	if (value != 0) {
		if (abstime) {
			if (timer->clock == MM_CLOCK_MONOTONIC) {
				timer->entry.value = value + manager->time;
			} else {
				timer->entry.value = value - manager->real_time + manager->time;
			}
		} else {
			timer->entry.value = value + manager->time;
		}

		mm_timeq_insert(manager->time_queue, &timer->entry);
	}

	LEAVE();
}

static void
mm_timer_block_cleanup(struct mm_timer_resume *timer)
{
	mm_timeq_delete(timer->manager->time_queue, &timer->entry);
}

void
mm_timer_block(mm_timeout_t timeout)
{
	ENTER();

	struct mm_core *core = mm_core_selfptr();
	struct mm_time_manager *manager = &core->time_manager;
	mm_timeval_t time = manager->time + timeout;
	DEBUG("time: %llu", time);

	struct mm_timer_resume timer = { .manager = manager,
					 .task = mm_task_selfptr() };
	mm_timeq_entry_init(&timer.entry, time, MM_TIMER_BLOCK);

	mm_task_cleanup_push(mm_timer_block_cleanup, &timer);

	mm_timeq_insert(manager->time_queue, &timer.entry);
	mm_task_block();

	mm_task_cleanup_pop(mm_timer_is_armed(&timer.entry));

	LEAVE();
}
