/*
 * base/logger.c - MainMemory logging.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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

#include "base/logger.h"

#include "base/list.h"
#include "base/lock.h"
#include "base/memory/alloc.h"
#include "base/memory/cache.h"
#include "base/thread/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MM_LOG_CHUNK_SIZE	MM_PAGE_SIZE

struct mm_log_chunk
{
	struct mm_qlink link;
	uint32_t used;
	char data[];
};

// The pending log chunk list.
static struct mm_queue MM_QUEUE_INIT(mm_log_queue);

static mm_common_lock_t mm_log_lock = MM_COMMON_LOCK_INIT;
static bool mm_log_busy = false;

static struct mm_log_chunk *
mm_log_create_chunk(size_t size)
{
	size += sizeof(struct mm_log_chunk);
	if (size < MM_LOG_CHUNK_SIZE)
		size = MM_LOG_CHUNK_SIZE;

	struct mm_log_chunk *chunk = mm_memory_xalloc(size);
	mm_qlink_prepare(&chunk->link);
	chunk->used = 0;

	struct mm_queue *queue = mm_thread_getlog(mm_thread_selfptr());
	mm_queue_append(queue, &chunk->link);

	return chunk;
}

static size_t
mm_log_chunk_size(const struct mm_log_chunk *chunk)
{
	size_t size = mm_memory_cache_chunk_size(chunk);
	return size - sizeof(struct mm_log_chunk);
}

void NONNULL(1)
mm_log_str(const char *str)
{
	size_t len = strlen(str);

	struct mm_log_chunk *chunk = NULL;
	struct mm_queue *queue = mm_thread_getlog(mm_thread_selfptr());
	if (!mm_queue_empty(queue)) {
		struct mm_qlink *link = mm_queue_tail(queue);
		chunk = containerof(link, struct mm_log_chunk, link);

		size_t avail = mm_log_chunk_size(chunk) - chunk->used;
		if (avail < len) {
			memcpy(chunk->data + chunk->used, str, avail);
			chunk->used += avail;
			str += avail;
			len -= avail;
			chunk = NULL;
		}
	}

	if (chunk == NULL)
		chunk = mm_log_create_chunk(len);

	memcpy(chunk->data + chunk->used, str, len);
	chunk->used += len;
}

void NONNULL(1)
mm_log_vfmt(const char *restrict fmt, va_list va)
{
	struct mm_log_chunk *chunk = NULL;
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_queue *queue = mm_thread_getlog(thread);
	if (!mm_queue_empty(queue)) {
		struct mm_qlink *link = mm_queue_tail(queue);
		chunk = containerof(link, struct mm_log_chunk, link);
	}

	char dummy;
	char *space;
	size_t avail = 0;
	if (chunk == NULL) {
		space = &dummy;
		avail = sizeof dummy;
	} else {
		space = chunk->data + chunk->used;
		avail = mm_log_chunk_size(chunk) - chunk->used;
	}

	va_list va2;
	va_copy(va2, va);
	size_t len = vsnprintf(space, avail, fmt, va2);
	va_end(va2);

	if (chunk == NULL || len >= avail) {
		chunk = mm_log_create_chunk(len + 1);
		(void) vsnprintf(chunk->data, len + 1, fmt, va);
	}
	chunk->used += len;
}

void NONNULL(1) FORMAT(1, 2)
mm_log_fmt(const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_log_vfmt(fmt, va);
	va_end(va);
}

void
mm_log_relay(void)
{
	struct mm_queue *queue = mm_thread_getlog(mm_thread_selfptr());
	if (!mm_queue_empty(queue)) {
		struct mm_qlink *head = mm_queue_head(queue);
		struct mm_qlink *tail = mm_queue_tail(queue);

		mm_common_lock(&mm_log_lock);
		mm_queue_append_span(&mm_log_queue, head, tail);
		mm_common_unlock(&mm_log_lock);

		mm_queue_prepare(queue);
	}
}

size_t
mm_log_flush(void)
{
	mm_common_lock(&mm_log_lock);

	if (mm_log_busy || mm_queue_empty(&mm_log_queue)) {
		mm_common_unlock(&mm_log_lock);
		// TODO: wait for write completion.
		return 0;
	}

	struct mm_qlink *link = mm_queue_head(&mm_log_queue);
	mm_queue_prepare(&mm_log_queue);
	mm_log_busy = true;

	mm_common_unlock(&mm_log_lock);

	// The number of written bytes.
	size_t written = 0;

	do {
		struct mm_log_chunk *chunk = containerof(link, struct mm_log_chunk, link);
		link = chunk->link.next;

		// TODO: take care of partial writes
		if (write(2, chunk->data, chunk->used) != chunk->used)
			ABORT();

		written += chunk->used;

		mm_memory_free(chunk);

	} while (link != NULL);

	mm_memory_store(mm_log_busy, false);

	return written;
}
