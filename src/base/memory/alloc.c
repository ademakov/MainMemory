/*
 * base/memory/alloc.c - MainMemory memory allocation.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "base/memory/alloc.h"

#include "base/log/error.h"
#include "base/memory/global.h"
#include "base/memory/malloc.h"
#include "base/util/libcall.h"

/**********************************************************************
 * Stubs for LIBC Memory Allocation Routines.
 **********************************************************************/

void *
malloc(size_t size)
{
	mm_libcall("malloc");
	return mm_global_alloc(size);
}

void *
calloc(size_t count, size_t size)
{
	mm_libcall("calloc");
	return mm_global_calloc(count, size);
}

void *
realloc(void *ptr, size_t size)
{
	mm_libcall("realloc");
	return mm_global_realloc(ptr, size);
}

void
free(void *ptr)
{
	// This is called too often by sprintf, suppress it for now.
	//mm_libcall("free");
	mm_global_free(ptr);
}

/**********************************************************************
 * Memory subsystem initialization.
 **********************************************************************/

void
mm_alloc_init(void)
{
	dlmallopt(M_GRANULARITY, 16 * MM_PAGE_SIZE);
}

/**********************************************************************
 * Memory Space Allocation Routines.
 **********************************************************************/

mm_mspace_t
mm_mspace_create(void)
{
	mm_mspace_t space;
	space.opaque = create_mspace(0, 0);
	if (space.opaque == NULL)
		mm_fatal(errno, "failed to create mspace");
	return space;
}

void
mm_mspace_destroy(mm_mspace_t space)
{
	destroy_mspace(space.opaque);
}

void *
mm_mspace_alloc(mm_mspace_t space, size_t size)
{
	return mspace_malloc(space.opaque, size);
}

void *
mm_mspace_aligned_alloc(mm_mspace_t space, size_t align, size_t size)
{
	return mspace_memalign(space.opaque, align, size);
}

void *
mm_mspace_calloc(mm_mspace_t space, size_t count, size_t size)
{
	return mspace_calloc(space.opaque, count, size);
}

void *
mm_mspace_realloc(mm_mspace_t space, void *ptr, size_t size)
{
	return mspace_realloc(space.opaque, ptr, size);
}

void
mm_mspace_free(mm_mspace_t space, void *ptr)
{
	mspace_free(space.opaque, ptr);
}

size_t
mm_mspace_getallocsize(const void *ptr)
{
	return mspace_usable_size(ptr);
}

void
mm_mspace_bulk_free(mm_mspace_t space, void **ptrs, size_t nptrs)
{
	mspace_bulk_free(space.opaque, ptrs, nptrs);
}

void
mm_mspace_trim(mm_mspace_t space)
{
	mspace_trim(space.opaque, 16 * MM_PAGE_SIZE);
}

size_t
mm_mspace_getfootprint(mm_mspace_t space)
{
	return mspace_footprint(space.opaque);
}

size_t
mm_mspace_getfootprint_limit(mm_mspace_t space)
{
	return mspace_footprint_limit(space.opaque);
}

size_t
mm_mspace_setfootprint_limit(mm_mspace_t space, size_t size)
{
	return mspace_set_footprint_limit(space.opaque, size);
}
