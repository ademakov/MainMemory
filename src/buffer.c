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
#include "chunk.h"
#include "log.h"
#include "trace.h"

#include <stdarg.h>
#include <stdio.h>

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

static struct mm_buffer_segment *
mm_buffer_segment_create(char *data, size_t size,
			 mm_buffer_release_t release,
			 uintptr_t release_data)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_local_alloc(sizeof *seg);
	seg->data = data;
	seg->size = size;
	seg->next = NULL;
	seg->release = release;
	seg->release_data = release_data;

	LEAVE();
	return seg;
}

static void
mm_buffer_segment_destroy(struct mm_buffer_segment *seg)
{
	ENTER();

	if (seg->release != NULL)
		(*seg->release)(seg->release_data);
	mm_local_free(seg);

	LEAVE();
}

/**********************************************************************
 * Buffer internal segments.
 **********************************************************************/

#define MM_BUFFER_MIN_CHUNK_SIZE	(4 * 1024 - MM_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(256 * 1024 - MM_CHUNK_OVERHEAD)

static void
mm_buffer_chunk_release(uintptr_t release_data)
{
	ENTER();

	mm_chunk_destroy((struct mm_chunk *) release_data);

	LEAVE();
}

// Create a new buffer segment that resides in an internal buffer chunk.
// The created segment size may be smaller or bigger than requested.
static struct mm_buffer_segment *
mm_buffer_chunk_reserve(size_t desired_size)
{
	ENTER();

	// The chunk should have a reasonable size that does not
	// put too much pressure on the memory allocator.
	size_t size = desired_size;
	if (size < MM_BUFFER_MIN_CHUNK_SIZE)
		size = MM_BUFFER_MIN_CHUNK_SIZE;
	if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	DEBUG("reserve %d/%d bytes for a buffer chunk",
	      (int) size, (int) desired_size);

	// Create an internal chunk.
	struct mm_chunk *chunk = mm_chunk_create(size);
	size = mm_chunk_size(chunk);

	// Create a buffer segment based on the chunk.
	struct mm_buffer_segment *seg
		= mm_buffer_segment_create(chunk->data, size,
					   mm_buffer_chunk_release,
					   (uintptr_t) chunk);

	LEAVE();
	return seg;
}

static inline bool
mm_buffer_is_chunk_segment(struct mm_buffer_segment *seg)
{
	return (seg->release == mm_buffer_chunk_release);
}

/**********************************************************************
 * Buffer helper routines.
 **********************************************************************/

static struct mm_buffer_segment *
mm_buffer_ensure_first_in(struct mm_buffer *buf, size_t desired_size)
{
	struct mm_buffer_segment *seg = buf->in_seg;
	if (seg == NULL) {
		seg = mm_buffer_chunk_reserve(desired_size);
		buf->in_seg = seg;
		buf->out_seg = seg;
		buf->chunk_size += seg->size;
	}
	return seg;
}

static struct mm_buffer_segment *
mm_buffer_ensure_next_in(struct mm_buffer *buf,
			 struct mm_buffer_segment *seg,
			 size_t desired_size)
{
	if (seg->next == NULL) {
		seg->next = mm_buffer_chunk_reserve(desired_size);
		buf->chunk_size += seg->size;
	}
	return seg->next;
}

/**********************************************************************
 * Buffer routines.
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
		mm_buffer_segment_destroy(seg);
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
	if (seg == NULL)
		goto leave;
	if (!mm_buffer_empty(buf))
		goto leave;

	if (mm_buffer_is_chunk_segment(seg)) {
		struct mm_chunk *chunk = (struct mm_chunk *) seg->release_data;
		size_t n = seg->data - chunk->data;
		seg->data -= n;
		seg->size += n;
	} else if (seg->release != NULL) {
		buf->in_seg = seg->next;
		buf->out_seg = seg->next;
		buf->extra_size -= seg->size;
		mm_buffer_segment_destroy(seg);
	}
	buf->in_off = 0;
	buf->out_off = 0;

leave:
	LEAVE();
}

void
mm_buffer_append(struct mm_buffer *buf, const char *data, size_t size)
{
	ENTER();

	if (unlikely(size == 0))
		goto leave;

	struct mm_buffer_segment *seg = mm_buffer_ensure_first_in(buf, size);
	char *p = seg->data + buf->in_off;
	size_t n = seg->size - buf->in_off;

	while (n < size) {
		memcpy(p, data, n);
		data += n;
		size -= n;

		seg = mm_buffer_ensure_next_in(buf, seg, size);
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

leave:
	LEAVE();
}

void
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_buffer_ensure_first_in(buf, 1);
	char *p = seg->data + buf->in_off;
	size_t n = seg->size - buf->in_off;

	va_list va2;
	va_copy(va2, va);
	int len = vsnprintf(p, n, fmt, va2);
	va_end(va2);

	if (unlikely(len < 0)) {
		mm_error(errno, "invalid format string");
	} else if ((unsigned) len < n) {
		buf->in_off += len;
	} else {
		char *ptr = mm_local_alloc(len + 1);
		len = vsnprintf(ptr, len + 1, fmt, va);

		mm_buffer_append(buf, ptr, len);

		mm_local_free(ptr);
	}

	LEAVE();
}

void
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(buf, fmt, va);
	va_end(va);
}

void
mm_buffer_demand(struct mm_buffer *buf, size_t size)
{
	ENTER();

	if (unlikely(size == 0))
		goto leave;

	struct mm_buffer_segment *seg = mm_buffer_ensure_first_in(buf, size);
	size_t n = seg->size - buf->in_off;
	while (n < size) {
		size -= n;
		seg = mm_buffer_ensure_next_in(buf, seg, size);
		n = seg->size;
	}

leave:
	LEAVE();
}

size_t
mm_buffer_expand(struct mm_buffer *buf, size_t size)
{
	ENTER();

	size_t o_size = size;
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
	return (o_size - size);
}

size_t
mm_buffer_reduce(struct mm_buffer *buf, size_t size)
{
	ENTER();

	size_t o_size = size;
	struct mm_buffer_segment *seg = buf->out_seg;
	if (seg != NULL) {
		size_t n = seg->size - buf->out_off;
		while (n <= size && seg != buf->in_seg) {
			if (mm_buffer_is_chunk_segment(seg)) {
				struct mm_chunk *chunk = (struct mm_chunk *) seg->release_data;
				buf->chunk_size -= mm_chunk_size(chunk);
			} else {
				buf->extra_size -= seg->size;
			}

			struct mm_buffer_segment *next = seg->next;
			mm_buffer_segment_destroy(seg);
			seg = next;

			size -= n;
			n = seg->size;
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
	return (o_size - size);
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
			(*release)(release_data);
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
	buf->extra_size += size;

leave:
	LEAVE();
}
