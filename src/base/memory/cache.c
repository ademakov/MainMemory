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
#include "base/memory/span.h"

#define MEMORY_VERIFY(e, msg) (likely(e) ? (void) 0 : mm_panic("panic: " __LOCATION__ ": " msg "\n"))

/*
  Chunk Ranks
  ===========

  row | msb | 0            | 1            | 2            | 3            |
 -----+-----+--------------+--------------+--------------+--------------+--------------
   0  |  3  |       8 (0)  |      10 (1)  |      12 (2)  |      14 (3)  | SMALL SIZES
   1  |  4  |      16 (4)  |      20 (5)  |      24 (6)  |      28 (7)  |
   2  |  5  |      32 (8)  |      40 (9)  |      48 (10) }      56 (11) |
   3  |  6  |      64 (12) |      80 (13) |      96 (14) |     112 (15) |
 -----+-----+--------------+--------------+--------------+-----------------------------
   4  |  7  |     128 (16) |     160 (17) |     192 (18) |     224 (19) | MEDIUM SIZES
   5  |  8  |     256 (20) |     320 (21) |     384 (22) |     448 (23) |
   6  |  9  |     512 (24) |     640 (25) |     768 (26) |     896 (27) |
   7  | 10  |    1024 (28) |    1280 (29) |    1536 (30) |    1792 (31) |
   8  | 11  |    2048 (32) |    2560 (33) |    3072 (34) |    3584 (35) |
 -----+-----+--------------+--------------+--------------+--------------+--------------
   9  | 12  |    4096 (36) |    5120 (37) |    6144 (38) |    7168 (39) | LARGE SIZES
  10  | 13  |    8192 (40) |   10240 (41) |   12288 (42) |   14336 (43) |
  11  | 14  |   16384 (44) |   20480 (45) |   24576 (46) |   28672 (47) |
  12  | 15  |   32768 (48) |   40960 (49) |   49152 (50) |   57344 (51) |
  13  | 16  |   65536 (52) |   81920 (53) |   98304 (54) |  114688 (55) |
  14  | 17  |  131072 (56) |  163840 (57) |  196608 (58) |  229376 (59) |
  15  | 18  |  262144 (60) |  327680 (61) |  393216 (62) |  458752 (63) |
  16  | 19  |  524288 (64) |  655360 (65) |  786432 (66) |  917504 (67) |
  17  | 20  | 1048576 (68) | 1310720 (69) | 1572864 (70) | 1835008 (71) |
 -----+-----+--------------+--------------+--------------+--------------+--------------
  18  | 21  | 2097152 (72)  ...                                         | HUGE SIZES


  Unit Map Encoding
  =================

  byte 0
  ------
  large chunk size index:
    value >= 0x24 --  36 -- 0 0 1 0 | 0 1 0 0
    value <= 0x47 --  71 -- 0 1 0 0 | 0 1 1 1
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
#define MM_MEMORY_SMALL_SIZES		(16u)
#define MM_MEMORY_MEDIUM_SIZES		(20u)
#define MM_MEMORY_LARGE_SIZES		(36u)
#define MM_MEMORY_BLOCK_SIZES		(MM_MEMORY_SMALL_SIZES + MM_MEMORY_MEDIUM_SIZES)
#define MM_MEMORY_CACHE_SIZES		(MM_MEMORY_BLOCK_SIZES + MM_MEMORY_LARGE_SIZES)

#define MM_MEMORY_SMALL_TO_MEDIUM	(20u)

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

#define MM_CHUNK_MAKE_SIZE(r, m)	((size_t) (4u | (m)) << (r + 1u))

#define MM_CHUNK_MAGIC_SHIFT		(18u)
#define MM_CHUNK_MAGIC_FACTOR		(1u << MM_CHUNK_MAGIC_SHIFT)
#define MM_CHUNK_MAKE_MAGIC(r, m)	((MM_CHUNK_MAGIC_FACTOR + MM_CHUNK_MAKE_SIZE(r, m) - 1u) / MM_CHUNK_MAKE_SIZE(r, m))

#define MM_CHUNK_ROW(r, _)		_(r, 0u), _(r, 1u), _(r, 2u), _(r, 3u)
#define MM_CHUNK_LOWER_ROWS(_)		\
	MM_CHUNK_ROW(0u, _),		\
	MM_CHUNK_ROW(1u, _),		\
	MM_CHUNK_ROW(2u, _),		\
	MM_CHUNK_ROW(3u, _),		\
	MM_CHUNK_ROW(4u, _),		\
	MM_CHUNK_ROW(5u, _),		\
	MM_CHUNK_ROW(6u, _),		\
	MM_CHUNK_ROW(7u, _),		\
	MM_CHUNK_ROW(8u, _)
#define MM_CHUNK_UPPER_ROWS(_)		\
	MM_CHUNK_ROW(9u, _),		\
	MM_CHUNK_ROW(10u, _),		\
	MM_CHUNK_ROW(11u, _),		\
	MM_CHUNK_ROW(12u, _),		\
	MM_CHUNK_ROW(13u, _),		\
	MM_CHUNK_ROW(14u, _),		\
	MM_CHUNK_ROW(15u, _),		\
	MM_CHUNK_ROW(16u, _),		\
	MM_CHUNK_ROW(17u, _)

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

	// The list of chunks freed remotely.
	struct mm_mpsc_queue remote_free_list;

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
	if (size-- <= 8)
		return 0;

	// Search for most significant set bit, on x86 this should translate
	// to a single BSR instruction.
	const uint32_t msb = mm_clz(size) ^ (mm_nbits(size) - 1);

	// Calcualte the rank.
	return (msb << 2u) + (size >> (msb - 2u)) - 15u;
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

	const uint8_t x = heap->units[unit];
	if (x <= MM_MEMORY_UNIT_HMASK) {
		const uint8_t y = heap->units[unit - 1];
		MEMORY_VERIFY(y >= MM_MEMORY_BASE_TAG, "bad pointer");
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

	// Initialize the remote free list.
	mm_mpsc_queue_prepare(&heap->remote_free_list);

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
		while (link != mm_list_stub(&cache->staging)) {
			struct mm_memory_heap *next = containerof(link, struct mm_memory_heap, staging_link);
			original_rank = mm_memory_find_chunk(next, required_rank);
			if (original_rank < MM_MEMORY_CACHE_SIZES) {
				next->status = MM_MEMORY_HEAP_ACTIVE;
				mm_list_delete(link);
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
				errno = ENOMEM;
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

static void
mm_memory_cache_free_chunk(struct mm_memory_heap *const heap, void *const ptr)
{
	// Identify the chunk.
	const uint32_t base = mm_memory_deduce_base(heap, ptr);
	MEMORY_VERIFY(base >= 4 && base < MM_MEMORY_UNIT_NUMBER, "bad pointer");
	const uint8_t rank = heap->units[base];
	const uint8_t mark = heap->units[base + 1];
	MEMORY_VERIFY(rank >= MM_MEMORY_BLOCK_SIZES && rank < MM_MEMORY_CACHE_SIZES, "bad pointer");

	// Handle a large chunk.
	if ((mark & ~MM_MEMORY_UNIT_LMASK) != MM_MEMORY_BASE_TAG) {
		MEMORY_VERIFY((mark & ~MM_MEMORY_UNIT_LMASK) != MM_MEMORY_NEXT_TAG, "double free");
		MEMORY_VERIFY(mark == 0, "bad pointer");
		mm_memory_free_chunk(heap, base, rank);
		return;
	}

	// Locate the block.
	const uint32_t medium_rank = rank - MM_MEMORY_MEDIUM_SIZES;
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE);
	const uint32_t shift = (((uint8_t *) ptr - (uint8_t *) block) * mm_memory_magic[medium_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	MEMORY_VERIFY(shift > 0 || shift < 32, "bad pointer");

	// Handle a medium chunk.
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0) {
		MEMORY_VERIFY((block->chunk_free & mask) == 0, "double free");
		if (block->chunk_free == 0) {
			block->next = heap->blocks[medium_rank];
			heap->blocks[medium_rank] = block;
		}
		block->chunk_free |= mask;
		return;
	}

	// Locate the inner block.
	const uint32_t small_rank = medium_rank - MM_MEMORY_SMALL_TO_MEDIUM;
	struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * mm_memory_sizes[medium_rank]);
	const uint32_t inner_shift = (((uint8_t *) ptr - (uint8_t *) inner) * mm_memory_magic[small_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	MEMORY_VERIFY(inner_shift > 0 || inner_shift < 32, "bad pointer");

	// Handle a small chunk.
	const uint32_t inner_mask = 1u << inner_shift;
	MEMORY_VERIFY((inner->free & inner_mask) == 0, "double free");
	inner->free |= inner_mask;
	if (inner->free == 0xfffe) {
		block->inner_used ^= mask;
		block->inner_free ^= mask;
		if (block->chunk_free == 0) {
			block->next = heap->blocks[medium_rank];
			heap->blocks[medium_rank] = block;
		}
		block->chunk_free |= mask;

		if (heap->blocks[small_rank] == block) {
			heap->blocks[small_rank] = block->inner_next;
		} else {
			struct mm_memory_block *prev = heap->blocks[small_rank];
			while (prev) {
				if (prev->inner_next == block) {
					prev->inner_next = block->inner_next;
					break;
				}
				prev = prev->next;
			}
		}
	} else {
		if (block->inner_free == 0) {
			block->inner_next = heap->blocks[small_rank];
			heap->blocks[small_rank] = block;
		}
		block->inner_free |= mask;
	}
}

static void
mm_memory_cache_handle_remote_free_list(struct mm_memory_heap *const heap)
{
	for (;;) {
		struct mm_mpsc_qlink *link = mm_mpsc_queue_remove(&heap->remote_free_list);
		if (link == NULL)
			break;
		mm_memory_cache_free_chunk(heap, link);
	}
}

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *const cache, struct mm_context *context)
{
	cache->context = context;

	mm_list_prepare(&cache->staging);

	cache->active = (struct mm_memory_heap *) mm_memory_span_create_heap(cache);
	MEMORY_VERIFY(cache->active, "failed to create an initial memory span");
	mm_memory_prepare_heap(cache->active);
}

void NONNULL(1)
mm_memory_cache_cleanup(struct mm_memory_cache *const cache)
{
	while (!mm_list_empty(&cache->staging)) {
		struct mm_link *link = mm_list_remove_head(&cache->staging);
		struct mm_memory_heap *heap = containerof(link, struct mm_memory_heap, staging_link);
		mm_memory_span_destroy(&heap->base);
	}
	if (cache->active != NULL) {
		mm_memory_span_destroy(&cache->active->base);
	}
}

void NONNULL(1)
mm_memory_cache_collect(struct mm_memory_cache *const cache)
{
	mm_memory_cache_handle_remote_free_list(cache->active);

	struct mm_link *link = mm_list_head(&cache->staging);
	while (link != mm_list_stub(&cache->staging)) {
		struct mm_memory_heap *heap = containerof(link, struct mm_memory_heap, staging_link);
		mm_memory_cache_handle_remote_free_list(heap);
		link = link->next;
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
		const uint32_t medium_rank = rank + MM_MEMORY_SMALL_TO_MEDIUM;
		if (block != NULL) {
			ASSERT(block->inner_free);
			const uint32_t shift = mm_ctz(block->inner_free);
			uint8_t *const inner_base = ((uint8_t *) block) + shift * mm_memory_sizes[medium_rank];

			struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) inner_base;
			ASSERT(inner->free);
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
			block = mm_memory_alloc_block(cache, medium_rank + MM_MEMORY_MEDIUM_SIZES);
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
mm_memory_cache_zalloc(struct mm_memory_cache *cache, size_t size)
{
	void *ptr = mm_memory_cache_alloc(cache, size);
	if (ptr != NULL) {
		memset(ptr, 0, size);
	}
	return ptr;
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
	// TODO: extend the unit map for large chunks with large alignment
	return (void *) ((((uintptr_t) ptr) + align_mask) & ~align_mask);
}

void * NONNULL(1) MALLOC
mm_memory_cache_calloc(struct mm_memory_cache *cache, size_t count, size_t size)
{
	size_t total;
#if MM_WORD_32BIT
	bool overflow = mm_mul_uint32(count, size, &total);
#else
	bool overflow = mm_mul_uint64(count, size, &total);
#endif
	if (overflow) {
		errno = EOVERFLOW;
		return NULL;
	}
	return mm_memory_cache_zalloc(cache, total);
}

void * NONNULL(1, 2) MALLOC
mm_memory_cache_realloc(struct mm_memory_cache *const cache, void *const ptr, const size_t size)
{
	// TODO: verify that ptr is local.

	if (size == 0) {
		mm_memory_cache_local_free(cache, ptr);
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
	mm_memory_cache_local_free(cache, ptr);

	return next_ptr;
}

void NONNULL(1, 2)
mm_memory_cache_local_free(struct mm_memory_cache *cache, void *ptr)
{
	struct mm_memory_span *span = mm_memory_span_from_ptr(ptr);
	VERIFY(cache == span->cache);

	// Handle a huge span.
	if (mm_memory_span_huge(span)) {
		mm_memory_span_destroy(span);
		return;
	}

	// Handle a chunk in a heap span.
	mm_memory_cache_free_chunk((struct mm_memory_heap *) span, ptr);
}

void NONNULL(1, 2)
mm_memory_cache_remote_free(struct mm_memory_span *const span, void *const ptr)
{
	ASSERT(span == mm_memory_span_from_ptr(ptr));

	// Handle a huge span.
	if (mm_memory_span_huge(span)) {
		mm_memory_span_destroy(span);
		return;
	}

	// Handle a chunk in a heap span.
	struct mm_mpsc_queue *const list = &((struct mm_memory_heap *) span)->remote_free_list;
	struct mm_mpsc_qlink *const link = ptr;
	mm_mpsc_qlink_prepare(link);
	mm_mpsc_queue_append(list, link);
}

size_t
mm_memory_cache_chunk_size(const void *const ptr)
{
	if (ptr == NULL)
		return 0;

	// Handle a huge chunk.
	const struct mm_memory_span *const span = mm_memory_span_from_ptr(ptr);
	if (mm_memory_span_huge(span))
		return mm_memory_span_huge_size(span);

	// Identify the chunk.
	struct mm_memory_heap *const heap = (struct mm_memory_heap *) span;
	const uint32_t base = mm_memory_deduce_base(heap, ptr);
	const uint8_t rank = heap->units[base];
	const uint8_t mark = heap->units[base + 1];
	MEMORY_VERIFY(rank >= MM_MEMORY_BLOCK_SIZES && rank < MM_MEMORY_CACHE_SIZES, "bad pointer");
	MEMORY_VERIFY(mark == 0 || (mark & ~MM_MEMORY_UNIT_LMASK) == MM_MEMORY_BASE_TAG, "bad pointer");

	// Handle a large chunk.
	if (mark == 0)
		return mm_memory_sizes[rank];

	// Locate the block.
	const uint32_t medium_rank = rank - MM_MEMORY_MEDIUM_SIZES;
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE);
	const uint32_t shift = (((uint8_t *) ptr - (uint8_t *) block) * mm_memory_magic[medium_rank]) >> MM_CHUNK_MAGIC_SHIFT;
	MEMORY_VERIFY(shift > 0 || shift < 32, "bad pointer");

	// Handle a medium chunk.
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0)
		return mm_memory_sizes[medium_rank];

	// Handle a small chunk.
	return mm_memory_sizes[medium_rank - MM_MEMORY_SMALL_TO_MEDIUM];
}
