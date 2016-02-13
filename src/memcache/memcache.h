/*
 * memcache/memcache.h - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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
#define MEMCACHE_H

#include "common.h"
#include "base/bitset.h"

/* Enable table access combiner. */
#define ENABLE_MEMCACHE_COMBINER	0
/* Enable table access via delegate thread. */
#define ENABLE_MEMCACHE_DELEGATE	0
/* Enable table access with locking. */
#define ENABLE_MEMCACHE_LOCKING		0

/* Build with -msse4.2 to enable hash based on the SSE4.2 crc32 instruction. */
#ifndef mc_hash
# if __SSE4_2__
#  define mc_hash			mm_cksum
# else
#  define mc_hash			mm_hash_murmur3_32
# endif
#endif

/* Maximum total data size by default. */
#define MC_TABLE_VOLUME_DEFAULT		(64 * 1024 * 1024)

#define MC_COMBINER_SIZE		(1024)
#define MC_COMBINER_HANDOFF		(16)

/* Sanity checks for requested table access method. */
#if (ENABLE_MEMCACHE_COMBINER && ENABLE_MEMCACHE_DELEGATE)	\
 || (ENABLE_MEMCACHE_COMBINER && ENABLE_MEMCACHE_LOCKING)	\
 || (ENABLE_MEMCACHE_DELEGATE && ENABLE_MEMCACHE_LOCKING)
# error "Ambiguous memcache table access method."
#endif
/* Use locking by default. */
#if !ENABLE_MEMCACHE_COMBINER && !ENABLE_MEMCACHE_DELEGATE
# undef ENABLE_MEMCACHE_LOCKING
# define ENABLE_MEMCACHE_LOCKING	1
#endif

struct mm_memcache_config
{
	const char *addr;
	uint16_t port;

	size_t volume;
	mm_core_t nparts;

#if ENABLE_MEMCACHE_DELEGATE
	struct mm_bitset affinity;
#endif
};

void mm_memcache_init(const struct mm_memcache_config *config);

#endif	/* MEMCACHE_H */
