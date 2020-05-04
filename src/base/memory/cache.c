/*
 * base/memory/cache.c - MainMemory virtual memory allocation cache.
 *
 * Copyright (C) 2019-2020  Aleksey Demakov
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

#include "base/memory/cache.h"

#include "base/bitops.h"
#include "base/exit.h"
#include "base/report.h"
#include "base/memory/span.h"

/*
  Chunk Ranks
  ===========

  row | msb | 0            | 1            | 2            | 3            |
 -----+-----+--------------+--------------+--------------+--------------+--------------
   0  |  2  |       4 (0)  |       5 (1)  |       6 (2)  |       7 (3)  | SMALL SIZES
   1  |  3  |       8 (4)  |      10 (5)  |      12 (6)  |      14 (7)  |
   2  |  4  |      16 (8)  |      20 (9)  |      24 (10) |      28 (11) |
   3  |  5  |      32 (12) |      40 (13) |      48 (14) }      56 (15) |
   4  |  6  |      64 (16) |      80 (17) |      96 (18) |     112 (19) |
 -----+-----+--------------+--------------+--------------+-----------------------------
   5  |  7  |     128 (20) |     160 (21) |     192 (22) |     224 (23) | MEDIUM SIZES
   6  |  8  |     256 (24) |     320 (25) |     384 (26) |     448 (27) |
   7  |  9  |     512 (28) |     640 (29) |     768 (30) |     896 (31) |
   8  | 10  |    1024 (32) |    1280 (33) |    1536 (34) |    1792 (35) |
   9  | 11  |    2048 (36) |    2560 (37) |    3072 (38) |    3584 (39) |
 -----+-----+--------------+--------------+--------------+--------------+--------------
  10  | 12  |    4096 (40) |    5120 (41) |    6144 (42) |    7168 (43) | LARGE SIZES
  11  | 13  |    8192 (44) |   10240 (45) |   12288 (46) |   14336 (47) |
  12  | 14  |   16384 (48) |   20480 (49) |   24576 (50) |   28672 (51) |
  13  | 15  |   32768 (52) |   40960 (53) |   49152 (54) |   57344 (55) |
  14  | 16  |   65536 (56) |   81920 (57) |   98304 (58) |  114688 (59) |
  15  | 17  |  131072 (60) |  163840 (61) |  196608 (62) |  229376 (63) |
  16  | 18  |  262144 (64) |  327680 (65) |  393216 (66) |  458752 (67) |
  17  | 19  |  524288 (68) |  655360 (69) |  786432 (70) |  917504 (71) |
  18  | 20  | 1048576 (72) | 1310720 (73) | 1572864 (74) | 1835008 (75) |
 -----+-----+--------------+--------------+--------------+--------------+--------------
  19  | 21  | 2097152 (76)  ...                                         | HUGE SIZES


  Unit Map Encoding
  =================

  byte 0
  ------
  large chunk size index:
    value >= 0x28 --  40 -- 0 0 1 0 | 1 0 0 0
    value <= 0x4b --  75 -- 0 1 0 0 | 1 0 1 1
    0 x x x | x x x x

  byte 1
  ------
  for a used large chunk
    value == 0
    0 0 0 0 | 0 0 0 0

  for a block -- base of itself -- lo 6 bits
    value >= 0x80 -- 128 -- 1 0 0 0 | 0 0 0 0
    value <= 0xbf -- 191 -- 1 0 1 1 | 1 1 1 1
    1 0 x x | x x x x
  also repeated at bytes 3, 5, ...

  for a free large chunk -- base of the next free chunk -- lo 6 bits
    value >= 0xc0 -- 192 -- 1 1 0 0 | 0 0 0 0
    value <= 0xff -- 255 -- 1 1 1 1 | 1 1 1 1
    1 1 x x | x x x x

  byte 2
  ------
  for a used large chunk
    value == 0
    0 0 0 0 | 0 0 0 0

  for a block -- base of itself -- hi 5 bits
    value >= 0x00 --   0 -- 0 0 0 0 | 0 0 0 0
    value <= 0x19 --  31 -- 0 0 0 1 | 1 1 1 1
    0 0 0 x | x x x x
  also repeated at bytes 3, 5, ...

  for a free large chunk -- base of the next free chunk -- hi 5 bits
    value >= 0x00 --   0 -- 0 0 0 0 | 0 0 0 0
    value <= 0x19 --  31 -- 0 0 0 1 | 1 1 1 1
    0 0 0 x | x x x x

*/

// The number of chunk ranks.
#define MM_MEMORY_SMALL_SIZES		(20u)
#define MM_MEMORY_MEDIUM_SIZES		(20u)
#define MM_MEMORY_LARGE_SIZES		(36u)
#define MM_MEMORY_BLOCK_SIZES		(MM_MEMORY_SMALL_SIZES + MM_MEMORY_MEDIUM_SIZES)
#define MM_MEMORY_CACHE_SIZES		(MM_MEMORY_BLOCK_SIZES + MM_MEMORY_LARGE_SIZES)

// The number of chunk ranks that are allocated by halving.
#define MM_MEMORY_BUDDY_SIZES		(MM_MEMORY_LARGE_SIZES - 12u)

// Sizes of the memory map units in a heap span.
#define MM_MEMORY_HEAD_SIZE		(4096u)
#define MM_MEMORY_UNIT_SIZE		(1024u)
#define MM_MEMORY_UNIT_NUMBER		(2048u)

// Constants used for encoding of chunk ranks.
#define MM_MEMORY_UNIT_LBITS		(6u)
#define MM_MEMORY_UNIT_HBITS		(5u)
#define MM_MEMORY_UNIT_LMASK		((1u << MM_MEMORY_UNIT_LBITS) - 1u)
#define MM_MEMORY_UNIT_HMASK		((1u << MM_MEMORY_UNIT_HBITS) - 1u)

#define MM_MEMORY_BASE_TAG		(128u)
#define MM_MEMORY_NEXT_TAG		(192u)

#define MM_CHUNK_MAKE_SIZE(r, m)	((size_t) (4u | (m)) << (r))

#define MM_CHUNK_MAGIC_SHIFT		(18u)
#define MM_CHUNK_MAGIC_FACTOR		(1u << MM_CHUNK_MAGIC_SHIFT)
#define MM_CHUNK_MAKE_MAGIC(e, m)	((MM_CHUNK_MAGIC_FACTOR + MM_CHUNK_MAKE_SIZE(e, m) - 1u) / MM_CHUNK_MAKE_SIZE(e, m))

#define MM_CHUNK_ROW(e, _)		_(e, 0u), _(e, 1u), _(e, 2u), _(e, 3u)
#define MM_CHUNK_LOWER_ROWS(_)		\
	MM_CHUNK_ROW(0u, _),		\
	MM_CHUNK_ROW(1u, _),		\
	MM_CHUNK_ROW(2u, _),		\
	MM_CHUNK_ROW(3u, _),		\
	MM_CHUNK_ROW(4u, _),		\
	MM_CHUNK_ROW(5u, _),		\
	MM_CHUNK_ROW(6u, _),		\
	MM_CHUNK_ROW(7u, _),		\
	MM_CHUNK_ROW(8u, _),		\
	MM_CHUNK_ROW(9u, _)
#define MM_CHUNK_UPPER_ROWS(_)		\
	MM_CHUNK_ROW(10u, _),		\
	MM_CHUNK_ROW(11u, _),		\
	MM_CHUNK_ROW(12u, _),		\
	MM_CHUNK_ROW(13u, _),		\
	MM_CHUNK_ROW(14u, _),		\
	MM_CHUNK_ROW(15u, _),		\
	MM_CHUNK_ROW(16u, _),		\
	MM_CHUNK_ROW(17u, _),		\
	MM_CHUNK_ROW(18u, _)

typedef enum
{
	MM_MEMORY_HEAP_ACTIVE = 0,
	MM_MEMORY_HEAP_STAGING = 1
} mm_memory_heap_status_t;

// The header struct for a block of small chunks.
struct mm_memory_block_inner
{
        // A bitset of free chunks. The very first chunk is never free as
	// it is used for the header itself.
	uint32_t free;
};

// The header struct for a block of medium chunks.
struct mm_memory_block
{
	struct mm_memory_block *next;
	struct mm_memory_block *inner_next;

        // A bitset of free chunks. The very first chunk is never free as
	// it is used for the header itself.
	uint32_t chunk_free;

        // A bitset of chunks used for small chunks.
	uint32_t inner_used;
	// A bitset of chunks with some free small chunks.
	uint32_t inner_free;
};

// The header of a single heap span.
struct mm_memory_heap
{
	struct mm_memory_span base;

	struct mm_link staging_link;
	mm_memory_heap_status_t status;

	// Cached blocks and chunks.
	struct mm_memory_block *blocks[MM_MEMORY_BLOCK_SIZES];
	uint16_t chunks[MM_MEMORY_LARGE_SIZES];

	// The map of units.
	uint8_t units[MM_MEMORY_UNIT_NUMBER];
};

// Memory rank sizes.
static const uint32_t mm_memory_sizes[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_SIZE),
	MM_CHUNK_UPPER_ROWS(MM_CHUNK_MAKE_SIZE)
};

// Chunk size magic numbers.
static const uint32_t mm_memory_magic[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_MAGIC)
};

static inline uint32_t
mm_memory_get_rank(size_t size)
{
	if (size-- <= 4)
		return 0;

	// Search for most significant set bit, on x86 this should translate
	// to a single BSR instruction.
	const uint32_t msb = mm_clz(size) ^ (mm_nbits(size) - 1);

	// Calcualte the rank.
	return (msb << 2u) + (size >> (msb - 2u)) - 11u;
}

static inline uint32_t
mm_memory_decode_base(uint8_t hi, uint8_t lo)
{
	return ((uint32_t) hi << MM_MEMORY_UNIT_LBITS) | (lo & MM_MEMORY_UNIT_LMASK);
}

static uint32_t
mm_memory_deduce_base(const struct mm_memory_heap *const heap, const void *const ptr)
{
	const uint32_t offset = (uint8_t *) ptr - (uint8_t *) heap;
	const uint32_t unit = offset / MM_MEMORY_UNIT_SIZE;
	VERIFY(unit >= 4);

	const uint8_t x = heap->units[unit];
	if (x <= MM_MEMORY_UNIT_HMASK) {
		const uint8_t y = heap->units[unit - 1];
		VERIFY(y >= MM_MEMORY_BASE_TAG);
		return mm_memory_decode_base(x, y);
	}
	if (x >= MM_MEMORY_BASE_TAG) {
		const uint8_t y = heap->units[unit - 1];
		if (y <= MM_MEMORY_UNIT_HMASK) {
			return mm_memory_decode_base(y, x);
		}
		return unit - 1;
	}

	return unit;
}

static void
mm_memory_free_chunk(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t rank)
{
	ASSERT(rank >= MM_MEMORY_BLOCK_SIZES);
	const uint32_t next = heap->chunks[rank - MM_MEMORY_BLOCK_SIZES];
	heap->units[base + 1] = next | MM_MEMORY_NEXT_TAG;
	heap->units[base + 2] = next >> MM_MEMORY_UNIT_LBITS;
	heap->chunks[rank - MM_MEMORY_BLOCK_SIZES] = base;
}

static void
mm_memory_make_chunk(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t rank)
{
	heap->units[base] = rank;
	mm_memory_free_chunk(heap, base, rank);
}

static void
mm_memory_make_two(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t first, const uint32_t second)
{
	mm_memory_make_chunk(heap, base, first);
	mm_memory_make_chunk(heap, base + mm_memory_sizes[first] / MM_MEMORY_UNIT_SIZE, second);
}

static uint32_t
mm_memory_find_chunk(const struct mm_memory_heap *const heap, uint32_t rank)
{
	ASSERT(rank >= MM_MEMORY_BLOCK_SIZES && rank < MM_MEMORY_CACHE_SIZES);

	while (rank < (MM_MEMORY_BLOCK_SIZES + MM_MEMORY_BUDDY_SIZES)) {
		if (heap->chunks[rank - MM_MEMORY_BLOCK_SIZES])
			return rank;
		rank += 4;
	}
	while (rank < MM_MEMORY_CACHE_SIZES) {
		if (heap->chunks[rank - MM_MEMORY_BLOCK_SIZES])
			return rank;
		rank += 1;
	}

	return rank;
}

static void
mm_memory_split_chunk(struct mm_memory_heap *const heap, const uint32_t original_base, const uint32_t original_rank, const uint32_t required_rank)
{
	// Here the rank value is adjusted to large-only sizes.
	ASSERT(original_rank > MM_MEMORY_BLOCK_SIZES && original_rank <= MM_MEMORY_CACHE_SIZES);
	ASSERT(required_rank >= MM_MEMORY_BLOCK_SIZES && required_rank < MM_MEMORY_CACHE_SIZES);
	ASSERT(original_rank > required_rank);

	uint32_t running_base = original_base;
	uint32_t running_rank = required_rank;
	heap->units[original_base] = required_rank;
	running_base += mm_memory_sizes[required_rank] / MM_MEMORY_UNIT_SIZE;

	while (running_rank < (MM_MEMORY_BLOCK_SIZES + MM_MEMORY_BUDDY_SIZES)) {
		mm_memory_make_chunk(heap, running_base, running_rank);
		running_base += mm_memory_sizes[running_rank] / MM_MEMORY_UNIT_SIZE;

		running_rank += 4;
		if (running_rank == original_rank) {
			return;
		}
	}

	const uint32_t running_distance = original_rank - running_rank;
	switch (running_distance) {
	case 1:
		mm_memory_make_chunk(heap, running_base, (running_rank & ~3u) - 8);
		break;
	case 2:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_make_chunk(heap, running_base, running_rank - 4);
			break;
		case 1: case 3:
			mm_memory_make_chunk(heap, running_base, running_rank - 5);
			break;
		case 2:
			mm_memory_make_chunk(heap, running_base, running_rank - 6);
			break;
		}
		break;
	case 3:
		switch ((running_rank & 3)) {
		case 0: case 2: case 3:
			mm_memory_make_chunk(heap, running_base, running_rank - 2);
			break;
		case 1:
			mm_memory_make_chunk(heap, running_base, running_rank - 3);
			break;
		}
		break;
	case 4:
		mm_memory_make_chunk(heap, running_base, running_rank);
		break;
	case 5:
		switch ((running_rank & 3)) {
		case 0:	case 1: case 2:
			mm_memory_make_chunk(heap, running_base, running_rank + 2);
			break;
		case 3:
			mm_memory_make_two(heap, running_base, running_rank - 3, running_rank - 2);
			break;
		}
		break;
	case 6:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_make_chunk(heap, running_base, running_rank + 4);
			break;
		case 1:
			mm_memory_make_two(heap, running_base, running_rank - 1, running_rank);
			break;
		case 2:
			mm_memory_make_chunk(heap, running_base, running_rank + 3);
			break;
		case 3:
			mm_memory_make_two(heap, running_base, running_rank - 2, running_rank + 1);
			break;
		}
		break;
	case 7:
		switch ((running_rank & 3)) {
		case 0: case 2:
			mm_memory_make_chunk(heap, running_base, running_rank + 5);
			break;
		case 1:
			mm_memory_make_two(heap, running_base, running_rank - 1, running_rank + 2);
			break;
		case 3:
			mm_memory_make_two(heap, running_base, running_rank - 2, running_rank + 3);
			break;
		}
		break;
	case 8:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_make_chunk(heap, running_base, running_rank + 6);
			break;
		case 1:	case 2:
			mm_memory_make_two(heap, running_base, running_rank + 2, running_rank + 3);
			break;
		case 3:
			mm_memory_make_two(heap, running_base, running_rank - 2, running_rank + 5);
			break;
		}
		break;
	case 9:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_make_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 4);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 11)) {
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 6);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 10)) {
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 8, MM_MEMORY_CACHE_SIZES - 5);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 9));
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 11, MM_MEMORY_CACHE_SIZES - 3);
		}
		break;
	case 10:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_make_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 3);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 11)) {
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 4);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 10));
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 7, MM_MEMORY_CACHE_SIZES - 4);
		}
		break;
	case 11:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_make_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 2);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 11));
			mm_memory_make_two(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 3);
		}
		break;
	case 12:
		ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 12));
		mm_memory_make_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 1);
		break;
	default:
		ABORT();
	}
}


static void
mm_memory_prepare_heap(struct mm_memory_heap *const heap)
{
	// As the heap comes after a fresh mmap() call there is no need
	// to zero it out manually.
#if 0
	heap->status = MM_MEMORY_HEAP_ACTIVE;

	for (uint32_t i = 0; i < MM_MEMORY_BLOCK_SIZES; i++)
		heap->blocks[i] = NULL;
	for (uint32_t i = 0; i < MM_MEMORY_LARGE_SIZES; i++)
		heap->chunks[i] = 0;

	memset(heap->units, 0, sizeof heap->units);
#endif

	// The initial heap layout takes out the very first 4KiB chunk
	// from the heap. It is used up for the very heap header that is
	// initialized here.
	heap->units[0] = MM_MEMORY_BLOCK_SIZES;
	mm_memory_split_chunk(heap, 0, MM_MEMORY_CACHE_SIZES, MM_MEMORY_BLOCK_SIZES);
}

static void *
mm_memory_alloc_large(struct mm_memory_cache *const cache, const uint32_t required_rank, bool block)
{
	ASSERT(required_rank >= MM_MEMORY_BLOCK_SIZES && required_rank < MM_MEMORY_CACHE_SIZES);

	struct mm_memory_heap *heap = cache->active;
	uint32_t original_rank = mm_memory_find_chunk(heap, required_rank);
	if (original_rank >= MM_MEMORY_CACHE_SIZES) {
		// TODO: Try to coalesce freed memory in the active span.

		heap = NULL;

		// Try to find a suitable span in the staging list.
		struct mm_link *link = mm_list_head(&cache->staging);
		while (link != mm_list_sentinel(&cache->staging)) {
			struct mm_memory_heap *next = containerof(link, struct mm_memory_heap, staging_link);
			original_rank = mm_memory_find_chunk(next, required_rank);
			if (original_rank < MM_MEMORY_CACHE_SIZES){
				next->status = MM_MEMORY_HEAP_ACTIVE;
				heap = next;
				break;
			}
			link = link->next;
		}

		// Allocate a new span if none found.
		if (heap == NULL) {
			heap = (struct mm_memory_heap *) mm_memory_span_create_heap(cache);
			if (heap == NULL) {
				// Out of memory.
				return NULL;
			}

			mm_memory_prepare_heap(heap);
			original_rank = mm_memory_find_chunk(heap, required_rank);
			ASSERT(original_rank < MM_MEMORY_CACHE_SIZES);
		}

		cache->active->status = MM_MEMORY_HEAP_STAGING;
		mm_list_insert(&cache->staging, &cache->active->staging_link);
		cache->active = heap;
	}

	// Remove the chunk from the free list.
	const uint32_t index = original_rank - MM_MEMORY_BLOCK_SIZES;
	const uint32_t base = heap->chunks[index];
	heap->chunks[index] = mm_memory_decode_base(heap->units[base + 2], heap->units[base + 1]);

	// If the chunk is bigger than required then split.
	if (original_rank != required_rank) {
		mm_memory_split_chunk(heap, base, original_rank, required_rank);
	}

	if (!block) {
		// The large chunk is to be used as such.
		heap->units[base + 1] = 0;
		heap->units[base + 2] = 0;
	} else {
		// The large chunk is to be used as a block. Fill the unit map.
		const uint8_t bytes[4] = {
			(base & MM_MEMORY_UNIT_LMASK) | MM_MEMORY_BASE_TAG,
			base >> MM_MEMORY_UNIT_LBITS,
			(base & MM_MEMORY_UNIT_LMASK) | MM_MEMORY_BASE_TAG,
			base >> MM_MEMORY_UNIT_LBITS
		};

		uint8_t *const map = &heap->units[base + 1];
		const uint32_t end = mm_memory_sizes[required_rank] / MM_MEMORY_UNIT_SIZE - 1;
		const uint32_t loop_end = end & ~3u;
		const uint32_t tail = end & 3u;

		uint32_t i = 0;
		while (i < loop_end) {
			memcpy(&map[i], bytes, 4);
			i += 4;
		}
		if ((tail & 2) != 0) {
			memcpy(&map[i], bytes, 2);
			i += 2;
		}
		if ((tail & 1) != 0) {
			map[i] = bytes[0];
		}
	}

	return (uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE;
}

static struct mm_memory_block *
mm_memory_alloc_block(struct mm_memory_cache *const cache, const uint32_t rank)
{
	// Allocate a large chunk.
	struct mm_memory_block *const block = mm_memory_alloc_large(cache, rank, true);
	if (unlikely(block == NULL))
		return NULL;

	// Set it up as a block.
	block->next = NULL;
	block->inner_next = NULL;
	block->inner_used = 0;
	block->inner_free = 0;

	// One slot is used for 'struct mm_memory_block', another will be used
	// for allocation right away.
	block->chunk_free = 0xfffc;

	// Cache the block for futher use.
	ASSERT(cache->active->blocks[rank - MM_MEMORY_MEDIUM_SIZES] == NULL);
	cache->active->blocks[rank - MM_MEMORY_MEDIUM_SIZES] = block;

	return block;
}

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *const cache, struct mm_context *context)
{
	cache->context = context;

	mm_list_prepare(&cache->staging);
	mm_list_prepare(&cache->spans);

	cache->active = (struct mm_memory_heap *) mm_memory_span_create_heap(cache);
	if (cache->active == NULL)
		mm_panic("panic: failed to create an initial memory span\n");
	mm_memory_prepare_heap(cache->active);
}

void NONNULL(1)
mm_memory_cache_cleanup(struct mm_memory_cache *const cache)
{
	while (!mm_list_empty(&cache->spans)) {
		struct mm_link *link = mm_list_head(&cache->spans);
		struct mm_memory_span *span = containerof(link, struct mm_memory_span, cache_link);
		mm_memory_span_destroy(span);
	}
}

void * NONNULL(1) MALLOC
mm_memory_cache_alloc(struct mm_memory_cache *const cache, const size_t size)
{
	const uint32_t rank = mm_memory_get_rank(size);
	if (rank >= MM_MEMORY_BLOCK_SIZES) {
		// Handle a huge or large size.

		if (rank >= MM_MEMORY_CACHE_SIZES) {
			struct mm_memory_span *span = mm_memory_span_create_huge(cache, size);
			if (unlikely(span == NULL))
				return NULL;
			return mm_memory_span_huge_data(span);
		}
		return mm_memory_alloc_large(cache, rank, false);

	} else if (rank >= MM_MEMORY_SMALL_SIZES) {
		// Handle a medium size.

		// Use a cached block if any.
		struct mm_memory_block *block = cache->active->blocks[rank];
		if (block != NULL) {
			ASSERT(block->chunk_free);
			const uint32_t shift = mm_ctz(block->chunk_free);
			block->chunk_free ^= (1u << shift);
			if (block->chunk_free == 0) {
				// Remove a fully used block.
				cache->active->blocks[rank] = block->next;
			}
			return (uint8_t *) block + shift * mm_memory_sizes[rank];
		}

		// Allocate a new block.
		block = mm_memory_alloc_block(cache, rank + MM_MEMORY_MEDIUM_SIZES);
		if (unlikely(block == NULL))
			return NULL;
		return (uint8_t *) block + mm_memory_sizes[rank];

	} else {
		// Handle a small size.

		// Use a cached inner block if any.
		struct mm_memory_block *block = cache->active->blocks[rank];
		const uint32_t medium_rank = rank + MM_MEMORY_SMALL_SIZES;
		if (block != NULL) {
			ASSERT(block->inner_free);
			const uint32_t shift = mm_ctz(block->inner_free);
			uint8_t *const inner_base = ((uint8_t *) block) + shift * mm_memory_sizes[medium_rank];

			struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) inner_base;
			ASSERT(inner->free != 0);
			const uint32_t inner_shift = mm_ctz(inner->free);
			inner->free ^= (1u << inner_shift);
			if (inner->free == 0) {
				block->inner_free ^= (1u << shift);
				if (block->inner_free == 0) {
					// Remove a fully used inner block.
					cache->active->blocks[rank] = block->inner_next;
				}
			}

			return inner_base + inner_shift * mm_memory_sizes[rank];
		}

		// Allocate a medium chunk and use it as an inner block.
		uint8_t *inner_base;
		block = cache->active->blocks[medium_rank];
		if (block != NULL) {
			// Use a cached block.
			ASSERT(block->chunk_free);
			const uint32_t shift = mm_ctz(block->chunk_free);
			// Mark the medium chunk as an inner block.
			block->inner_used |= (1u << shift);
			block->inner_free |= (1u << shift);
			block->chunk_free ^= (1u << shift);
			if (block->chunk_free == 0) {
				// Remove a fully used block.
				cache->active->blocks[medium_rank] = block->next;
			}
			inner_base = (uint8_t *) block + shift * mm_memory_sizes[medium_rank];
		} else {
			// Allocate a new block.
			block = mm_memory_alloc_block(cache, rank + MM_MEMORY_BLOCK_SIZES);
			if (unlikely(block == NULL))
				return NULL;
			// Mark the medium chunk as an inner block.
			block->inner_used |= 2;
			block->inner_free |= 2;
			inner_base = (uint8_t *) block + mm_memory_sizes[medium_rank];
		}

		// Mark the remaining small chunks as free.
		((struct mm_memory_block_inner *) inner_base)->free = 0xfffc;

		// Cache the block for futher use.
		ASSERT(cache->active->blocks[rank] == NULL);
		cache->active->blocks[rank] = block;
		block->inner_next = NULL;

		return inner_base + mm_memory_sizes[rank];
	}
}

void * NONNULL(1) MALLOC
mm_memory_cache_aligned_alloc(struct mm_memory_cache *cache, size_t align, size_t size)
{
	if (!mm_is_pow2z(align)) {
		errno = EINVAL;
		return NULL;
	}
	if (align > MM_MEMORY_SPAN_ALIGNMENT / 2) {
		errno = EINVAL;
		return NULL;
	}

	// Handle naturally aligned sizes.
	if (align <= MM_MEMORY_UNIT_SIZE) {
		const uint32_t rank = mm_memory_get_rank(size);

		size_t alloc_align;
		if (rank >= MM_MEMORY_BLOCK_SIZES) {
			if (rank >= MM_MEMORY_CACHE_SIZES)
				alloc_align = MM_CACHELINE;
			else
				alloc_align = MM_MEMORY_UNIT_SIZE;
		} else {
			switch ((rank & 3)) {
			case 0:
				alloc_align = mm_memory_sizes[rank];
				break;
			case 1:
				alloc_align = mm_memory_sizes[rank - 1] / 4;
				break;
			case 2:
				alloc_align = mm_memory_sizes[rank - 2] / 2;
				break;
			case 3:
				alloc_align = mm_memory_sizes[rank - 3] / 4;
				break;
			}
		}

		if (alloc_align >= align) {
			return mm_memory_cache_alloc(cache, size);
		}
	}

	const size_t align_mask = align - 1;
	void *const ptr = mm_memory_cache_alloc(cache, size + align_mask);
	return (void *) ((((uintptr_t) ptr) + align_mask) & ~align_mask);
}

void * NONNULL(1) MALLOC
mm_memory_cache_calloc(struct mm_memory_cache *cache, size_t count, size_t size)
{
	// TODO: check for aithmetic overflow.
	size_t total_size = count * size;

	void *ptr = mm_memory_cache_alloc(cache, total_size);
	if (ptr == NULL)
		return NULL;
	memset(ptr, 0, total_size);

	return ptr;
}

void * NONNULL(1) MALLOC
mm_memory_cache_realloc(struct mm_memory_cache *const cache, void *const ptr, const size_t size)
{
	if (size == 0) {
		mm_memory_cache_free(cache, ptr);
		return NULL;
	}

	const size_t prev_size = mm_memory_cache_chunk_size(ptr);
	if (prev_size >= size) {
		if (prev_size == size)
			return ptr;

		const uint32_t rank = mm_memory_get_rank(size);
		const uint32_t prev_rank = mm_memory_get_rank(prev_size);
		if (prev_rank == rank)
			return ptr;
	}

	void *next_ptr = mm_memory_cache_alloc(cache, size);
	if (next_ptr == NULL)
		return NULL;

	memcpy(next_ptr, ptr, min(prev_size, size));
	mm_memory_cache_free(cache, ptr);

	return next_ptr;
}

void NONNULL(1)
mm_memory_cache_free(struct mm_memory_cache *const cache, void *const ptr)
{
	if (ptr == NULL)
		return;

	struct mm_memory_span *const span = mm_memory_span_from_ptr(ptr);
	VERIFY(cache == span->cache);

	// Handle a huge chunk.
	if (mm_memory_span_huge(span)) {
		mm_list_delete(&span->cache_link);
		mm_memory_span_destroy(span);
		return;
	}

	// Identify the chunk.
	struct mm_memory_heap *const heap = (struct mm_memory_heap *) span;
	const uint32_t base = mm_memory_deduce_base(heap, ptr);
	const uint8_t rank = heap->units[base];
	const uint8_t mark = heap->units[base + 1];
	if (rank < MM_MEMORY_BLOCK_SIZES || rank > MM_MEMORY_CACHE_SIZES)
		mm_panic("panic: bad memory chunk\n");
	if (mark != 0 && (mark & ~MM_MEMORY_UNIT_LMASK) != MM_MEMORY_BASE_TAG)
		mm_panic("panic: bad memory chunk\n");

	// Handle a large chunk.
	if (mark == 0) {
		mm_memory_free_chunk(heap, base, rank);
		return;
	}

	// Locate the block.
	const uint32_t medium_rank = rank - MM_MEMORY_MEDIUM_SIZES;
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE);
	const uint32_t shift = (((uint8_t *) ptr - (uint8_t *) block) * mm_memory_magic[medium_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	if (shift == 0 || shift > 31)
		mm_panic("panic: bad memory chunk\n");

	// Handle a medium chunk.
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0) {
		if (block->chunk_free == 0) {
			block->next = heap->blocks[medium_rank];
			heap->blocks[medium_rank] = block;
		} else if ((block->chunk_free & mask) != 0) {
			mm_panic("panic: double free\n");
		}
		block->chunk_free |= mask;
		return;
	}

	// Locate the inner block.
	const uint32_t small_rank = rank - MM_MEMORY_BLOCK_SIZES;
	struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * mm_memory_sizes[medium_rank]);
	const uint32_t inner_shift = (((uint8_t *) ptr - (uint8_t *) inner) * mm_memory_magic[small_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	if (inner_shift == 0 || inner_shift > 31)
		mm_panic("panic: bad memory chunk\n");

	// Handle a small chunk.
	const uint32_t inner_mask = 1u << inner_shift;
	if ((inner->free & inner_mask) != 0)
		mm_panic("panic: double free\n");
	inner->free |= inner_mask;
	if (inner->free == 0xfffe) {
		block->inner_used ^= mask;
		block->inner_free ^= mask;
		if (block->chunk_free == 0) {
			block->next = heap->blocks[medium_rank];
			heap->blocks[medium_rank] = block;
		}
		block->chunk_free |= mask;
	} else {
		if (block->inner_free == 0) {
			block->inner_next = heap->blocks[small_rank];
			heap->blocks[small_rank] = block;
		}
		block->inner_free |= mask;
	}
}

size_t
mm_memory_cache_chunk_size(const void *const ptr)
{
	if (ptr == NULL)
		return 0;

	const struct mm_memory_span *const span = mm_memory_span_from_ptr(ptr);
	if (span == NULL)
		mm_panic("panic: bad memory chunk\n");

	// Handle a huge chunk.
	if (mm_memory_span_huge(span))
		return mm_memory_span_huge_size(span);

	// Identify the chunk.
	struct mm_memory_heap *const heap = (struct mm_memory_heap *) span;
	const uint32_t base = mm_memory_deduce_base(heap, ptr);
	const uint8_t rank = heap->units[base];
	const uint8_t mark = heap->units[base + 1];
	if (rank < MM_MEMORY_BLOCK_SIZES || rank > MM_MEMORY_CACHE_SIZES)
		mm_panic("panic: bad memory chunk\n");
	if (mark != 0 && (mark & ~MM_MEMORY_UNIT_LMASK) != MM_MEMORY_BASE_TAG)
		mm_panic("panic: bad memory chunk\n");

	// Handle a large chunk.
	if (mark == 0)
		return mm_memory_sizes[rank];

	// Locate the block.
	const uint32_t medium_rank = rank - MM_MEMORY_MEDIUM_SIZES;
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE);
	const uint32_t shift = (((uint8_t *) ptr - (uint8_t *) block) * mm_memory_magic[medium_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	if (shift == 0 || shift > 31)
		mm_panic("panic: bad memory chunk\n");

	// Handle a medium chunk.
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0)
		return mm_memory_sizes[medium_rank];

	// Handle a small chunk.
	const uint32_t small_rank = rank - MM_MEMORY_BLOCK_SIZES;
	return mm_memory_sizes[small_rank];
}
