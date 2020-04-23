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
#include "base/report.h"
#include "base/memory/span.h"

/*
  Chunk Sizes
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
  19  | 21  | 2097152 (76)                                              | HUGE SIZES


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

/* The number of size classes in cache. */
#define MM_MEMORY_SMALL_SIZES		(20u)
#define MM_MEMORY_MEDIUM_SIZES		(20u)
#define MM_MEMORY_LARGE_SIZES		(36u)
#define MM_MEMORY_BLOCK_SIZES		(MM_MEMORY_SMALL_SIZES + MM_MEMORY_MEDIUM_SIZES)
#define MM_MEMORY_CACHE_SIZES		(MM_MEMORY_BLOCK_SIZES + MM_MEMORY_LARGE_SIZES)

#define MM_MEMORY_BUDDY_SIZES		(MM_MEMORY_LARGE_SIZES - 12u)

#define MM_MEMORY_HEAD_SIZE		(4096u)
#define MM_MEMORY_UNIT_SIZE		(1024u)
#define MM_MEMORY_UNIT_NUMBER		(2048u)

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

// A set of memory chunks within a larger memory block.
typedef uint32_t mm_memory_chunks_t;

// A header for a batch of small-sized chunks.
struct mm_memory_block_inner
{
	mm_memory_chunks_t free;
};

// A header for a batch of medium-sized chunks.
struct mm_memory_block
{
	uint32_t size_index;

	uint32_t chunk_size;
	uint32_t chunk_magic;
	mm_memory_chunks_t chunk_free;

	uint32_t inner_size;
	uint32_t inner_magic;
	mm_memory_chunks_t inner_used;
	mm_memory_chunks_t inner_free;

	struct mm_link block_peers;
	struct mm_link inner_peers;
};

struct mm_memory_heap
{
	struct mm_memory_span base;

	// Cached blocks and chunks.
	struct mm_memory_block *blocks[MM_MEMORY_BLOCK_SIZES];
	uint16_t chunks[MM_MEMORY_LARGE_SIZES];

	// The map of units.
	uint8_t units[MM_MEMORY_UNIT_NUMBER];
};

// Memory rank sizes.
static const uint32_t mm_memory_rank_sizes[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_SIZE),
	MM_CHUNK_UPPER_ROWS(MM_CHUNK_MAKE_SIZE)
};

// Chunk size magic numbers.
static const uint32_t mm_memory_chunk_magic[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_MAGIC)
};

static inline uint32_t
mm_memory_encode_size(size_t size)
{
	if (size-- <= 4)
		return 0;

	// Search for most significant set bit, on x86 this should
	// translate to a single BSR instruction.
	const uint32_t msb = mm_clz(size) ^ (mm_nbits(size) - 1);
	// Calcualte the size class index.
	return (msb << 2u) + (size >> (msb - 2u)) - 11u;
}

static inline size_t
mm_memory_decode_size(size_t code)
{
	return MM_CHUNK_MAKE_SIZE(code >> 2u, code & 3u);
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
mm_memory_add_chunk(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t rank)
{
	ASSERT(rank >= MM_MEMORY_BLOCK_SIZES);
	const uint32_t next = heap->chunks[rank - MM_MEMORY_BLOCK_SIZES];
	heap->units[base + 1] = next | MM_MEMORY_NEXT_TAG;
	heap->units[base + 2] = next >> MM_MEMORY_UNIT_LBITS;
	heap->chunks[rank - MM_MEMORY_BLOCK_SIZES] = base;
}

static void
mm_memory_set_chunk(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t rank)
{
	heap->units[base] = rank;
	mm_memory_add_chunk(heap, base, rank);
}

static void
mm_memory_set_chunks(struct mm_memory_heap *const heap, const uint32_t base, const uint32_t first, const uint32_t second)
{
	mm_memory_set_chunk(heap, base, first);
	mm_memory_set_chunk(heap, base + mm_memory_rank_sizes[first] / MM_MEMORY_UNIT_SIZE, second);
}

static uint32_t
mm_memory_find_chunk(const struct mm_memory_heap *const heap, uint32_t rank)
{
	ASSERT(rank >= MM_MEMORY_BLOCK_SIZES && rank < MM_MEMORY_CACHE_SIZES);
	rank -= MM_MEMORY_BLOCK_SIZES;

	while (rank < MM_MEMORY_BUDDY_SIZES) {
		if (heap->chunks[rank])
			return rank;
		rank += 4;
	}
	while (rank < MM_MEMORY_LARGE_SIZES) {
		if (heap->chunks[rank])
			return rank;
		rank += 1;
	}

	return rank + MM_MEMORY_BLOCK_SIZES;
}

static void
mm_memory_split_chunk(struct mm_memory_heap *const heap, const uint32_t original_base, const uint32_t original_rank, const uint32_t required_rank)
{
	// Here the rank value is adjusted to large-only sizes.
	ASSERT(original_rank >= MM_MEMORY_BLOCK_SIZES && original_rank <= MM_MEMORY_CACHE_SIZES);
	ASSERT(required_rank >= MM_MEMORY_BLOCK_SIZES && required_rank < MM_MEMORY_CACHE_SIZES);
	ASSERT(original_rank > required_rank);

	uint32_t running_base = original_base;
	uint32_t running_rank = required_rank;
	heap->units[original_base] = required_rank;
	running_base += mm_memory_rank_sizes[required_rank] / MM_MEMORY_UNIT_SIZE;

	while (running_rank < (MM_MEMORY_BLOCK_SIZES + MM_MEMORY_BUDDY_SIZES)) {
		mm_memory_set_chunk(heap, running_base, running_rank);

		running_rank += 4;
		if (running_rank == original_rank) {
			return;
		}

		running_base += mm_memory_rank_sizes[running_rank] / MM_MEMORY_UNIT_SIZE;
	}

	const uint32_t running_distance = original_rank - running_rank;
	switch (running_distance) {
	case 1:
		mm_memory_set_chunk(heap, running_base, (running_rank & ~3u) - 8);
		break;
	case 2:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_set_chunk(heap, running_base, running_rank - 4);
			break;
		case 1: case 3:
			mm_memory_set_chunk(heap, running_base, running_rank - 5);
			break;
		case 2:
			mm_memory_set_chunk(heap, running_base, running_rank - 6);
			break;
		}
		break;
	case 3:
		switch ((running_rank & 3)) {
		case 0: case 2: case 3:
			mm_memory_set_chunk(heap, running_base, running_rank - 2);
			break;
		case 1:
			mm_memory_set_chunk(heap, running_base, running_rank - 3);
			break;
		}
		break;
	case 4:
		mm_memory_set_chunk(heap, running_base, running_rank);
		break;
	case 5:
		switch ((running_rank & 3)) {
		case 0:	case 1: case 2:
			mm_memory_set_chunk(heap, running_base, running_rank + 2);
			break;
		case 3:
			mm_memory_set_chunks(heap, running_base, running_rank - 3, running_rank - 2);
			break;
		}
		break;
	case 6:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_set_chunk(heap, running_base, running_rank + 4);
			break;
		case 1:
			mm_memory_set_chunks(heap, running_base, running_rank - 1, running_rank);
			break;
		case 2:
			mm_memory_set_chunk(heap, running_base, running_rank + 3);
			break;
		case 3:
			mm_memory_set_chunks(heap, running_base, running_rank - 2, running_rank + 1);
			break;
		}
		break;
	case 7:
		switch ((running_rank & 3)) {
		case 0: case 2:
			mm_memory_set_chunk(heap, running_base, running_rank + 5);
			break;
		case 1:
			mm_memory_set_chunks(heap, running_base, running_rank - 1, running_rank + 2);
			break;
		case 3:
			mm_memory_set_chunks(heap, running_base, running_rank - 2, running_rank + 3);
			break;
		}
		break;
	case 8:
		switch ((running_rank & 3)) {
		case 0:
			mm_memory_set_chunk(heap, running_base, running_rank + 6);
			break;
		case 1:	case 2:
			mm_memory_set_chunks(heap, running_base, running_rank + 2, running_rank + 3);
			break;
		case 3:
			mm_memory_set_chunks(heap, running_base, running_rank - 2, running_rank + 5);
			break;
		}
		break;
	case 9:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_set_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 4);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 11)) {
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 6);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 10)) {
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 8, MM_MEMORY_CACHE_SIZES - 5);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 9));
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 11, MM_MEMORY_CACHE_SIZES - 3);
		}
		break;
	case 10:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_set_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 3);
		} else if (running_rank == (MM_MEMORY_CACHE_SIZES - 11)) {
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 4);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 10));
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 7, MM_MEMORY_CACHE_SIZES - 4);
		}
		break;
	case 11:
		if (running_rank == (MM_MEMORY_CACHE_SIZES - 12)) {
			mm_memory_set_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 2);
		} else {
			ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 11));
			mm_memory_set_chunks(heap, running_base, MM_MEMORY_CACHE_SIZES - 9, MM_MEMORY_CACHE_SIZES - 3);
		}
		break;
	case 12:
		ASSERT(running_rank == (MM_MEMORY_CACHE_SIZES - 12));
		mm_memory_set_chunk(heap, running_base, MM_MEMORY_CACHE_SIZES - 1);
		break;
	default:
		ABORT();
	}
}

static void
mm_memory_prepare_heap(struct mm_memory_heap *const heap)
{
	for (uint32_t i = 0; i < MM_MEMORY_BLOCK_SIZES; i++)
		heap->blocks[i] = NULL;
	for (uint32_t i = 0; i < MM_MEMORY_LARGE_SIZES; i++)
		heap->chunks[i] = 0;

	memset(heap->units, 0, sizeof heap->units);
	mm_memory_split_chunk(heap, 0, MM_MEMORY_CACHE_SIZES, MM_MEMORY_BLOCK_SIZES);
}

static void *
mm_memory_alloc_chunk(struct mm_memory_cache *const cache, const uint32_t required_rank)
{
	struct mm_memory_heap *heap = cache->active;
	uint32_t original_rank = mm_memory_find_chunk(heap, required_rank);
	if (original_rank >= MM_MEMORY_CACHE_SIZES) {
		heap = (struct mm_memory_heap *) mm_memory_span_create_heap(cache);
		if (heap == NULL)
			mm_fatal(errno, "failed to create an additional memory span");
		mm_memory_prepare_heap(heap);

		cache->active->base.type_and_size = MM_MEMORY_SPAN_STAGING_HEAP;
		mm_list_append(&cache->heap, &cache->active->base.cache_link);
		cache->active = heap;

		original_rank = mm_memory_find_chunk(heap, required_rank);
		ASSERT(original_rank < MM_MEMORY_CACHE_SIZES);
	}

	const uint32_t index = original_rank - MM_MEMORY_BLOCK_SIZES;
	const uint32_t base = heap->chunks[index];
	heap->chunks[index] = mm_memory_decode_base(heap->units[base + 2], heap->units[base + 1]);
	heap->units[base + 1] = 0;
	heap->units[base + 2] = 0;

	if (original_rank != required_rank)
		mm_memory_split_chunk(heap, base, original_rank, required_rank);

	return ((uint8_t *) heap) + base * MM_MEMORY_UNIT_SIZE;
}

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *cache, struct mm_context *context)
{
	ENTER();

	cache->context = context;

	mm_list_prepare(&cache->heap);
	mm_list_prepare(&cache->huge);

	cache->active = (struct mm_memory_heap *) mm_memory_span_create_heap(cache);
	if (cache->active == NULL)
		mm_fatal(errno, "failed to create an initial memory span");
	mm_memory_prepare_heap(cache->active);

	LEAVE();
}

void NONNULL(1)
mm_memory_cache_cleanup(struct mm_memory_cache *cache)
{
	ENTER();

	while (!mm_list_empty(&cache->huge)) {
		struct mm_link *link = mm_list_remove_head(&cache->huge);
		struct mm_memory_span *span = containerof(link, struct mm_memory_span, cache_link);
		mm_memory_span_destroy(span);
	}
	while (!mm_list_empty(&cache->heap)) {
		struct mm_link *link = mm_list_remove_head(&cache->heap);
		struct mm_memory_span *span = containerof(link, struct mm_memory_span, cache_link);
		mm_memory_span_destroy(span);
	}
	if (cache->active != NULL) {
		mm_memory_span_destroy(&cache->active->base);
		cache->active = NULL;
	}

	LEAVE();
}

void * NONNULL(1)
mm_memory_cache_alloc(struct mm_memory_cache *const cache, const size_t size)
{
	ENTER();
	void *chunk = NULL;

	const uint32_t rank = mm_memory_encode_size(size);
	if (rank >= MM_MEMORY_BLOCK_SIZES) {
		if (rank >= MM_MEMORY_CACHE_SIZES) {
			struct mm_memory_span *span = mm_memory_span_create_huge(cache, size);
			chunk = ((uint8_t *) span) + sizeof(struct mm_memory_span);
		} else {
			chunk = mm_memory_alloc_chunk(cache, rank);
		}

	} else if (rank >= MM_MEMORY_SMALL_SIZES) {
		struct mm_memory_heap *heap = cache->active;
		struct mm_memory_block *block = heap->blocks[rank];
		if (block == NULL || block->chunk_free == 0) {
			// TODO
			goto leave;
		}

		const uint32_t shift = mm_ctz(block->chunk_free);
		block->chunk_free ^= (1u << shift);
		chunk = (uint8_t *) block + shift * block->chunk_size;

	} else {
		struct mm_memory_heap *heap = cache->active;
		struct mm_memory_block *block = heap->blocks[rank];
		if (block == NULL || block->inner_free == 0) {
			if (block == NULL || block->chunk_free == 0) {
				// TODO
				goto leave;
			}

			const uint32_t shift = mm_ctz(block->chunk_free);
			struct mm_memory_block_inner *inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * block->chunk_size);
			inner->free = 0xfffc;

			block->chunk_free ^= (1u << shift);
			block->inner_used |= (1u << shift);
			block->inner_free |= (1u << shift);

			chunk = (uint8_t *) inner + block->inner_size;;
		} else {
			const uint32_t shift = mm_ctz(block->inner_free);
			struct mm_memory_block_inner *inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * block->chunk_size);
			ASSERT(inner->free != 0);

			const uint32_t inner_shift = mm_ctz(inner->free);
			inner->free ^= (1u << inner_shift);
			if (inner->free == 0)
				block->inner_free ^= (1u << shift);
			chunk = (uint8_t *) inner + inner_shift * block->inner_size;
		}
	}

leave:
	LEAVE();
	return chunk;
}

void NONNULL(1)
mm_memory_cache_free(struct mm_memory_cache *const cache, void *const ptr)
{
	struct mm_memory_span *const span = mm_memory_span_from_ptr(ptr);
	VERIFY(cache == span->cache);

	if (mm_memory_span_huge(span)) {
		mm_list_delete(&span->cache_link);
		mm_memory_span_destroy(span);
		return;
	}

	struct mm_memory_heap *const heap = (struct mm_memory_heap *) span;
	const uint32_t base = mm_memory_deduce_base(heap, ptr);
	VERIFY(heap->units[base] >= MM_MEMORY_BLOCK_SIZES);
	VERIFY(heap->units[base] < MM_MEMORY_CACHE_SIZES);

	const uint8_t rank = heap->units[base];
	const uint8_t mark = heap->units[base + 1];
	if (mark == 0) {
		mm_memory_add_chunk(heap, base, rank);
		return;
	}

	VERIFY(mark == ((mark & MM_MEMORY_UNIT_LMASK) | MM_MEMORY_BASE_TAG));
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + base * MM_MEMORY_UNIT_SIZE);
	const uint32_t offset = (uint8_t *) ptr - (uint8_t *) block;
	VERIFY(offset >= block->chunk_size);
	const uint32_t shift = (offset * block->chunk_magic) >> MM_CHUNK_MAGIC_SHIFT;
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0) {
		VERIFY((block->chunk_free & mask) == 0);
		block->chunk_free |= mask;
		return;
	}

	struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * block->chunk_size);
	const uint32_t inner_offset = (uint8_t *) ptr - (uint8_t *) inner;
	VERIFY(inner_offset >= block->inner_size);
	const uint32_t inner_shift = (inner_offset * block->inner_magic) >> MM_CHUNK_MAGIC_SHIFT;
	const uint32_t inner_mask = 1u << inner_shift;
	VERIFY((inner->free & inner_mask) == 0);
	inner->free |= inner_mask;
	if (inner->free == 0xfffe) {
		block->inner_used ^= mask;
		block->inner_free ^= mask;
		block->chunk_free |= mask;
	} else {
		block->inner_free |= mask;
	}
}
