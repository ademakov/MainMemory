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
mm_future_finish(struct mm_future *future, mm_value_t result)
{
	ENTER();

	// Synchronize with waiters.
	mm_task_lock(&future->lock);

	// Store the result.
	mm_memory_store(future->result, result);
	mm_memory_store_fence();

	// Wakeup all the waiters.
	mm_waitset_broadcast(&future->waitset, &future->lock);

	// Advertise the future task has finished. This must be the last
	// access to the future structure performed by the task.
	mm_memory_store_fence();
	mm_memory_store(future->task, NULL);

	LEAVE();
}

static void
mm_future_cleanup(struct mm_future *future)
{
	ENTER();

	mm_future_finish(future, MM_RESULT_CANCELED);

	LEAVE();
}

static mm_value_t
mm_future_routine(mm_value_t arg)
{
	ENTER();

	struct mm_future *future = (struct mm_future *) arg;
	ASSERT(mm_memory_load(future->result) == MM_RESULT_NOTREADY);

	// Ensure cleanup on task exit/cancellation.
	mm_task_cleanup_push(mm_future_cleanup, future);

	// Advertise that the future task is running.
	mm_memory_store(future->task, mm_task_self());
	mm_memory_store_fence();

	// Actually start the future unless already canceled.
	bool cancel = mm_memory_load(future->cancel);
	if (cancel) {
		mm_future_finish(future, MM_RESULT_CANCELED);
	} else {
		mm_value_t result = future->start(future->start_arg);
		ASSERT(result != MM_RESULT_NOTREADY);
		ASSERT(result != MM_RESULT_DEFERRED);
		mm_future_finish(future, result);
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

	mm_pool_prepare(&mm_core->future_pool, "future", sizeof(struct mm_future));

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
	future->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	future->cancel = false;
	future->result = MM_RESULT_DEFERRED;
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

	mm_value_t result = mm_memory_load(future->result);
	if (result != MM_RESULT_DEFERRED) {
		if (unlikely(result == MM_RESULT_NOTREADY))
			mm_fatal(0, "Destroying a running future object.");

		// There is a chance that the future task is still running
		// at this point. It is required to wait until it cannot
		// access the future structure anymore.
		uint32_t count = 0;
		while (mm_memory_load(future->task) != NULL)
			count = mm_task_backoff(count);
	}

	mm_waitset_cleanup(&future->waitset);

	mm_pool_free(&mm_core->future_pool, future);

	LEAVE();
}

mm_value_t
mm_future_start(struct mm_future *future, mm_core_t core)
{
	ENTER();

	// Atomically set the future status as started.
	mm_value_t result = mm_atomic_uintptr_cas(&future->result,
						  MM_RESULT_DEFERRED,
						  MM_RESULT_NOTREADY);

	// Initiate execution of the future routine.
	if (result == MM_RESULT_DEFERRED) {
		mm_core_post(core, mm_future_routine, (mm_value_t) future);
		result = MM_RESULT_NOTREADY;
	}

	LEAVE();
	return result;
}

void
mm_future_cancel(struct mm_future *future)
{
	ENTER();

	mm_memory_store(future->cancel, true);

	// Make a synchronized check of the future status.
	mm_task_lock(&future->lock);

	mm_value_t result = mm_memory_load(future->result);
	if (result == MM_RESULT_NOTREADY) {
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
	mm_value_t result = mm_memory_load(future->result);
	if (result == MM_RESULT_DEFERRED)
		result = mm_future_start(future, MM_CORE_NONE);

	// Wait for future completion.
	while (result == MM_RESULT_NOTREADY) {

		// Check if the task has been canceled.
		mm_task_testcancel();

		// Make a synchronized check of the future status.
		mm_task_lock(&future->lock);

		result = mm_memory_load(future->result);
		if (result != MM_RESULT_NOTREADY) {
			mm_task_unlock(&future->lock);
			break;
		}

		// Wait for completion notification.
		mm_waitset_wait(&future->waitset, &future->lock);

		// Update the future status.
		result = mm_memory_load(future->result);
	}

	LEAVE();
	return result;
}

mm_value_t
mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout)
{
	ENTER();

	// Remember the wait time.
	mm_timeval_t deadline = mm_core->time_manager.time + timeout;

	// Start the future if it has not been started already.
	mm_value_t result = mm_memory_load(future->result);
	if (result == MM_RESULT_DEFERRED)
		result = mm_future_start(future, MM_CORE_NONE);

	// Wait for future completion.
	while (result == MM_RESULT_NOTREADY) {

		// Check if the task has been canceled.
		mm_task_testcancel();

		// Check if timed out.
		if (deadline <= mm_core->time_manager.time) {
			DEBUG("future timed out");
			break;
		}

		// Make a synchronized check of the future status.
		mm_task_lock(&future->lock);

		result = mm_memory_load(future->result);
		if (result != MM_RESULT_NOTREADY) {
			mm_task_unlock(&future->lock);
			break;
		}

		// Wait for completion notification.
		mm_waitset_timedwait(&future->waitset, &future->lock, timeout);

		// Update the future status.
		result = mm_memory_load(future->result);
	}

	LEAVE();
	return result;
}
