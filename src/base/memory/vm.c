/*
 * base/memory/vm.c - MainMemory virtual memory allocator.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#include "base/memory/vm.h"

#include "base/bitops.h"
#include "base/report.h"

#include <sys/mman.h>

// A span is a large memory block allocated with a single mmap() system call.
// Each span is aligned to a 64 KiB boundary and is at least 512 KiB of size.
// It starts with span metadata info.
struct mm_vm_span
{
	size_t size;
	struct mm_vm_arena *arena;
};

// A span index node.
struct mm_vm_node
{
	union
	{
		struct mm_vm_node *node;
		struct mm_vm_span *span;
	};
};

// The span index.
static struct mm_vm_node mm_vm_nodes[0x8000];

// Chunk sizes.
static const uint32_t mm_vm_sizes[] = {
	//   0       1       2       3       4       5       6       7
	    16,     32,     48,     64,     80,     96,    112,    128,
	//   8       9      10      11      12      13      14      15
	   144,    160,    176,    192,    208,    224,    240,    256,
	//  16      17      18      19      20      21      22      23
	   288,    320,    352,    384,    416,    448,    480,    512,
	//  24      25      26      27      28      29      30      31
	   576,    640,    704,    768,    832,    896,    960,   1024,
	//  32      33      34      35      36      37      38      39
	  1152,   1280,   1408,   1536,   1664,   1792,   1920,   2048,
	//  40      41      42      43      44      45      46      47
	  2304,   2560,   2816,   3072,   3328,   3584,   3840,   4096,
	//  48      49      50      51      52      53      54      55
	  4608,   5120,   5632,   6144,   6656,   7168,   7680,   8192,
	//  56      57      58      59      60      61      62      63
	  9216,  10240,  11264,  12288,  13312,  14336,  15360,  16384,
	//  64      65      66      67      68      69      70      71
	 18432,  20480,  22528,  24576,  26624,  28672,  30720,  32768,
	//  72      73      74      75      76      77      78      79
	 36864,  40960,  45056,  49152,  53248,  57344,  61440,  65536,
	//  80      81      82      83      84      85      86      87
	 73728,  81920,  90112,  98304, 106496, 114688, 122880, 131072,
	//  88      89      90      91      92      93      94      95
	147456, 163840, 180224, 196608, 212992, 229376, 245760, 262144,
	//  96      97      98      99     100     101     102     103
	294912, 327680, 360448, 393216, 425984, 458752, 491520, 524288
};

static void *
mm_vm_make_span(size_t size)
{
	ASSERT(size != 0 && (size & 0xffff) == 0 && (size & (MM_PAGE_SIZE - 1)) == 0);

	// Allocate a span speculatively assuming that it will be aligned as
	// required.
	void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

#if MM_PAGE_SIZE < 0xffff
	// If failed to align then repeat allocation with required padding.
	if ((((uintptr_t) addr) & 0xffff) != 0) {
		munmap(addr, size);

		size_t aligned_size = size + 0xffff - MM_PAGE_SIZE + 1;
		if (aligned_size < size)
			return NULL;

		addr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (addr == MAP_FAILED)
			return NULL;

		void *aligned_addr = (void *) ((((uintptr_t) addr) + 0xffff) & 0xffff);
		size_t leading_size = aligned_addr - addr;
		size_t trailing_size = aligned_size - size - leading_size;
		if (leading_size)
			munmap(addr, leading_size);
		if (trailing_size)
			munmap(addr + leading_size + size, trailing_size);

		return aligned_addr;
	}
#endif

	return addr;
}

static inline struct mm_vm_span *
mm_vm_find_span(void *const ptr)
{
	const uintptr_t addr = (uintptr_t) ptr;
#if MM_ADDRESS_BITS > 48
	struct mm_vm_node *node = mm_vm_nodes[addr >> 48].node;
	if (unlikely(node == NULL))
		return NULL;
	node = node[(addr >> 32) & 0xffff].node;
	if (unlikely(node == NULL))
		return NULL;
	return node[(addr >> 16) & 0xffff].span;
#elif MM_ADDRESS_BITS > 32
	struct mm_vm_node *node = mm_vm_nodes[addr >> 32].node;
	if (unlikely(node == NULL))
		return NULL;
	return node[(addr >> 16) & 0xffff].span;
#else
	return mm_vm_nodes[addr >> 16].span;
#endif
}

static inline int32_t
mm_vm_size_index(size_t size)
{
	ASSERT(size);
	if (size-- < 128) {
		// Tiny sizes are rounded to a 16-byte multiple.
		return size >> 4;
	} else {
		// Search for most significant set bit, on x86 this should translate to
		// single BSR instruction.
		size_t msb = mm_clz(size) ^ (mm_nbits(size) - 1);
		// Calcualte the size class index.
		return (msb << 3u) + (size >> (msb - 3u)) - 56;
	}
}

static void *
mm_vm_getchunk(struct mm_stack *chunks, size_t size, mm_vm_flags_t flags)
{
	struct mm_slink *link = mm_stack_head(chunks);
	if (link != NULL) {
		mm_stack_remove(chunks);
		if ((flags & MM_VM_ALLOC_ZERO) != 0) {
			memset(link, 0, size);
		}
	}
	return link;
}

void NONNULL(1)
mm_vm_arena_prepare(struct mm_vm_arena *arena)
{
	for (int i = 0; i < MM_VM_ARENA_SIZE; i++) {
		mm_stack_prepare(&arena->chunks[i]);
	}
}

void NONNULL(1, 2)
mm_vm_cache_prepare(struct mm_vm_cache *cache, struct mm_vm_arena *arena)
{
	cache->arena = arena;
	for (int i = 0; i < MM_VM_CACHE_SIZE; i++) {
		mm_stack_prepare(&cache->chunks[i]);
	}
}

void * NONNULL(1)
mm_vm_alloc(struct mm_vm_cache *cache, size_t size, mm_vm_flags_t flags)
{
	if (size == 0)
		return NULL;

	int32_t i = mm_vm_size_index(size);
	if (i < MM_VM_CACHE_SIZE) {
		void *p = mm_vm_getchunk(&cache->chunks[i], size, flags);
		if (p != NULL)
			return p;

		if ((flags & MM_VM_ALLOC_FAST) != 0) {
			for (int32_t j = i + 1; j < MM_VM_CACHE_SIZE; j++) {
				p = mm_vm_getchunk(&cache->chunks[j], size, flags);
				if (p != NULL)
					return p;
			}
		}

	} else if (i < MM_VM_ARENA_SIZE) {
	} else {
	}

	return NULL;
}

#if 0
void * NONNULL(1)
mm_vm_realloc(struct mm_vm_cache *cache, void *ptr, size_t size)
{
}

void NONNULL(1)
mm_vm_free(struct mm_vm_cache *cache, void *ptr)
{
}
#endif
