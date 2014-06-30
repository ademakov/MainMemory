/*
 * lock.h - MainMemory contention back off.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef BACKOFF_H
#define BACKOFF_H

#include "common.h"
#include "arch/spin.h"
#include "task.h"
#include "thread.h"
#include "trace.h"

static inline uint32_t
mm_task_backoff(uint32_t count)
{
#if ENABLE_SMP
	ASSERT(mm_running_task != NULL);

	if (count > 0xff) {
		count = 0;
		mm_task_yield();
	}

	for (uint32_t n = count; n; n--)
		mm_spin_pause();

	return count * 2 + 1;
#else
	return count;
#endif
}

static inline uint32_t
mm_thread_backoff(uint32_t count)
{
#if ENABLE_SMP
	if (count > 0xff) {
		if (count > 0x7ff) {
			count = 0;
			mm_thread_yield();
		} else if (mm_running_task != NULL) {
			mm_task_yield();
		}
	}
#else
	if (count > 0x7ff) {
		count = 0;
		mm_thread_yield();
	}
#endif

	for (uint32_t n = count & 0xff; n; n--)
		mm_spin_pause();

	return count * 2 + 1;
}

#endif /* BACKOFF_H */
