/*
 * base/barrier.h - MainMemory barriers.
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

#ifndef BASE_BARRIER_H
#define	BASE_BARRIER_H

#include "common.h"
#include "arch/atomic.h"

struct mm_barrier
{
	uint32_t count __align_cacheline;

	mm_atomic_uint32_t value;

	uint32_t sense __align_cacheline;
};

struct mm_barrier_local
{
	uint32_t sense;
};

void mm_barrier_init(struct mm_barrier *barrier, uint32_t count)
	__attribute__((nonnull(1)));

void mm_barrier_local_init(struct mm_barrier_local *local)
	__attribute__((nonnull(1)));

void mm_barrier_wait(struct mm_barrier *barrier, struct mm_barrier_local *local)
	__attribute__((nonnull(1)));

#endif /* BASE_BARRIER_H */
