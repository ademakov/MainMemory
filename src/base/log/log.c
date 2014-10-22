/*
 * base/log/log.c - MainMemory logging.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#include "base/log/log.h"
#include "base/list.h"
#include "base/lock.h"
#include "base/mem/alloc.h"
#include "base/mem/chunk.h"
#include "base/thr/thread.h"

#include "core/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>

#define MM_LOG_CHUNK_SIZE	(MM_PAGE_SIZE - MM_ALLOC_OVERHEAD)

struct mm_log_chunk
{
	struct mm_chunk_base base;
	uint16_t used;
	char data[];
};

// The pending log chunk list.
static struct mm_queue MM_QUEUE_INIT(mm_log_queue);

static mm_thread_lock_t mm_log_lock = MM_THREAD_LOCK_INIT;
static bool mm_log_busy = false;

static struct mm_log_chunk *
mm_log_create_chunk(size_t size)
{
	size += sizeof(struct mm_log_chunk) - sizeof(struct mm_chunk);
	if (size < MM_LOG_CHUNK_SIZE)
		size = MM_LOG_CHUNK_SIZE;

	struct mm_chunk *chunk;
	if (mm_core != NULL)
		chunk = mm_chunk_create(size);
	else
		chunk = mm_chunk_create_global(size);

	struct mm_log_chunk *log_chunk = (struct mm_log_chunk *) chunk;
	log_chunk->used = 0;

	struct mm_queue *queue = mm_thread_getlog(mm_thread_self());
	mm_queue_append(queue, &log_chunk->base.link);

	return log_chunk;
}

static size_t
mm_log_chunk_size(const struct mm_log_chunk *chunk)
{
	size_t size;
	if (chunk->base.core != MM_CORE_NONE)
		size = mm_chunk_base_size(&chunk->base);
	else
		size = mm_chunk_base_size_global(&chunk->base);
	return size - (sizeof(struct mm_log_chunk) - sizeof(struct mm_chunk));
}

void
mm_log_str(const char *str)
{
	size_t len = strlen(str);

	struct mm_log_chunk *chunk = NULL;
	struct mm_queue *queue = mm_thread_getlog(mm_thread_self());
	if (!mm_queue_empty(queue)) {
		struct mm_link *link = mm_queue_tail(queue);
		chunk = containerof(link, struct mm_log_chunk, base.link);

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

void
mm_log_vfmt(const char *restrict fmt, va_list va)
{
	struct mm_log_chunk *chunk = NULL;
	struct mm_queue *queue = mm_thread_getlog(mm_thread_self());
	if (!mm_queue_empty(queue)) {
		struct mm_link *link = mm_queue_tail(queue);
		chunk = containerof(link, struct mm_log_chunk, base.link);
	}

	char dummy[1];
	char *space;
	size_t avail = 0;
	if (chunk == NULL) {
		space = dummy;
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

void
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
	struct mm_queue *queue = mm_thread_getlog(mm_thread_self());
	if (!mm_queue_empty(queue)) {
		struct mm_link *head = mm_queue_head(queue);
		struct mm_link *tail = mm_queue_tail(queue);

		mm_thread_lock(&mm_log_lock);
		mm_queue_splice_tail(&mm_log_queue, head, tail);
		mm_thread_unlock(&mm_log_lock);

		mm_queue_init(queue);
	}
}

size_t
mm_log_flush(void)
{
	mm_thread_lock(&mm_log_lock);

	if (mm_log_busy || mm_queue_empty(&mm_log_queue)) {
		mm_thread_unlock(&mm_log_lock);
		// TODO: wait for write completion.
		return 0;
	}

	struct mm_link *link = mm_queue_head(&mm_log_queue);
	mm_queue_init(&mm_log_queue);
	mm_log_busy = true;

	mm_thread_unlock(&mm_log_lock);

	// The number of written bytes.
	size_t written = 0;

	do {
		struct mm_log_chunk *chunk = containerof(link, struct mm_log_chunk, base.link);
		link = chunk->base.link.next;

		// TODO: take care of partial writes
		if (write(2, chunk->data, chunk->used) != chunk->used)
			ABORT();

		written += chunk->used;

		if (chunk->base.core != MM_CORE_NONE)
			mm_core_reclaim_chunk((struct mm_chunk *) chunk);
		else
			mm_chunk_destroy_global((struct mm_chunk *) chunk);

	} while (link != NULL);

	mm_memory_store(mm_log_busy, false);

	return written;
}
