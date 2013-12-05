/*
 * future.c - MainMemory delayed computation.
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

#include "future.h"

#include "alloc.h"
#include "core.h"
#include "pool.h"
#include "trace.h"

static void
mm_future_cleanup(struct mm_future *future)
{
	ENTER();

	// Reset the task reference.
	struct mm_task *task = future->task;
	mm_memory_store(future->task, NULL);

	// Store the completion status and result.
	if (mm_memory_load(future->status) == MM_FUTURE_STARTED) {
		if ((task->flags & MM_TASK_CANCEL_OCCURRED) != 0) {
			mm_memory_store(future->status, MM_FUTURE_CANCELED);
		} else {
			mm_memory_store(future->result, task->result);
			mm_memory_store_fence();
			mm_memory_store(future->status, MM_FUTURE_COMPLETED);
		}
	}

	// Wakeup all the waiters.
	mm_core_lock(&future->lock);
	mm_waitset_broadcast(&future->waitset, &future->lock);

	LEAVE();
}

static mm_result_t
mm_future_routine(uintptr_t arg)
{
	ENTER();

	struct mm_future *future = (struct mm_future *) arg;
	ASSERT(mm_memory_load(future->status) == MM_FUTURE_STARTED);

	// Ensure cleanup on exit.
	mm_task_cleanup_push(mm_future_cleanup, future);

	// Actually start the future unless already canceled.
	mm_memory_store(future->task, mm_running_task);
	if (mm_memory_load(future->cancel))
		mm_memory_store(future->status, MM_FUTURE_CANCELED);
	else
		future->start(future->start_arg);

	// Cleanup on return.
	mm_task_cleanup_pop(true);

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
mm_future_create(mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	struct mm_future *future = mm_pool_alloc(&mm_core->future_pool);
	future->lock = (mm_core_lock_t) MM_ATOMIC_LOCK_INIT;
	future->cancel = false;
	future->status = MM_FUTURE_CREATED;
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
	ASSERT(future->status != MM_FUTURE_STARTED);
	ASSERT(future->waitset.size == 0);

	mm_waitset_cleanup(&future->waitset);
	mm_pool_free(&mm_core->future_pool, future);

	LEAVE();
}

void
mm_future_start(struct mm_future *future, struct mm_core *core)
{
	ENTER();

	// TODO: use atomic CAS instead of lock.
	mm_core_lock(&future->lock);
	uint8_t status = future->status;
	if (status == MM_FUTURE_CREATED)
		future->status = MM_FUTURE_STARTED;
	mm_core_lock(&future->lock);

	if (status == MM_FUTURE_CREATED) {
		if (core == NULL) {
			mm_core_post(false, mm_future_routine, (uintptr_t) future);
		} else {
			mm_core_submit(core, mm_future_routine, (uintptr_t) future);
		}
	}

	LEAVE();
}

void
mm_future_cancel(struct mm_future *future)
{
	ENTER();

	mm_memory_store(future->cancel, true);

	// TODO: cancel the future task if it is already running.

	LEAVE();
}

mm_result_t
mm_future_wait(struct mm_future *future)
{
	ENTER();

	if (mm_memory_load(future->status) == MM_FUTURE_CREATED)
		mm_future_start(future, NULL);

	int cp = mm_task_enter_cancel_point();

	if (mm_memory_load(future->status) == MM_FUTURE_STARTED) {
		for (;;) {
			mm_core_lock(&future->lock);
			if (future->status != MM_FUTURE_STARTED)
				break;
			mm_waitset_wait(&future->waitset, &future->lock);
		}
	}

	mm_task_leave_cancel_point(cp);

	ENTER();
	return future->result;
}

mm_result_t
mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout)
{
	ENTER();

	if (mm_memory_load(future->status) == MM_FUTURE_CREATED)
		mm_future_start(future, NULL);

	int cp = mm_task_enter_cancel_point();

	if (mm_memory_load(future->status) == MM_FUTURE_STARTED) {
		for (;;) {
			mm_core_lock(&future->lock);
			if (future->status != MM_FUTURE_STARTED)
				break;
			mm_waitset_timedwait(&future->waitset, &future->lock, timeout);
			// TODO: break if timed out!!!
		}
	}

	mm_task_leave_cancel_point(cp);

	LEAVE();
	return future->result;
}
