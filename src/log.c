/*
 * log.c - MainMemory logging.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#include "log.h"

#include "alloc.h"
#include "chunk.h"
#include "core.h"
#include "list.h"
#include "lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>

/**********************************************************************
 * Low-Level Logging Routines.
 **********************************************************************/

#define MM_LOG_CHUNK_SIZE	(2000)

static struct mm_list mm_log_data = { &mm_log_data, &mm_log_data };
static mm_global_lock_t mm_log_lock = MM_ATOMIC_LOCK_INIT;
static bool mm_log_busy = false;

static void
mm_log_add_chain(struct mm_list *head, struct mm_list *tail)
{
	mm_global_lock(&mm_log_lock);
	mm_list_splice_prev(&mm_log_data, head, tail);
	mm_global_unlock(&mm_log_lock);
}

static void
mm_log_add_chunk(struct mm_chunk *chunk)
{
	mm_log_add_chain(&chunk->link, &chunk->link);
}

static struct mm_chunk *
mm_log_create_chunk(size_t size)
{
	struct mm_chunk *chunk;
	if (mm_core == NULL) {
		chunk = mm_alloc(sizeof(struct mm_chunk) + size);
		chunk->size = size;
		chunk->used = 0;
		chunk->core = NULL;
	} else {
		if (size < MM_LOG_CHUNK_SIZE)
			size = MM_LOG_CHUNK_SIZE;
		chunk = mm_chunk_create(size);
		mm_list_append(&mm_core->log_chunks, &chunk->link);
	}

	return chunk;
}

void
mm_log_str(const char *str)
{
	size_t len = strlen(str);

	struct mm_chunk *chunk = NULL;
	if (mm_core != NULL && !mm_list_empty(&mm_core->log_chunks)) {
		struct mm_list *tail = mm_list_tail(&mm_core->log_chunks);
		chunk = containerof(tail, struct mm_chunk, link);

		size_t avail = chunk->size - chunk->used;
		if (avail < len) {
			memcpy(chunk->data + chunk->used, str, avail);
			chunk->used += avail;
			str += avail;
			len -= avail;
			chunk = NULL;
		}
	}

	if (chunk == NULL) {
		chunk = mm_log_create_chunk(len);
	}

	memcpy(chunk->data + chunk->used, str, len);
	chunk->used += len;

	if (mm_core == NULL) {
		mm_log_add_chunk(chunk);
		if (len == 1 && str[0] == '\n') {
			mm_log_write();
		}
	}
}

void
mm_log_vfmt(const char *restrict fmt, va_list va)
{
	struct mm_chunk *chunk = NULL;
	if (mm_core != NULL && !mm_list_empty(&mm_core->log_chunks)) {
		struct mm_list *tail = mm_list_tail(&mm_core->log_chunks);
		chunk = containerof(tail, struct mm_chunk, link);
	}

	char dummy[1];
	char *space;
	size_t avail = 0;
	if (chunk == NULL) {
		space = dummy;
		avail = sizeof dummy;
	} else {
		space = chunk->data + chunk->used;
		avail = chunk->size - chunk->used;
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

	if (mm_core == NULL) {
		mm_log_add_chunk(chunk);
	}
}

void
mm_log_fmt(const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_log_vfmt(fmt, va);
	va_end(va);
}

size_t
mm_log_write(void)
{
	mm_global_lock(&mm_log_lock);

	if (mm_log_busy || mm_list_empty(&mm_log_data)) {
		mm_global_unlock(&mm_log_lock);
		// TODO: wait for write completion.
		return 0;
	}

	struct mm_list *head = mm_list_head(&mm_log_data);
	struct mm_list *tail = mm_list_tail(&mm_log_data);
	mm_list_cleave(head, tail);
	mm_log_busy = true;

	mm_global_unlock(&mm_log_lock);

	// The number of written bytes.
	size_t written = 0;

	for (;;) {
		struct mm_chunk *chunk = containerof(head, struct mm_chunk, link);
		if (write(2, chunk->data, chunk->used) != chunk->used) {
			ABORT();
		}

		written += chunk->used;

		struct mm_list *next = head->next;
		if (chunk->core == NULL) {
			mm_free(chunk);
		} else {
			mm_chunk_destroy_global(chunk);
		}

		if (head == tail)
			break;

		head = next;
	}

	// TODO: signal the write completion to waiting threads.
	mm_memory_store(mm_log_busy, false);

	return written;
}

/**********************************************************************
 * High-Level Logging Routines.
 **********************************************************************/

static bool mm_verbose_enabled = false;
static bool mm_warning_enabled = false;

void
mm_enable_verbose(bool value)
{
	mm_verbose_enabled = value;
}

void
mm_enable_warning(bool value)
{
	mm_warning_enabled = value;
}

void
mm_brief(const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

void
mm_verbose(const char *restrict msg, ...)
{
	if (!mm_verbose_enabled)
		return;

	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

void
mm_warning(int error, const char *restrict msg, ...)
{
	if (!mm_warning_enabled)
		return;

	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}
}

void
mm_error(int error, const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}
}

void
mm_fatal(int error, const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_brief(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}

	mm_exit(EXIT_FAILURE);
}

void
mm_flush(void)
{
	if (mm_core != NULL && !mm_list_empty(&mm_core->log_chunks)) {
		struct mm_list *head = mm_list_head(&mm_core->log_chunks);
		struct mm_list *tail = mm_list_tail(&mm_core->log_chunks);
		mm_list_cleave(head, tail);
		mm_log_add_chain(head, tail);
	}
}
