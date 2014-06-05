/*
 * memcache.h - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#ifndef MEMCACHE_H
#define	MEMCACHE_H

#include "../common.h"
#include "../bitset.h"

#define ENABLE_MEMCACHE_LOCKS		1

#if ENABLE_DEBUG //|| 1
# define ENABLE_MEMCACHE_INDEX_DEBUG	1
#endif

#ifndef mc_hash
# define mc_hash	mm_hash_murmur3_32
#endif


struct mm_memcache_config
{
#if ENABLE_MEMCACHE_LOCKS
	mm_core_t nparts;
#else
	struct mm_bitset affinity;
#endif
};


void mm_memcache_init(const struct mm_memcache_config *config);

#endif	/* MEMCACHE_H */
