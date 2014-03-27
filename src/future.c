/*
 * future.c - MainMemory delayed computation.
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

#include "future.h"

#include "alloc.h"
#include "core.h"
#include "log.h"
#include "pool.h"
#include "trace.h"

static void
mm_future_finish(struct mm_future *future,
		 mm_future_status_t status,
		 mm_value_t result)
{
	ENTER();

	// Synchronize with waiters.
	mm_task_lock(&future->lock);

	// Reset the task reference.
	mm_memory_store(future->task, NULL);

	// Store the result first, ensure it will be ready on the status update.
	mm_memory_store(future->result, result);
	mm_memory_store_fence();

	// Update the future status.
	ASSERT(mm_memory_load(future->status.value) == MM_FUTURE_STARTED);
	mm_memory_store(future->status.value, status);

	// Wakeup all the waiters.
	mm_waitset_broadcast(&future->waitset, &future->lock);

	LEAVE();
}

static void
mm_future_cleanup(struct mm_future *future)
{
	ENTER();

	mm_future_finish(future, MM_FUTURE_CANCELED, MM_TASK_CANCELED);

	LEAVE();
}

static mm_value_t
mm_future_routine(mm_value_t arg)
{
	ENTER();

	struct mm_future *future = (struct mm_future *) arg;
	ASSERT(mm_memory_load(future->status.value) == MM_FUTURE_STARTED);

	// Ensure cleanup on task exit/cancellation.
	mm_task_cleanup_push(mm_future_cleanup, future);

	// Actually start the future unless already canceled.
	bool cancel = mm_memory_load(future->cancel);
	if (cancel) {
		mm_future_finish(future, MM_FUTURE_CANCELED, MM_TASK_CANCELED);
	} else {
		mm_memory_store(future->task, mm_running_task);
		mm_value_t result = future->start(future->start_arg);
		mm_future_finish(future, MM_FUTURE_COMPLETED, result);
	}

	// Cleanup on return is not needed.
	mm_task_cleanup_pop(false);

	LEAVE();
	return 0;
}

void
mm_future_init(void)
{
	ENTER();

	mm_pool_prepare(&mm_core->future_pool, "future", &mm_alloc_core,
			sizeof(struct mm_future));

	LEAVE();
}

void
mm_future_term(void)
{
	ENTER();

	mm_pool_cleanup(&mm_core->future_pool);

	LEAVE();
}

struct mm_future *
mm_future_create(mm_routine_t start, mm_value_t start_arg)
{
	ENTER();

	struct mm_future *future = mm_pool_alloc(&mm_core->future_pool);
	future->lock = MM_TASK_LOCK_INIT;
	future->cancel = false;
	future->status.value = MM_FUTURE_CREATED;
	future->result = MM_TASK_UNRESOLVED;
	future->start = start;
	future->start_arg = start_arg;
	future->task = NULL;
	mm_waitset_prepare(&future->waitset);

	LEAVE();
	return future;
}

void
mm_future_destroy(struct mm_future *future)
{
	ENTER();
	ASSERT(future->status.value != MM_FUTURE_STARTED);
	ASSERT(future->waitset.size == 0);

	mm_waitset_cleanup(&future->waitset);
	mm_pool_free(&mm_core->future_pool, future);

	LEAVE();
}

void
mm_future_start(struct mm_future *future, struct mm_core *core)
{
	ENTER();

	// Check if the future has already been started.
	uint8_t status = mm_memory_load(future->status.value);
	if (status != MM_FUTURE_CREATED)
		goto leave;

	// Atomically set the future status as started.
	status = mm_atomic_uint8_cas(&future->status, MM_FUTURE_CREATED, MM_FUTURE_STARTED);

	// Initiate execution of the future routine.
	if (status == MM_FUTURE_CREATED) {
		if (core == NULL) {
			mm_core_post(false, mm_future_routine, (mm_value_t) future);
		} else {
			mm_core_submit(core, mm_future_routine, (mm_value_t) future);
		}
	}

leave:
	LEAVE();
}

void
mm_future_cancel(struct mm_future *future)
{
	ENTER();

	mm_memory_store(future->cancel, true);

	// Make a synchronized check of the future status.
	mm_task_lock(&future->lock);
	uint8_t status = mm_memory_load(future->status.value);
	if (status == MM_FUTURE_STARTED) {
		struct mm_task *task = mm_memory_load(future->task);
		if (task != NULL) {
			// TODO: task cancel across cores
			// TODO: catch and stop cancel in the future routine.
#if 0
			mm_task_cancel(task);
#else
			mm_warning(0, "running future cancellation is not implemented");
#endif
		}
	}
	mm_task_unlock(&future->lock);

	LEAVE();
}

mm_value_t
mm_future_wait(struct mm_future *future)
{
	ENTER();

	// Start the future if it has not been started already.
	mm_future_start(future, NULL);

	// Wait for future completion.
	for (;;) {
		// Get the future status.
		uint8_t status = mm_memory_load(future->status.value);

		// Ensure that the future result will be ready to get.
		mm_memory_load_fence();

		// Exit the loop if the future has finished.
		if (status != MM_FUTURE_STARTED)
			break;

		// Make a synchronized check of the future status.
		mm_task_lock(&future->lock);
		status = mm_memory_load(future->status.value);
		if (status != MM_FUTURE_STARTED) {
			mm_task_unlock(&future->lock);
			break;
		}

		// Wait for completion notification.
		mm_waitset_wait(&future->waitset, &future->lock);

		// Check if the task has been canceled.
		mm_task_testcancel();
	}

	LEAVE();
	return mm_memory_load(future->result);
}

mm_value_t
mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout)
{
	ENTER();

	// Remember the wait time.
	mm_timeval_t deadline = mm_core->time_value + timeout;

	// Start the future if it has not been started already.
	mm_future_start(future, NULL);

	// Wait for future completion.
	for (;;) {
		// Get the future status.
		uint8_t status = mm_memory_load(future->status.value);

		// Ensure that the future result will be ready to get.
		mm_memory_load_fence();

		// Exit the loop if the future has finished.
		if (status != MM_FUTURE_STARTED)
			break;

		// Check if timed out.
		if (deadline <= mm_core->time_value) {
			DEBUG("future timed out");
			break;
		}

		// Make a synchronized check of the future status.
		mm_task_lock(&future->lock);
		status = mm_memory_load(future->status.value);
		if (status != MM_FUTURE_STARTED) {
			mm_task_unlock(&future->lock);
			break;
		}

		// Wait for completion notification.
		mm_waitset_timedwait(&future->waitset, &future->lock, timeout);

		// Check if the task has been canceled.
		mm_task_testcancel();
	}

	LEAVE();
	return mm_memory_load(future->result);
}
