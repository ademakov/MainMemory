/*
 * base/thread/barrier.h - MainMemory barriers.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#ifndef BASE_THREAD_BARRIER_H
#define BASE_THREAD_BARRIER_H

#include "common.h"
#include "arch/atomic.h"

struct mm_thread_barrier
{
	uint32_t count CACHE_ALIGN;

	mm_atomic_uint32_t value;

	uint32_t sense CACHE_ALIGN;
};

struct mm_thread_barrier_local
{
	uint32_t sense;
};

void NONNULL(1)
mm_thread_barrier_prepare(struct mm_thread_barrier *barrier, uint32_t count);

void NONNULL(1)
mm_thread_barrier_local_prepare(struct mm_thread_barrier_local *local);

void NONNULL(1, 2)
mm_thread_barrier_wait(struct mm_thread_barrier *barrier, struct mm_thread_barrier_local *local);

#endif /* BASE_THREAD_BARRIER_H */
