/*
 * base/memory/buffer.c - MainMemory data buffers.
 *
 * Copyright (C) 2013-2016  Aleksey Demakov
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

#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"

#include <stdio.h>

#define MM_BUFFER_MIN_CHUNK_SIZE	(4 * 1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(1024 * 1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_CHUNK_OVERHEAD	(MM_CHUNK_OVERHEAD + MM_BUFFER_SEGMENT_SIZE)

#define MM_BUFFER_SPLICE_THRESHOLD	(128)

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf)
{
	ENTER();

	mm_queue_prepare(&buf->chunks);
	mm_buffer_iterator_prepare(&buf->head);
	mm_buffer_iterator_prepare(&buf->tail);

	LEAVE();
}

void NONNULL(1)
mm_buffer_cleanup(struct mm_buffer *buf)
{
	ENTER();

	// Release external segments.
	struct mm_buffer_iterator iter;
	struct mm_buffer_segment *seg = mm_buffer_iterator_begin(&iter, buf);
	while (seg != NULL) {
		mm_buffer_segment_release(seg);
		seg = mm_buffer_iterator_next(&iter);
	}

	// Release buffer chunks.
	mm_chunk_destroy_queue(&buf->chunks);

	LEAVE();
}

/* Improve space utilization of an empty buffer that was previously in use. */
void NONNULL(1)
mm_buffer_rectify(struct mm_buffer *buf)
{
	ENTER();

	struct mm_buffer_iterator iter;
	struct mm_buffer_segment *start = mm_buffer_iterator_begin(&iter, buf);
	if (start == NULL)
		goto leave;

	struct mm_buffer_segment *last = start;
	while (start != buf->head.seg) {
		// Release an external segment.
		mm_buffer_segment_release(start);

		// Convert the first segment of a chunk to an empty internal
		// segment.
		uint32_t area = mm_buffer_segment_getarea(start);
		start->meta = area | MM_BUFFER_INTERNAL;
		start->used = 0;

		// Combine the chunk segments.
		for (;;) {
			// Proceed to the next segment.
			last = mm_buffer_chunk_next(start);
			if (last == buf->head.seg)
				break;

			// If the chunk end is reached proceed to the next one.
			if (last == iter.sen) {
				struct mm_chunk *chunk = iter.chunk;
				start = mm_buffer_iterator_chunk_start(&iter, mm_chunk_queue_next(iter.chunk));
				last = start;
				// Release the last chunk.
				ASSERT(chunk == mm_chunk_from_qlink(mm_queue_head(&buf->chunks)));
				mm_queue_remove(&buf->chunks);
				mm_chunk_destroy(chunk);
				break;
			}

			// Release an external segment.
			mm_buffer_segment_release(last);

			// Merge this segment with the first one.
			start->meta += mm_buffer_segment_getarea(last);
		}
	}

	// Handle the last consumed segment.
	ASSERT(last == buf->head.seg);
	if (buf->head.ptr == buf->head.end) {
		// Release an external segment.
		mm_buffer_segment_release(last);

		// Merge this segment with the first one.
		uint32_t area = mm_buffer_segment_getarea(last);
		if (start == last) {
			start->meta = area | MM_BUFFER_INTERNAL;
			start->used = 0;
		} else {
			start->meta += area;
			buf->head.seg = start;
		}
		if (buf->tail.seg == last) {
			ASSERT(buf->tail.ptr == buf->head.ptr);
			buf->tail.seg = start;
			mm_buffer_write_start(buf);
		}
		mm_buffer_read_start(buf);
	}

leave:
	LEAVE();
}

struct mm_buffer_segment * NONNULL(1)
mm_buffer_extend(struct mm_buffer *buf, size_t size)
{
	ENTER();

	// The chunk should have a reasonable size that does not put
	// too much pressure on the memory allocator.
	if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	if (size < MM_BUFFER_MIN_CHUNK_SIZE)
		size = MM_BUFFER_MIN_CHUNK_SIZE;
	size = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);

	// Check if the first buffer chunk is to be created.
	bool first = mm_queue_empty(&buf->chunks);

	// Create the buffer chunk.
	DEBUG("create a buffer chunk of %u bytes", (unsigned) size);
	struct mm_chunk *chunk = mm_chunk_create_private(size);
	mm_chunk_queue_append(&buf->chunks, chunk);

	// Initialize the initial segment within the chunk.
	struct mm_buffer_segment *seg = mm_buffer_chunk_begin(chunk);
	size = mm_buffer_round_room(mm_chunk_getsize(chunk));
	seg->meta = size | MM_BUFFER_INTERNAL;
	seg->used = 0;

	// Initialize the head position for the first buffer chunk.
	if (first) {
		mm_buffer_iterator_chunk_start(&buf->head, chunk);
		mm_buffer_read_start(buf);
	}

	// Initialize the tail position.
	mm_buffer_iterator_chunk_start(&buf->tail, chunk);
	mm_buffer_write_start(buf);

	LEAVE();
	return seg;
}

static struct mm_buffer_segment * NONNULL(1)
mm_buffer_insert(struct mm_buffer *buf, mm_buffer_segment_t type, uint32_t size)
{
	struct mm_buffer_segment *seg = buf->tail.seg;
	if (seg == NULL) {
		seg = mm_buffer_extend(buf, size);
	} else {
		ASSERT(mm_buffer_segment_gettype(seg) == MM_BUFFER_INTERNAL);
		uint32_t area = mm_buffer_segment_getarea(seg);
		uint32_t used = seg->used;
		if (used)
			used = mm_buffer_round_size(used + MM_BUFFER_SEGMENT_SIZE);
		if ((size + MM_BUFFER_SEGMENT_SIZE) > (area - used)) {
			seg = mm_buffer_extend(buf, size);
		} else if (used) {
			seg->meta = used | MM_BUFFER_INTERNAL;
			seg = (struct mm_buffer_segment *) (((char *) seg) + used);
			seg->meta = (area - used) | MM_BUFFER_INTERNAL;
			seg->used = 0;
		}
	}

	struct mm_buffer_segment *res = seg;

	uint32_t area = mm_buffer_segment_getarea(seg);
	uint32_t used = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);
	if (area <= (used + MM_BUFFER_SEGMENT_SIZE)) {
		seg->meta = area | type;
		mm_buffer_extend(buf, 0);
	} else {
		seg->meta = used | type;
		seg = (struct mm_buffer_segment *) (((char *) seg) + used);
		seg->meta = (area - used) | MM_BUFFER_INTERNAL;
		seg->used = 0;

		buf->tail.seg = seg;
		mm_buffer_write_start(buf);
	}

	return res;
}

size_t NONNULL(1)
mm_buffer_getsize(struct mm_buffer *buf)
{
	size_t size = 0;
	struct mm_chunk *chunk = mm_chunk_queue_head(&buf->chunks);
	while (chunk != NULL) {
		size += mm_buffer_chunk_getsize(chunk);
		chunk = mm_chunk_queue_next(chunk);
	}
	return size;
}

size_t NONNULL(1)
mm_buffer_getarea(struct mm_buffer *buf)
{
	size_t size = 0;
	struct mm_buffer_iterator iter;
	struct mm_buffer_segment *seg = mm_buffer_iterator_begin(&iter, buf);
	while (seg != NULL) {
		size += mm_buffer_segment_getarea(seg);
		seg = mm_buffer_iterator_next(&iter);
	}
	return size;
}

size_t NONNULL(1)
mm_buffer_getleft(struct mm_buffer *buf)
{
	size_t size = buf->head.end - buf->head.ptr;
	struct mm_buffer_iterator iter = buf->head;
	while (mm_buffer_iterator_next(&iter))
		size += mm_buffer_segment_getused(iter.seg);
	return size;
}

size_t NONNULL(1)
mm_buffer_fill(struct mm_buffer *buf, size_t cnt)
{
	ENTER();

	size_t left = cnt;
	struct mm_buffer_iterator *iter = &buf->tail;

	for (;;) {
		char *p = iter->ptr;
		size_t n = iter->end - p;
		if (n >= left) {
			iter->seg->used += left;
			iter->ptr += left;
			left = 0;
			break;
		}

		if (unlikely(p == NULL)) {
			mm_buffer_extend(buf, left);
			continue;
		}

		iter->seg->used += n;
		iter->ptr += n;
		left -= n;

		mm_buffer_write_next(buf, left);
	}

	mm_buffer_read_reset(buf);

	LEAVE();
	return (cnt - left);
}

size_t NONNULL(1)
mm_buffer_flush(struct mm_buffer *buf, size_t cnt)
{
	ENTER();

	size_t left = cnt;
	struct mm_buffer_iterator *iter = &buf->head;

	for (;;) {
		char *p = iter->ptr;
		size_t n = iter->end - p;
		if (n >= left) {
			iter->ptr += left;
			left = 0;
			break;
		}

		if (unlikely(p == NULL))
			break;

		iter->ptr += n;
		left -= n;

		if (!mm_buffer_read_next(buf))
			break;
	}

	LEAVE();
	return (cnt - left);
}

size_t NONNULL(1, 2)
mm_buffer_read(struct mm_buffer *buf, void *ptr, size_t cnt)
{
	ENTER();

	size_t left = cnt;
	char *data = ptr;
	struct mm_buffer_iterator *iter = &buf->head;

	for (;;) {
		char *p = iter->ptr;
		size_t n = iter->end - p;
		if (n >= left) {
			iter->ptr += left;
			memcpy(data, p, left);
			left = 0;
			break;
		}

		if (unlikely(p == NULL))
			break;

		iter->ptr += n;
		memcpy(data, p, n);
		data += n;
		left -= n;

		if (!mm_buffer_read_next(buf))
			break;
	}

	LEAVE();
	return (cnt - left);
}

size_t NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *ptr, size_t cnt)
{
	ENTER();

	size_t left = cnt;
	const char *data = ptr;
	struct mm_buffer_iterator *iter = &buf->tail;

	for (;;) {
		char *p = iter->ptr;
		size_t n = iter->end - p;
		if (n >= left) {
			iter->seg->used += left;
			iter->ptr += left;
			memcpy(p, data, left);
			left = 0;
			break;
		}

		if (unlikely(p == NULL)) {
			mm_buffer_extend(buf, left);
			continue;
		}

		iter->seg->used += n;
		iter->ptr += n;
		memcpy(p, data, n);
		data += n;
		left -= n;

		mm_buffer_write_next(buf, left);
	}

	mm_buffer_read_reset(buf);

	LEAVE();
	return (cnt - left);
}

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va)
{
	ENTER();

	struct mm_buffer_iterator *iter = &buf->tail;
	char *p = iter->ptr;
	size_t n = iter->end - p;

	va_list va2;
	va_copy(va2, va);
	int len = vsnprintf(p, n, fmt, va2);
	va_end(va2);

	if (unlikely(len < 0)) {
		mm_error(errno, "invalid format string");
	} else if ((unsigned) len < n) {
		iter->seg->used += len;
		iter->ptr += len;
	} else {
		char *ptr = mm_private_alloc(len + 1);
		len = vsnprintf(ptr, len + 1, fmt, va);
		mm_buffer_write(buf, ptr, len);
		mm_private_free(ptr);
	}

	LEAVE();
}

void NONNULL(1, 2) FORMAT(2, 3)
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(buf, fmt, va);
	va_end(va);
}

void NONNULL(1, 2)
mm_buffer_splice(struct mm_buffer *buf, char *data, uint32_t size, uint32_t used,
		 mm_buffer_release_t release, uintptr_t release_data)
{
	ENTER();

	// Don't bother allocating a new segment for short data, just copy
	// it to the buffer's internal storage.
	if (size < MM_BUFFER_SPLICE_THRESHOLD) {
		mm_buffer_write(buf, data, size);
		if (release != NULL)
			(*release)(release_data);
		goto leave;
	}

	uint32_t area = sizeof(struct mm_buffer_xsegment) - MM_BUFFER_SEGMENT_SIZE;
	struct mm_buffer_segment *seg = mm_buffer_insert(buf, MM_BUFFER_EXTERNAL, area);
	struct mm_buffer_xsegment *xseg = (struct mm_buffer_xsegment *) seg;

	xseg->data = data;
	xseg->size = size;
	xseg->used = used;
	xseg->release = release;
	xseg->release_data = release_data;

leave:
	LEAVE();
}

void * NONNULL(1)
mm_buffer_embed(struct mm_buffer *buf, uint32_t size)
{
	ENTER();
	ASSERT(size <= (UINT32_MAX - MM_BUFFER_SEGMENT_SIZE));

	struct mm_buffer_segment *seg = mm_buffer_insert(buf, MM_BUFFER_EMBEDDED, size);
	struct mm_buffer_isegment *iseg = (struct mm_buffer_isegment *) seg;

	LEAVE();
	return iseg->data;
}
