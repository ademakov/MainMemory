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
  large chunk size:
    0 0 x x | x x x x

  byte 1
  ------
  free large chunk
    0 1 0 0 | 0 0 0 0  == 0x40
  allocated large chunk
    0 1 0 0 | 0 0 0 1  == 0x41
  block of small and/or medium chunks
    0 1 0 0 | 0 0 1 0  == 0x42

  byte 2, 4, ...
  --------
  block start unit -- lo 6 bits
    1 0 x x | x x x x  >= 0x80

  byte 3, 5, ...
  --------
  block start unit -- hi 6 bits
    1 1 x x | x x x x  >= 0xc0

  Last byte if even
  -----------------
    0 1 0 0 | 0 0 1 1  == 0x43

*/

/* The number of size classes in cache. */
#define MM_MEMORY_CACHE_SMALL		(20u)
#define MM_MEMORY_CACHE_MEDIUM		(20u)
#define MM_MEMORY_CACHE_BLOCKS		(MM_MEMORY_CACHE_SMALL + MM_MEMORY_CACHE_MEDIUM)
#define MM_MEMORY_CACHE_CHUNKS		(36u)
#define MM_MEMORY_CACHE_TOTAL		(MM_MEMORY_CACHE_BLOCKS + MM_MEMORY_CACHE_CHUNKS)

#define MM_MEMORY_CACHE_UNIT_SIZE	(1024u)
#define MM_MEMORY_CACHE_UNIT_NUMBER	(MM_MEMORY_SPAN_HEAP_SIZE / MM_MEMORY_CACHE_UNIT_SIZE)

#define MM_MEMORY_CACHE_FREE_MARK	(0x40)
#define MM_MEMORY_CACHE_CHUNK_MARK	(0x41)
#define MM_MEMORY_CACHE_BLOCK_MARK	(0x42)
#define MM_MEMORY_CACHE_DUMMY_MARK	(0x43)
#define MM_MEMORY_CACHE_BASE_LBITS	(0x80)
#define MM_MEMORY_CACHE_BASE_HBITS	(0xc0)

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

	struct mm_memory_block *blocks[MM_MEMORY_CACHE_BLOCKS];
	struct mm_stack chunks[MM_MEMORY_CACHE_CHUNKS];

	uint8_t units[MM_MEMORY_CACHE_UNIT_NUMBER];
};

// Chunk sizes.
static const uint32_t mm_memory_chunk_sizes[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_SIZE),
	MM_CHUNK_UPPER_ROWS(MM_CHUNK_MAKE_SIZE)
};

// Chunk size magic numbers.
static const uint32_t mm_memory_chunk_magic[] = {
	MM_CHUNK_LOWER_ROWS(MM_CHUNK_MAKE_MAGIC)
};

static inline uint32_t
mm_memory_size_index(size_t size)
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
mm_memory_index_size(size_t index)
{
	return MM_CHUNK_MAKE_SIZE(index >> 2u, index & 3u);
}

static void
mm_memory_block_free(struct mm_memory_heap *const heap, const uint32_t unit, void *const chunk)
{
	struct mm_memory_block *const block = (struct mm_memory_block *) ((uint8_t *) heap + unit * MM_MEMORY_CACHE_UNIT_SIZE);
	const uint32_t shift = (((uint8_t *) chunk - (uint8_t *) block) * block->chunk_magic) >> MM_CHUNK_MAGIC_SHIFT;
	const uint32_t mask = 1u << shift;
	if ((block->inner_used & mask) == 0) {
		VERIFY((block->chunk_free & mask) == 0);
		block->chunk_free |= mask;
		return;
	}

	struct mm_memory_block_inner *const inner = (struct mm_memory_block_inner *) ((uint8_t *) block + shift * block->chunk_size);
	const uint32_t inner_shift = (((uint8_t *) chunk - (uint8_t *) inner) * block->inner_magic) >> MM_CHUNK_MAGIC_SHIFT;
	const uint32_t inner_mask = 1u << inner_shift;
	if (inner->free == 0) {
		inner->free = inner_mask;
		block->inner_free |= mask;
	} else {
		VERIFY((inner->free & inner_mask) == 0);
		inner->free |= inner_mask;
		if (inner->free == 0xfffe) {
			block->inner_used ^= mask;
			block->inner_free ^= mask;
			block->chunk_free |= mask;
		}
	}
}

static void
mm_memory_cache_prepare_heap(struct mm_memory_span *const span)
{
	struct mm_memory_heap *const heap = (struct mm_memory_heap *) span;
	for (uint32_t i = 0; i < MM_MEMORY_CACHE_BLOCKS; i++)
		heap->blocks[i] = NULL;
	for (uint32_t i = 0; i < MM_MEMORY_CACHE_CHUNKS; i++)
		mm_stack_prepare(&heap->chunks[i]);
	memset(heap->units, 0, sizeof heap->units);
}

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *cache, struct mm_context *context)
{
	ENTER();

	cache->active = NULL;
	mm_list_prepare(&cache->heap);
	mm_list_prepare(&cache->huge);
	cache->context = context;

	struct mm_memory_span *const span = mm_memory_span_create_heap(cache, true);
	if (span == NULL)
		mm_fatal(errno, "failed to create an initial memory span");
	mm_memory_cache_prepare_heap(span);

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
	const uint32_t index = mm_memory_size_index(size);

	if (index >= MM_MEMORY_CACHE_BLOCKS) {
		if (index >= MM_MEMORY_CACHE_TOTAL) {
			struct mm_memory_span *span = mm_memory_span_create_huge(cache, size);
			chunk = ((uint8_t *) span) + sizeof(struct mm_memory_span);
		} else {
			const uint32_t chunk_index = index - MM_MEMORY_CACHE_BLOCKS;
			struct mm_memory_heap *heap = cache->active;
			if (mm_stack_empty(&heap->chunks[chunk_index])) {
				// TODO
				goto leave;
			}
			chunk = mm_stack_remove(&heap->chunks[chunk_index]);
		}

	} else if (index >= MM_MEMORY_CACHE_SMALL) {
		struct mm_memory_heap *heap = cache->active;
		struct mm_memory_block *block = heap->blocks[index];
		if (block == NULL || block->chunk_free == 0) {
			// TODO
			goto leave;
		}

		const uint32_t shift = mm_ctz(block->chunk_free);
		block->chunk_free ^= (1u << shift);
		chunk = (uint8_t *) block + shift * block->chunk_size;

	} else {
		struct mm_memory_heap *heap = cache->active;
		struct mm_memory_block *block = heap->blocks[index];
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
	uint32_t unit = ((uint8_t *) ptr - (uint8_t *) span) / MM_MEMORY_CACHE_UNIT_SIZE;
	uint8_t code = heap->units[unit];
	if (code < MM_MEMORY_CACHE_CHUNKS) {
		uint8_t next = heap->units[unit + 1];
		if (next == MM_MEMORY_CACHE_CHUNK_MARK) {
			heap->units[unit + 1] = MM_MEMORY_CACHE_FREE_MARK;
			struct mm_slink *link = (struct mm_slink *) ((uint8_t *) heap + unit * MM_MEMORY_CACHE_UNIT_SIZE);
			mm_stack_insert(&heap->chunks[code], link);
			return;
		}
		VERIFY(next == MM_MEMORY_CACHE_BLOCK_MARK);
	} else if (code < MM_MEMORY_CACHE_BASE_LBITS) {
		if (code == MM_MEMORY_CACHE_BLOCK_MARK) {
			//code = heap->units[--unit];
			//VERIFY(code < MM_MEMORY_CACHE_CHUNKS);
			mm_memory_block_free(heap, unit - 1, ptr);
			return;
		}
		if (code == MM_MEMORY_CACHE_DUMMY_MARK) {
			unit -= 2;
			code = heap->units[unit];
		}
	} else if (code >= MM_MEMORY_CACHE_BASE_HBITS) {
		code = heap->units[--unit];
		VERIFY(code < MM_MEMORY_CACHE_CHUNKS);
	}
}
