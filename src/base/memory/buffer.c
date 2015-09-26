/*
 * base/memory/buffer.c - MainMemory data buffers.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "base/memory/buffer.h"

#include "base/log/error.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"

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

	struct mm_buffer_segment *seg = mm_private_alloc(sizeof *seg);
	seg->data = data;
	seg->size = size;
	seg->next = NULL;
	seg->release = release;
	seg->release_data = release_data;

	LEAVE();
	return seg;
}

void
mm_buffer_segment_destroy(struct mm_buffer_segment *seg)
{
	ENTER();

	if (seg->release != NULL)
		(*seg->release)(seg->release_data);
	mm_private_free(seg);

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
	struct mm_chunk *chunk = mm_chunk_create_private(size);
	size = mm_chunk_getsize(chunk);

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
mm_buffer_ensure_tail(struct mm_buffer *buf, size_t desired_size)
{
	struct mm_buffer_segment *seg = buf->tail_seg;
	if (seg == NULL) {
		seg = mm_buffer_chunk_reserve(desired_size);
		buf->tail_seg = seg;
		buf->head_seg = seg;
	}
	return seg;
}

static struct mm_buffer_segment *
mm_buffer_ensure_next(struct mm_buffer_segment *seg,
		      size_t desired_size)
{
	if (seg->next == NULL)
		seg->next = mm_buffer_chunk_reserve(desired_size);
	return seg->next;
}

/**********************************************************************
 * Buffer routines.
 **********************************************************************/

#define MM_BUFFER_SPLICE_THRESHOLD	(128)

void __attribute__((nonnull(1)))
mm_buffer_prepare(struct mm_buffer *buf)
{
	ENTER();

	memset(buf, 0, sizeof *buf);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_buffer_cleanup(struct mm_buffer *buf)
{
	ENTER();

	struct mm_buffer_segment *seg = buf->head_seg;
	while (seg != NULL) {
		struct mm_buffer_segment *next = seg->next;
		mm_buffer_segment_destroy(seg);
		seg = next;
	}

	LEAVE();
}

/* Improve space utilization of an empty buffer that was previously in use. */
void __attribute__((nonnull(1)))
mm_buffer_rectify(struct mm_buffer *buf)
{
	ENTER();

	struct mm_buffer_segment *seg = buf->head_seg;
	if (seg == NULL || !mm_buffer_empty(buf))
		goto leave;

	if (mm_buffer_is_chunk_segment(seg)) {
		struct mm_chunk *chunk = (struct mm_chunk *) seg->release_data;
		seg->size += seg->data - chunk->data;
		seg->data = chunk->data;
	} else if (seg->release != NULL) {
		buf->tail_seg = seg->next;
		buf->head_seg = seg->next;
		mm_buffer_segment_destroy(seg);
	}

	buf->tail_off = 0;
	buf->head_off = 0;

leave:
	LEAVE();
}

void __attribute__((nonnull(1)))
mm_buffer_demand(struct mm_buffer *buf, size_t size)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_buffer_ensure_tail(buf, size);
	size_t n = seg->size - buf->tail_off;
	while (n < size) {
		size -= n;
		seg = mm_buffer_ensure_next(seg, size);
		n = seg->size;
	}

	LEAVE();
}

size_t __attribute__((nonnull(1)))
mm_buffer_fill(struct mm_buffer *buf, size_t size)
{
	ENTER();
	size_t o_size = size;

	struct mm_buffer_segment *seg = buf->tail_seg;
	if (unlikely(seg == NULL))
		goto leave;

	size_t n = seg->size - buf->tail_off;
	while (n < size && seg->next != NULL) {
		size -= n;

		buf->tail_seg = seg->next;
		buf->tail_off = 0;

		seg = buf->head_seg;
		n = seg->size;
	}

	if (n > size)
		n = size;

	buf->tail_off += n;
	size -= n;

leave:
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1)))
mm_buffer_flush(struct mm_buffer *buf, size_t size)
{
	ENTER();
	size_t o_size = size;

	struct mm_buffer_segment *seg = buf->head_seg;
	if (unlikely(seg == NULL))
		goto leave;

	size_t n = seg->size - buf->head_off;
	while (n <= size && seg != buf->tail_seg) {
		size -= n;

		buf->head_seg = seg->next;
		buf->head_off = 0;

		mm_buffer_segment_destroy(seg);

		seg = buf->head_seg;
		n = seg->size;
	}

	if (seg == buf->tail_seg)
		n -= seg->size - buf->tail_off;
	if (n > size)
		n = size;

	buf->head_off += n;
	size -= n;

leave:
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1, 2)))
mm_buffer_read(struct mm_buffer *buf, void *ptr, size_t size)
{
	ENTER();
	size_t o_size = size;
	char *data = ptr;

	if (unlikely(size == 0))
		goto leave;

	struct mm_buffer_segment *seg = buf->head_seg;
	if (unlikely(seg == NULL))
		goto leave;

	char *p = seg->data + buf->head_off;
	size_t n = seg->size - buf->head_off;
	while (n < size && seg != buf->tail_seg) {
		memcpy(data, p, n);
		data += n;
		size -= n;

		buf->head_seg = seg->next;
		buf->head_off = 0;

		mm_buffer_segment_destroy(seg);

		seg = buf->head_seg;
		p = seg->data;
		n = seg->size;
	}

	if (seg == buf->tail_seg)
		n -= seg->size - buf->tail_off;
	if (n > size)
		n = size;

	memcpy(data, p, n);
	buf->head_off += n;
	size -= n;

leave:
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1, 2)))
mm_buffer_write(struct mm_buffer *buf, const void *ptr, size_t size)
{
	ENTER();
	size_t o_size = size;
	const char *data = ptr;

	if (unlikely(size == 0))
		goto leave;

	struct mm_buffer_segment *seg = mm_buffer_ensure_tail(buf, size);

	char *p = seg->data + buf->tail_off;
	size_t n = seg->size - buf->tail_off;
	while (n < size && seg->next != NULL) {
		memcpy(p, data, n);
		data += n;
		size -= n;

		seg = mm_buffer_ensure_next(seg, size);
		buf->tail_seg = seg;
		buf->tail_off = 0;
		p = seg->data;
		n = seg->size;
	}

	if (n > size)
		n = size;

	memcpy(p, data, n);
	buf->tail_off += n;
	size -= n;

leave:
	LEAVE();
	return (o_size - size);
}

void __attribute__((nonnull(1, 2)))
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va)
{
	ENTER();

	struct mm_buffer_segment *seg = mm_buffer_ensure_tail(buf, 1);
	char *p = seg->data + buf->tail_off;
	size_t n = seg->size - buf->tail_off;

	va_list va2;
	va_copy(va2, va);
	int len = vsnprintf(p, n, fmt, va2);
	va_end(va2);

	if (unlikely(len < 0)) {
		mm_error(errno, "invalid format string");
	} else if ((unsigned) len < n) {
		buf->tail_off += len;
	} else {
		char *ptr = mm_private_alloc(len + 1);
		len = vsnprintf(ptr, len + 1, fmt, va);

		mm_buffer_write(buf, ptr, len);

		mm_private_free(ptr);
	}

	LEAVE();
}

void __attribute__((format(printf, 2, 3))) __attribute__((nonnull(1, 2)))
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(buf, fmt, va);
	va_end(va);
}

void __attribute__((nonnull(1, 2)))
mm_buffer_splice(struct mm_buffer *buf, char *data, size_t size,
		 mm_buffer_release_t release, uintptr_t release_data)
{
	ENTER();

	// Don't bother allocating a new segment for short data,
	// just copy it to the buffer's internal storage.
	if (size < MM_BUFFER_SPLICE_THRESHOLD) {
		mm_buffer_write(buf, data, size);
		if (release != NULL) {
			(*release)(release_data);
		}
		goto leave;
	}

	// Create a new segment and insert it where appropriate.
	struct mm_buffer_segment *seg
		= mm_buffer_segment_create(data, size,
					   release, release_data);
	if (buf->tail_seg == NULL) {
		seg->next = NULL;
		buf->head_seg = seg;
	} else if (buf->tail_off == 0) {
		seg->next = buf->tail_seg;
		if (buf->head_seg == buf->tail_seg) {
			buf->head_seg = seg;
		} else {
			struct mm_buffer_segment *tmp = buf->head_seg;
			while (tmp->next != buf->tail_seg)
				tmp = tmp->next;
			tmp->next = seg;
		}
	} else if (buf->tail_off == buf->tail_seg->size) {
		seg->next = buf->tail_seg->next;
		buf->tail_seg->next = seg;
	} else {
		// Split the in_seg segment into two parts.
		struct mm_buffer_segment *ls, *rs;
		ls = buf->tail_seg;
		rs = mm_buffer_segment_create(ls->data + buf->tail_off,
					      ls->size - buf->tail_off,
					      ls->release, ls->release_data);
		rs->next = ls->next;
		ls->size = buf->tail_off;
		ls->release = NULL;
		ls->release_data = 0;
		seg->next = rs;
		ls->next = seg;
	}
	buf->tail_seg = seg;
	buf->tail_off = size;

leave:
	LEAVE();
}
