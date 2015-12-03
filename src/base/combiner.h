/*
 * base/combiner.h - MainMemory combining synchronization.
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

#ifndef BASE_COMBINER_H
#define BASE_COMBINER_H

#include "common.h"
#include "base/ring.h"

typedef void (*mm_combiner_routine_t)(uintptr_t data);

struct mm_combiner
{
	struct mm_ring_mpmc ring;
};

struct mm_combiner *
mm_combiner_create(size_t size, size_t handoff);

void NONNULL(1)
mm_combiner_destroy(struct mm_combiner *combiner);

void NONNULL(1)
mm_combiner_prepare(struct mm_combiner *combiner, size_t size, size_t handoff);

void NONNULL(1, 2)
mm_combiner_execute(struct mm_combiner *combiner, mm_combiner_routine_t routine, uintptr_t data);

#endif /* BASE_COMBINER_H */
