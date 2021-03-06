/*
 * base/memory/alloc.c - MainMemory context-aware memory allocation.
 *
 * Copyright (C) 2020  Aleksey Demakov
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

#include "base/async.h"
#include "base/context.h"
#include "base/lock.h"
#include "base/report.h"
#include "base/memory/cache.h"
#include "base/memory/span.h"
#include "base/thread/backoff.h"

#define MM_FREE_WARN_THRESHOLD	(64)
#define MM_FREE_ERROR_THRESHOLD	(512)
#define MM_FREE_FATAL_THRESHOLD	(4096)

// Global memory cache used to bootsrtrap per-context caches.
static struct mm_memory_cache mm_memory_fixed_cache;
static mm_lock_t mm_memory_fixed_cache_lock = MM_LOCK_INIT;

static void
mm_memory_fixed_cache_ensure(void)
{
	if (unlikely(mm_memory_fixed_cache.active == NULL)) {
		mm_memory_cache_prepare(&mm_memory_fixed_cache, NULL);
	}
}

/**********************************************************************
 * 'Fixed' memory allocation routines -- survive context destruction.
 **********************************************************************/

void * MALLOC
mm_memory_fixed_alloc(size_t size)
{
	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_fixed_cache_ensure();
	void *ptr = mm_memory_cache_alloc(&mm_memory_fixed_cache, size);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
	return ptr;
}

void * MALLOC
mm_memory_fixed_zalloc(size_t size)
{
	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_fixed_cache_ensure();
	void *ptr = mm_memory_cache_zalloc(&mm_memory_fixed_cache, size);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
	return ptr;
}

void * MALLOC
mm_memory_fixed_aligned_alloc(size_t align, size_t size)
{
	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_fixed_cache_ensure();
	void *ptr = mm_memory_cache_aligned_alloc(&mm_memory_fixed_cache, align, size);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
	return ptr;
}

void * MALLOC
mm_memory_fixed_calloc(size_t count, size_t size)
{
	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_fixed_cache_ensure();
	void *ptr = mm_memory_cache_calloc(&mm_memory_fixed_cache, count, size);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
	return ptr;
}

void * MALLOC
mm_memory_fixed_realloc(void *ptr, size_t size)
{
	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_fixed_cache_ensure();
	if (ptr != NULL)
		ptr = mm_memory_cache_realloc(&mm_memory_fixed_cache, ptr, size);
	else
		ptr = mm_memory_cache_alloc(&mm_memory_fixed_cache, size);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
	return ptr;
}

void * MALLOC
mm_memory_fixed_xalloc(size_t size)
{
	void *ptr = mm_memory_fixed_alloc(size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_fixed_xzalloc(size_t size)
{
	void *ptr = mm_memory_fixed_zalloc(size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_fixed_aligned_xalloc(size_t align, size_t size)
{
	void *ptr = mm_memory_fixed_aligned_alloc(align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_fixed_xcalloc(size_t count, size_t size)
{
	void *ptr = mm_memory_fixed_calloc(count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating (%zu * %zu) bytes of memory", count, size);
	return ptr;
}

void * MALLOC
mm_memory_fixed_xrealloc(void *ptr, size_t size)
{
	ptr = mm_memory_fixed_realloc(ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_memory_fixed_free(void *ptr)
{
	if (ptr == NULL)
		return;

	mm_global_lock(&mm_memory_fixed_cache_lock);
	mm_memory_cache_local_free(&mm_memory_fixed_cache, ptr);
	mm_global_unlock(&mm_memory_fixed_cache_lock);
}

/**********************************************************************
 * Basic memory allocation routines.
 **********************************************************************/

void * MALLOC
mm_memory_alloc(size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_alloc(context, size);
	} else {
		return mm_memory_fixed_alloc(size);
	}
}

void * MALLOC
mm_memory_zalloc(size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_zalloc(context, size);
	} else {
		return mm_memory_fixed_zalloc(size);
	}
}

void * MALLOC
mm_memory_aligned_alloc(size_t align, size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_aligned_alloc(context, align, size);
	} else {
		return mm_memory_fixed_aligned_alloc(align, size);
	}
}

void * MALLOC
mm_memory_calloc(size_t count, size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_calloc(context, count, size);
	} else {
		return mm_memory_fixed_calloc(count, size);
	}
}

void * MALLOC
mm_memory_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return mm_memory_alloc(size);

	struct mm_memory_span *span = mm_memory_span_from_ptr(ptr);
	if (span->context != NULL) {
		struct mm_context *context = mm_context_selfptr();
		if (context == span->context) {
			return mm_memory_cache_realloc(&context->cache, ptr, size);
		} else {
			// TODO: optimize for huge spans
			mm_memory_cache_remote_free(span, ptr);
			return mm_memory_cache_alloc(&context->cache, size);
		}
	} else {
		return mm_memory_fixed_realloc(ptr, size);
	}
}

void * MALLOC
mm_memory_xalloc(size_t size)
{
	void *ptr = mm_memory_alloc(size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_xzalloc(size_t size)
{
	void *ptr = mm_memory_zalloc(size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_aligned_xalloc(size_t align, size_t size)
{
	void *ptr = mm_memory_aligned_alloc(align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * MALLOC
mm_memory_xcalloc(size_t count, size_t size)
{
	void *ptr = mm_memory_calloc(count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating (%zu * %zu) bytes of memory", count, size);
	return ptr;
}

void * MALLOC
mm_memory_xrealloc(void *ptr, size_t size)
{
	ptr = mm_memory_realloc(ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_memory_free(void *ptr)
{
	if (ptr == NULL)
		return;

	struct mm_memory_span *span = mm_memory_span_from_ptr(ptr);
	if (span->context != NULL) {
		struct mm_context *context = mm_context_selfptr();
		if (context == span->context) {
			mm_memory_cache_local_free(&context->cache, ptr);
		} else {
			mm_memory_cache_remote_free(span, ptr);
		}
	} else {
		mm_memory_fixed_free(ptr);
	}
}
