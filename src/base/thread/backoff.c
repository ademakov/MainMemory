/*
 * base/thread/backoff.c - MainMemory contention back off.
 *
 * Copyright (C) 2014-2017  Aleksey Demakov
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

#include "base/thread/backoff.h"

#include "base/context.h"
#include "base/event/event.h"
#include "base/fiber/fiber.h"
#include "base/thread/thread.h"

uint32_t
mm_thread_backoff_slow(uint32_t count)
{
	struct mm_context *const context = mm_context_selfptr();
	if (context != NULL) {
		// Handle any pending async calls.
		mm_event_handle_calls(context);
		// If there are any waiting working fibers and this is a
		// working fiber too then yield to let them make progress.
		struct mm_strand *const strand = context->strand;
		if (!mm_runq_empty_above(&strand->runq, MM_PRIO_MASTER) && strand->fiber->priority < MM_PRIO_MASTER) {
			mm_fiber_yield();
			return count + count + 1;
		}
	}

	// If spinning for too long then yield the CPU to another thread/process.
	if (count >= 0xffff) {
		mm_thread_yield();
		return 0;
	}

	mm_thread_backoff_fixed(count & 0xfff);
	return count + count + 1;
}
