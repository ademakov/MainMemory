/*
 * buffer.c - MainMemory data buffers.
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

#include "buffer.h"

#include "alloc.h"
#include "log.h"
#include "trace.h"
#include "chunk.h"

#include <stdio.h>
#include <stdarg.h>

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

#define MM_BUFFER_MIN_CHUNK_SIZE	(4 * 1024 - MM_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(256 * 1024 - MM_CHUNK_OVERHEAD)

static struct mm_buffer_segment *
mm_buffer_segment_create(char *data, size_t size,
			 mm_buffer_release_t release, uintptr_t release_data)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_core_alloc(sizeof *seg);
	seg->next = NULL;
	seg->data = data;
	seg->size = size;
	seg->release = release;
	seg->release_data = release_data;

	LEAVE();
	return seg;
}

static void
mm_buffer_segment_destroy(struct mm_buffer *buf, struct mm_buffer_segment *seg)
{
	ENTER();

	if (seg->release != NULL)
		(*seg->release)(buf, seg->release_data);
	mm_core_free(seg);

	LEAVE();
}

static void
mm_buffer_segment_release(struct mm_buffer *buf, uintptr_t release_data)
{
	ENTER();

	struct mm_chunk *chunk = (struct mm_chunk *) release_data;
	ASSERT(chunk == buf->store_head);

	if (chunk->next == NULL) {
		buf->store_head = NULL;
		buf->store_tail = NULL;
	} else {
		buf->store_head = chunk->next;
	}

	buf->store_size -= chunk->size;

	mm_chunk_destroy(chunk);

	LEAVE();
}

/**********************************************************************
 * Buffer helper routines.
 **********************************************************************/

// Create a new buffer segment that should reside in a backing store chunk.
// The created segment may be smaller than requested if the available chunk
// cannot fit it all.
static struct mm_buffer_segment *
mm_buffer_reserve(struct mm_buffer *buf, size_t desired_size)
{
	ENTER();

	// Create a backing store chunk if needed.
	struct mm_chunk *chunk = buf->store_tail;
	if (chunk == NULL || chunk->used == chunk->size) {
		size_t chunk_size = desired_size;
		if (chunk_size > MM_BUFFER_MIN_CHUNK_SIZE)
			chunk_size = MM_BUFFER_MIN_CHUNK_SIZE;
		if (chunk_size < MM_BUFFER_MAX_CHUNK_SIZE)
			chunk_size = MM_BUFFER_MAX_CHUNK_SIZE;

		chunk = mm_chunk_create(chunk_size);
		if (buf->store_tail == NULL)
			buf->store_head = chunk;
		else
			buf->store_tail->next = chunk;
		buf->store_tail = chunk;

		buf->store_size += chunk_size;
	}

	// Allot space in the last available backing store chunk.
	char *data = chunk->data + chunk->used;
	uint32_t size = chunk->size - chunk->used;
	if (size > desired_size)
		size = desired_size;
	chunk->used += size;

	DEBUG("reserve %d/%d bytes for a buffer segment", (int) size, (int) desired_size);

	// If the chunk is full then it has to be released when the segment
	// is consumed.
	mm_buffer_release_t release = NULL;
	uintptr_t release_data = 0;
	if (chunk->used == chunk->size) {
		release = mm_buffer_segment_release;
		release_data = (uintptr_t) chunk;
	}

	// Create a new segment.
	struct mm_buffer_segment *seg = mm_buffer_segment_create(data, size,
								 release, release_data);

	LEAVE();
	return seg;
}

static struct mm_buffer_segment *
mm_buffer_ensure(struct mm_buffer *buf, size_t desired_size)
{
	struct mm_buffer_segment *seg = buf->in_seg;
	if (seg == NULL) {
		seg = mm_buffer_reserve(buf, desired_size);
		buf->in_seg = seg;
		buf->out_seg = seg;
	}
	return seg;
}

/**********************************************************************
 * Buffers.
 **********************************************************************/

#define MM_BUFFER_SPLICE_THRESHOLD	(128)

void
mm_buffer_prepare(struct mm_buffer *buf)
{
	ENTER();

	memset(buf, 0, sizeof *buf);

	LEAVE();
}

void
mm_buffer_cleanup(struct mm_buffer *buf)
{
	ENTER();

	struct mm_buffer_segment *seg = buf->out_seg;
	while (seg != NULL) {
		struct mm_buffer_segment *next = seg->next;
		mm_buffer_segment_destroy(buf, seg);
		seg = next;
	}

	LEAVE();
}

/* Improve space utilization of an empty buffer that was previously in use. */
void
mm_buffer_rectify(struct mm_buffer *buf)
{
	ENTER();

	struct mm_buffer_segment *seg = buf->out_seg;
	if (seg != NULL && mm_buffer_empty(buf)) {
		if (buf->store_head != NULL
		    && seg->data >= buf->store_head->data
		    && seg->data < buf->store_head->data + buf->store_head->used) {
			size_t n = seg->data - buf->store_head->data;
			seg->data -= n;
			seg->size += n;
		} else if (seg->release != NULL) {
			buf->in_seg = seg->next;
			buf->out_seg = seg->next;
			mm_buffer_segment_destroy(buf, seg);
		}
		buf->in_off = 0;
		buf->out_off = 0;
	}

	LEAVE();
}

void
mm_buffer_append(struct mm_buffer *buf, const char *data, size_t size)
{
	ENTER();

	if (likely(size)) {
		struct mm_buffer_segment *seg = mm_buffer_ensure(buf, size);
		char *p = seg->data + buf->in_off;
		size_t n = seg->size - buf->in_off;

		while (n < size) {
			memcpy(p, data, n);
			data += n;
			size -= n;

			if (seg->next == NULL)
				seg->next = mm_buffer_reserve(buf, size);
			seg = seg->next;

			p = seg->data;
			n = seg->size;
		}

		memcpy(p, data, size);
		if (buf->in_seg == seg) {
			buf->in_off += size;
		} else {
			buf->in_off = size;
			buf->in_seg = seg;
		}
	}

	LEAVE();
}

void
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_buffer_ensure(buf, 1);
	char *p = seg->data + buf->in_off;
	size_t n = seg->size - buf->in_off;

	va_list va;
	va_start(va, fmt);
	int len = vsnprintf(p, n, fmt, va);
	va_end(va);

	if (unlikely(len < 0)) {
		mm_error(errno, "invalid format string");
	} else if ((unsigned) len < n) {
		buf->in_off += len;
	} else {
		char *ptr = mm_core_alloc(len + 1);

		va_start(va, fmt);
		len = vsnprintf(ptr, len + 1, fmt, va);
		va_end(va);

		mm_buffer_append(buf, ptr, len);
		mm_core_free(ptr);
	}

	LEAVE();
}

void
mm_buffer_demand(struct mm_buffer *buf, size_t size)
{
	ENTER();

	if (likely(size)) {
		struct mm_buffer_segment *seg = mm_buffer_ensure(buf, size);
		size_t n = seg->size - buf->in_off;

		while (n < size) {
			size -= n;

			if (seg->next == NULL)
				seg->next = mm_buffer_reserve(buf, size);
			seg = seg->next;

			n = seg->size;
		}
	}

	LEAVE();
}

size_t
mm_buffer_expand(struct mm_buffer *buf, size_t size)
{
	ENTER();

	size_t orig_size = size;
	struct mm_buffer_segment *seg = buf->in_seg;
	if (seg != NULL) {
		size_t n = seg->size - buf->in_off;
		while (n < size && seg->next != NULL) {
			size -= n;
			seg = seg->next;
			n = seg->size;
		}

		if (n > size)
			n = size;

		size -= n;
		if (buf->in_seg == seg) {
			buf->in_off += n;
		} else {
			buf->in_off = n;
			buf->in_seg = seg;
		}
	}

	LEAVE();
	return (orig_size - size);
}

size_t
mm_buffer_reduce(struct mm_buffer *buf, size_t size)
{
	ENTER();

	size_t orig_size = size;
	struct mm_buffer_segment *seg = buf->out_seg;
	if (seg != NULL) {
		size_t n = seg->size - buf->out_off;
		while (n <= size && seg != buf->in_seg) {
			struct mm_buffer_segment *tmp = seg;
			size -= n;
			seg = seg->next;
			n = seg->size;
			mm_buffer_segment_destroy(buf, tmp);
		}

		if (buf->in_seg == seg)
			n -= seg->size - buf->in_off;
		if (n > size)
			n = size;

		size -= n;
		if (buf->out_seg == seg) {
			buf->out_off += n;
		} else {
			buf->out_off = n;
			buf->out_seg = seg;
		}
	}

	LEAVE();
	return (orig_size - size);
}

void
mm_buffer_splice(struct mm_buffer *buf, char *data, size_t size,
		 mm_buffer_release_t release, uintptr_t release_data)
{
	ENTER();

	// Don't bother allocating a new segment for short data,
	// just copy it to the buffer's internal storage.
	if (size < MM_BUFFER_SPLICE_THRESHOLD) {
		mm_buffer_append(buf, data, size);
		if (release != NULL) {
			(*release)(buf, release_data);
		}
		goto leave;
	}

	// Create a new segment and insert it where appropriate.
	struct mm_buffer_segment *seg
		= mm_buffer_segment_create(data, size,
					   release, release_data);
	if (buf->in_seg == NULL) {
		seg->next = NULL;
		buf->out_seg = seg;
	} else if (buf->in_off == 0) {
		seg->next = buf->in_seg;
		if (buf->out_seg == buf->in_seg) {
			buf->out_seg = seg;
		} else {
			struct mm_buffer_segment *tmp = buf->out_seg;
			while (tmp->next != buf->in_seg)
				tmp = tmp->next;
			tmp->next = seg;
		}
	} else if (buf->in_off == buf->in_seg->size) {
		seg->next = buf->in_seg->next;
		buf->in_seg->next = seg;
	} else {
		// Split the in_seg segment into two parts.
		struct mm_buffer_segment *ls, *rs;
		ls = buf->in_seg;
		rs = mm_buffer_segment_create(ls->data + buf->in_off,
					      ls->size - buf->in_off,
					      ls->release, ls->release_data);
		rs->next = ls->next;
		ls->size = buf->in_off;
		ls->release = NULL;
		ls->release_data = 0;
		seg->next = rs;
		ls->next = seg;
	}
	buf->in_seg = seg;
	buf->in_off = size;

leave:
	LEAVE();
}
