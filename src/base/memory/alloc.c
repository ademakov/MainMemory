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
#include "base/exit.h"
#include "base/lock.h"
#include "base/report.h"
#include "base/memory/cache.h"
#include "base/memory/span.h"
#include "base/thread/backoff.h"

#define MM_FREE_WARN_THRESHOLD	(64)
#define MM_FREE_ERROR_THRESHOLD	(512)
#define MM_FREE_FATAL_THRESHOLD	(4096)

// Global memory cache used to bootsrtrap per-context caches.
static struct mm_memory_cache mm_memory_initial_cache;
static mm_lock_t mm_memory_initial_cache_lock = MM_LOCK_INIT;

static void
mm_memory_initial_cache_ensure(void)
{
	if (unlikely(mm_memory_initial_cache.active == NULL)) {
		mm_memory_cache_prepare(&mm_memory_initial_cache, NULL);
	}
}

static void * MALLOC
mm_memory_initial_alloc(size_t size)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_initial_cache_ensure();
	void *ptr = mm_memory_cache_alloc(&mm_memory_initial_cache, size);
	mm_global_unlock(&mm_memory_initial_cache_lock);
	return ptr;
}

static void * MALLOC
mm_memory_initial_zalloc(size_t size)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_initial_cache_ensure();
	void *ptr = mm_memory_cache_zalloc(&mm_memory_initial_cache, size);
	mm_global_unlock(&mm_memory_initial_cache_lock);
	return ptr;
}

static void * MALLOC
mm_memory_initial_aligned_alloc(size_t align, size_t size)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_initial_cache_ensure();
	void *ptr = mm_memory_cache_aligned_alloc(&mm_memory_initial_cache, align, size);
	mm_global_unlock(&mm_memory_initial_cache_lock);
	return ptr;
}

static void * MALLOC
mm_memory_initial_calloc(size_t count, size_t size)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_initial_cache_ensure();
	void *ptr = mm_memory_cache_calloc(&mm_memory_initial_cache, count, size);
	mm_global_unlock(&mm_memory_initial_cache_lock);
	return ptr;
}

static void * MALLOC
mm_memory_initial_realloc(void *ptr, size_t size)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_initial_cache_ensure();
	ptr = mm_memory_cache_realloc(&mm_memory_initial_cache, ptr, size);
	mm_global_unlock(&mm_memory_initial_cache_lock);
	return ptr;
}

static void
mm_memory_initial_free(void *ptr)
{
	mm_global_lock(&mm_memory_initial_cache_lock);
	mm_memory_cache_free(&mm_memory_initial_cache, ptr);
	mm_global_unlock(&mm_memory_initial_cache_lock);
}

static void
mm_memory_remote_context_free_req(struct mm_context *context, uintptr_t *arguments)
{
	void *ptr = (void *) arguments[0];
	mm_context_free(context, ptr);
}

static void
mm_memory_remote_context_free(struct mm_context *context, void *ptr)
{
	if (!mm_async_trycall_1(context, mm_memory_remote_context_free_req, (uintptr_t) ptr)) {
		uint32_t count = 0;
		uint32_t backoff = 0;
		while (mm_async_trycall_1(context, mm_memory_remote_context_free_req, (uintptr_t) ptr)) {
			count++;
			if (count == MM_FREE_WARN_THRESHOLD) {
				mm_warning(0, "Problem with slow chunk reclamation");
			} else if (count == MM_FREE_ERROR_THRESHOLD) {
				mm_error(0, "Problem with slow chunk reclamation");
			} else if (count == MM_FREE_FATAL_THRESHOLD) {
				mm_fatal(0, "Problem with slow chunk reclamation");
			}
			backoff = mm_thread_backoff(backoff);
		}
	}
}

void * MALLOC
mm_memory_alloc(size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_alloc(context, size);
	} else {
		return mm_memory_initial_alloc(size);
	}
}

void * MALLOC
mm_memory_zalloc(size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_zalloc(context, size);
	} else {
		return mm_memory_initial_zalloc(size);
	}
}

void * MALLOC
mm_memory_aligned_alloc(size_t align, size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_aligned_alloc(context, align, size);
	} else {
		return mm_memory_initial_aligned_alloc(align, size);
	}
}

void * MALLOC
mm_memory_calloc(size_t count, size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_calloc(context, count, size);
	} else {
		return mm_memory_initial_calloc(count, size);
	}
}

void * MALLOC
mm_memory_realloc(void *ptr, size_t size)
{
	struct mm_context *context = mm_context_selfptr();
	if (context != NULL) {
		return mm_context_realloc(context, ptr, size);
	} else {
		return mm_memory_initial_realloc(ptr, size);
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
			mm_context_free(context, ptr);
		} else {
			mm_memory_remote_context_free(span->context, ptr);
		}
	} else {
		mm_memory_initial_free(ptr);
	}
}
