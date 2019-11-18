/*
 * base/arch/generic/atomic64.h - MainMemory 64-bit atomic ops.
 *
 * Copyright (C) 2016  Aleksey Demakov
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

#ifndef BASE_ARCH_GENERIC_ATOMIC64_H
#define BASE_ARCH_GENERIC_ATOMIC64_H

typedef	uint64_t ALIGN(sizeof(uint64_t))  mm_atomic_uint64_t;

static inline uint64_t
mm_atomic_uint64_cas(mm_atomic_uint64_t *p, uint64_t c, uint64_t v)
{
	return __sync_val_compare_and_swap(p, c, v);
}

static inline uint64_t
mm_atomic_uint64_fetch_and_set(mm_atomic_uint64_t *p, uint64_t v)
{
	return __sync_lock_test_and_set(p, v);
}

static inline uint64_t
mm_atomic_uint64_fetch_and_add(mm_atomic_uint64_t *p, uint64_t v)
{
	return __sync_fetch_and_add(p, v);
}

static inline void
mm_atomic_uint64_inc(mm_atomic_uint64_t *p)
{
	__sync_fetch_and_add(p, 1);
}

static inline int
mm_atomic_uint64_inc_and_test(mm_atomic_uint64_t *p)
{
	return __sync_add_and_fetch(p, 1) != 0;
}

static inline void
mm_atomic_uint64_dec(mm_atomic_uint64_t *p)
{
	__sync_fetch_and_sub(p, 1);
}

static inline int
mm_atomic_uint64_dec_and_test(mm_atomic_uint64_t *p)
{
	return __sync_sub_and_fetch(p, 1) != 0;
}

#endif /* BASE_ARCH_GENERIC_ATOMIC64_H */
