/*
 * base/fiber/timer.c - MainMemory timers.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#include "base/fiber/timer.h"

#include "base/event/task.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"

#define MM_TIMER_QUEUE_MAX_WIDTH	500
#define MM_TIMER_QUEUE_MAX_COUNT	2000

/* Generic timer. */
struct mm_timer
{
	/* A timer queue node. */
	struct mm_timeq_entry entry;

	/* Clock type. */
	mm_clock_t clock;

	/* Absolute time flag. */
	bool abstime;
	/* The timer task activity task. */
	bool active;

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
	struct mm_fiber *task;
};

/**********************************************************************
 * Timer task.
 **********************************************************************/

static mm_value_t
mm_timer_execute(mm_value_t arg)
{
	struct mm_timer *timer = (struct mm_timer *) arg;
	return (timer->start)(timer->start_arg);
}

static void
mm_timer_complete(mm_value_t arg, mm_value_t result UNUSED)
{
	struct mm_timer *timer = (struct mm_timer *) arg;
	timer->active = false;
}

MM_EVENT_TASK(mm_timer_task, mm_timer_execute, mm_timer_complete, mm_event_reassign_off);

/**********************************************************************
 * Timer handling.
 **********************************************************************/

static void
mm_timer_fire(struct mm_time_manager *manager, struct mm_timeq_entry *entry)
{
	ENTER();

	if (entry->ident == MM_TIMER_BLOCK) {
		struct mm_timer_resume *resume = containerof(entry, struct mm_timer_resume, entry);
		mm_fiber_run(resume->task);
	} else {
		struct mm_timer *timer = containerof(entry, struct mm_timer, entry);

		if (likely(timer->start)) {
			if (timer->active) {
				mm_warning(0, "timer is still active");
			} else {
				timer->active = true;
				struct mm_strand *strand = mm_strand_selfptr();
				mm_event_add_task(strand->listener, &mm_timer_task, (mm_value_t) timer);
			}
		}

		if (timer->interval) {
			entry->value = mm_timer_getclocktime(manager) + timer->interval;
			mm_timeq_insert(manager->time_queue, entry);
		}
	}

	LEAVE();
}

/**********************************************************************
 * Per-thread timer manager.
 **********************************************************************/

void NONNULL(1)
mm_timer_prepare(struct mm_time_manager *manager, mm_arena_t arena)
{
	ENTER();

	// Update the time.
	mm_timer_updateclock(manager);
	mm_timer_updaterealclock(manager);

	// Create the time queue.
	manager->time_queue = mm_timeq_create(arena);
	mm_timeq_set_max_bucket_width(manager->time_queue, MM_TIMER_QUEUE_MAX_WIDTH);
	mm_timeq_set_max_bucket_count(manager->time_queue, MM_TIMER_QUEUE_MAX_COUNT);

	mm_pool_prepare(&manager->timer_pool, "timer", arena, sizeof (struct mm_timer));

	LEAVE();
}

void NONNULL(1)
mm_timer_cleanup(struct mm_time_manager *manager)
{
	ENTER();

	mm_timeq_destroy(manager->time_queue);
	mm_pool_cleanup(&manager->timer_pool);

	LEAVE();
}

void NONNULL(1)
mm_timer_tick(struct mm_time_manager *manager)
{
	ENTER();

	// Execute the timers which time has come.
	struct mm_timeq_entry *entry = mm_timeq_getmin(manager->time_queue);
	while (entry != NULL && entry->value <= mm_timer_getclocktime(manager)) {
		// Remove the timer from the queue.
		mm_timeq_delete(manager->time_queue, entry);
		// Execute the timer action.
		mm_timer_fire(manager, entry);
		// Get the next timer.
		entry = mm_timeq_getmin(manager->time_queue);
	}

	LEAVE();
}

mm_timeval_t NONNULL(1)
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

/**********************************************************************
 * Timed task execution.
 **********************************************************************/

mm_timer_t NONNULL(2)
mm_timer_create(mm_clock_t clock, mm_routine_t start, mm_value_t start_arg)
{
	ENTER();

	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_time_manager *manager = &strand->time_manager;
	struct mm_timer *timer = mm_pool_alloc(&manager->timer_pool);
	mm_timer_t timer_id = mm_pool_ptr2idx(&manager->timer_pool, timer);

	// Check for timer_id overflow over the MM_TIMER_BLOCK value.
	if (unlikely(timer_id == MM_TIMER_BLOCK)) {
		mm_pool_free(&manager->timer_pool, timer);

		timer_id = MM_TIMER_ERROR;
		errno = EAGAIN;
		goto leave;
	}

	mm_timeq_entry_prepare(&timer->entry, timer_id);
	timer->clock = clock;
	timer->active = false;
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

	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_time_manager *manager = &strand->time_manager;
	struct mm_timer *timer = mm_pool_idx2ptr(&manager->timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timeq_entry_queued(&timer->entry))
		mm_timeq_delete(manager->time_queue, &timer->entry);

	// TODO: Check if the timer is still active and delay its
	// destruction in this case.
	mm_pool_free(&manager->timer_pool, timer);

	LEAVE();
}

void
mm_timer_settime(mm_timer_t timer_id, bool abstime, mm_timeval_t value, mm_timeval_t interval)
{
	ENTER();

	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_time_manager *manager = &strand->time_manager;
	struct mm_timer *timer = mm_pool_idx2ptr(&manager->timer_pool, timer_id);
	ASSERT(timer != NULL);

	if (mm_timeq_entry_queued(&timer->entry)) {
		// TODO: Check if the timer is still active and delay its
		// re-arming in this case.
		mm_timeq_delete(manager->time_queue, &timer->entry);
	}

	timer->abstime = abstime;
	timer->value = value;
	timer->interval = interval;

	if (value != 0 || interval != 0) {
		if (abstime) {
			if (timer->clock == MM_CLOCK_MONOTONIC) {
				timer->entry.value = value;
			} else {
				mm_timeval_t time = mm_timer_getclocktime(manager);
				mm_timeval_t real_time = mm_timer_getrealclocktime(manager);
				timer->entry.value = value - real_time + time;
			}
		} else {
			mm_timeval_t time = mm_timer_getclocktime(manager);
			timer->entry.value = value + time;
		}

		mm_timeq_insert(manager->time_queue, &timer->entry);
	}

	LEAVE();
}

/**********************************************************************
 * Timed task pauses.
 **********************************************************************/

static void
mm_timer_block_cleanup(struct mm_timer_resume *timer)
{
	mm_timeq_delete(timer->manager->time_queue, &timer->entry);
}

#if ENABLE_TIMER_LOCATION
void NONNULL(2, 3)
mm_timer_block_at(mm_timeout_t timeout, const char *location, const char *function)
#else
void
mm_timer_block(mm_timeout_t timeout)
#endif
{
	ENTER();

	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_time_manager *manager = &strand->time_manager;
	mm_timeval_t time = mm_timer_getclocktime(manager) + timeout;
	DEBUG("time: %lld", (long long) time);

	struct mm_timer_resume timer = { .manager = manager,
					 .task = mm_fiber_selfptr() };
	mm_timeq_entry_prepare(&timer.entry, MM_TIMER_BLOCK);
	mm_timeq_entry_settime(&timer.entry, time);

	mm_fiber_cleanup_push(mm_timer_block_cleanup, &timer);

	mm_timeq_insert(manager->time_queue, &timer.entry);
#if ENABLE_TIMER_LOCATION && ENABLE_FIBER_LOCATION
	mm_fiber_block_at(location, function);
#else
	mm_fiber_block();
#endif

	mm_fiber_cleanup_pop(mm_timeq_entry_queued(&timer.entry));

	LEAVE();
}
