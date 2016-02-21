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

#define MM_BUFFER_MIN_CHUNK_SIZE	(1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(4 * 1024 * 1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_CHUNK_OVERHEAD	(MM_CHUNK_OVERHEAD + MM_BUFFER_SEGMENT_SIZE)

#define MM_BUFFER_SPLICE_THRESHOLD	(128)

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf)
{
	ENTER();

	mm_queue_prepare(&buf->chunks);
	mm_buffer_iterator_prepare(&buf->head);
	mm_buffer_iterator_prepare(&buf->tail);
	buf->consumed_max = 0;

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

size_t NONNULL(1, 2)
mm_buffer_consume(struct mm_buffer *buf, const struct mm_buffer_position *pos)
{
	ENTER();
	ASSERT(mm_buffer_valid(buf));
	size_t consumed = 0;

	// Consume the chunks that precede the given position.
	for (;;) {
		struct mm_chunk *chunk = mm_chunk_queue_head(&buf->chunks);
		if (chunk == pos->chunk)
			break;

		// Account the chunk size.
		consumed += mm_buffer_chunk_getsize(chunk);

		// Release external segments.
		struct mm_buffer_segment *seg = mm_buffer_chunk_begin(chunk);
		struct mm_buffer_segment *sen = mm_buffer_chunk_end(chunk);
		while (seg != sen) {
			mm_buffer_segment_release(seg);
			seg = mm_buffer_chunk_next(seg);
		}

		// Destroy the chunk.
		mm_queue_remove(&buf->chunks);
		mm_chunk_destroy(chunk);
	}

	// Consume the segments that precede the given position.
	struct mm_buffer_segment *start = mm_buffer_chunk_begin(pos->chunk);
	if (start != pos->seg) {
		// Release an external segment.
		mm_buffer_segment_release(start);

		// Convert the first segment of a chunk to an empty internal
		// segment.
		uint32_t area = mm_buffer_segment_getarea(start);
		start->meta = area | MM_BUFFER_INTERNAL;
		start->used = 0;

		// Account the segment size.
		consumed += area;

		// Combine the chunk segments.
		for (;;) {
			// Proceed to the next segment.
			struct mm_buffer_segment *seg = mm_buffer_chunk_next(start);
			if (seg == pos->seg)
				break;

			// Release an external segment.
			mm_buffer_segment_release(seg);

			// Merge this segment with the first one.
			uint32_t area = mm_buffer_segment_getarea(start);
			start->meta += area;

			// Account the segment size.
			consumed += area;
		}
	}

	// Handle the last consumed segment.
	char *data = mm_buffer_segment_getdata(pos->seg);
	char *data_end = data + mm_buffer_segment_getused(pos->seg);
	if (data_end == pos->ptr) {
		// Release an external segment.
		mm_buffer_segment_release(pos->seg);

		// Merge this segment with the first one.
		uint32_t area = mm_buffer_segment_getarea(start);
		if (start == pos->seg) {
			start->meta = area | MM_BUFFER_INTERNAL;
			start->used = 0;
		} else {
			start->meta += area;
		}

		// Account the segment size.
		consumed += area;

		// Fix up the head and tail iterators if needed.
		if (buf->head.seg == pos->seg) {
			ASSERT(buf->head.ptr == pos->ptr);
			buf->head.seg = start;
			mm_buffer_iterator_read_start(&buf->head);
			if (buf->tail.seg == pos->seg) {
				ASSERT(buf->tail.ptr == pos->ptr);
				buf->tail.seg = start;
				mm_buffer_iterator_write_start(&buf->tail);
			}
		}
	}

	// Remember the maximum consumed data size to optimize later
	// buffer chunk allocation.
	if (buf->consumed_max < consumed)
		buf->consumed_max = consumed;

	LEAVE();
	return consumed;
}

/* Improve space utilization of an empty buffer that was previously in use. */
void NONNULL(1)
mm_buffer_rectify(struct mm_buffer *buf)
{
	ENTER();

	if (!mm_buffer_valid(buf))
		goto leave;

	// Get the last read position.
	struct mm_buffer_position pos;
	mm_buffer_position_save(&pos, buf);

	// Consume everything up to it.
	mm_buffer_consume(buf, &pos);

leave:
	LEAVE();
}

struct mm_buffer_segment * NONNULL(1)
mm_buffer_extend(struct mm_buffer *buf, size_t size_hint)
{
	ENTER();

	// The chunk should have a reasonable size that does not put
	// too much pressure on the memory allocator.
	size_t size = max(size_hint, buf->consumed_max);
	if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	if (size < MM_BUFFER_MIN_CHUNK_SIZE)
		size = MM_BUFFER_MIN_CHUNK_SIZE;
	size = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);

	// Check if the first buffer chunk is to be created.
	bool first = mm_queue_empty(&buf->chunks);

	// Create the buffer chunk.
	DEBUG("create a buffer chunk of %zu (%zu) bytes", size, buf->consumed_max);
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
		mm_buffer_iterator_read_start(&buf->head);
	}

	// Initialize the tail position.
	mm_buffer_iterator_chunk_start(&buf->tail, chunk);
	mm_buffer_iterator_write_start(&buf->tail);

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
		mm_buffer_iterator_write_start(&buf->tail);
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
	if (buf->tail.seg != buf->head.seg) {
		struct mm_buffer_iterator iter = buf->head;
		while (mm_buffer_iterator_filter_next(&iter) != NULL)
			size += mm_buffer_segment_getused(iter.seg);
	}
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

		if (likely(n != 0)) {
			iter->seg->used += n;
			left -= n;
		}

		mm_buffer_write_next(buf, left);
	}

	mm_buffer_update(buf);

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

		left -= n;

		if (!mm_buffer_read_next(buf)) {
			iter->ptr += n;
			break;
		}
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

		memcpy(data, p, n);
		data += n;
		left -= n;

		if (!mm_buffer_read_next(buf)) {
			iter->ptr += n;
			break;
		}
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

		if (likely(n != 0)) {
			iter->seg->used += n;
			memcpy(p, data, n);
			data += n;
			left -= n;
		}

		mm_buffer_write_next(buf, left);
	}

	mm_buffer_update(buf);

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

/**********************************************************************
 * Buffer in-place parsing support.
 **********************************************************************/

bool NONNULL(1)
mm_buffer_span_slow(struct mm_buffer *buf, size_t cnt)
{
	ENTER();
	bool rc = true;

	// Check to see if the requested span is too large.
	if (unlikely(cnt > MM_BUFFER_MAX_CHUNK_SIZE)) {
		rc = false;
		goto leave;
	}

	// Find out how much data is in the buffer.
	size_t left = mm_buffer_getleft(buf);

	// Find out how much room is required to fit the data.
	size_t size = max(left, cnt);

	// Find out how much room is available an the buffer tail.
	size_t room;
	if (buf->tail.seg == buf->head.seg)
		room = buf->tail.end - buf->head.ptr;
	else
		room = mm_buffer_segment_getsize(buf->tail.seg);

	// If the available room is not sufficient then get more
	// advancing the tail segment.
	while (room < size) {
		mm_buffer_write_next(buf, size);
		room = mm_buffer_segment_getsize(buf->tail.seg);
	}

	// The current head segment will do.
	if (buf->tail.seg == buf->head.seg)
		goto leave;

	// TODO: Have more than one target chunks.
	if (unlikely(left > MM_BUFFER_MAX_CHUNK_SIZE))
		mm_fatal(0, "not implemented yet");

	// Consolidate the entire unread data it the tail segment.
	// If the original tail segment is not empty and at the same
	// time is large enough to fit the entire data then the tail
	// data have to be shifted towards the end of the segment.
	// The rest of the data have to be inserted just before it.
	size_t tail_left = mm_buffer_segment_getused(buf->tail.seg);
	size_t rest_left = left - tail_left;
	if (rest_left == 0) {
		// There is no actual data to insert so just advance
		// the head iterator.
		while (buf->head.seg != buf->tail.seg)
			mm_buffer_iterator_read_next(&buf->head);
	} else {
		// Get the target address.
		char *data = mm_buffer_segment_getdata(buf->tail.seg);

		// Shift the tail data.
		if (tail_left)
			memmove(data + rest_left, data, tail_left);

		// Advance the tail position accordingly.
		buf->tail.ptr += rest_left;

		// Insert the rest of the data.
		while (buf->head.seg != buf->tail.seg) {
			char *p = buf->head.ptr;
			size_t n = buf->head.end - p;

			// Copy the current head segment.
			memcpy(data, p, n);
			data += n;

			// Account for the copied data.
			buf->head.seg->used -= n;
			buf->tail.seg->used += n;

			// Proceed to the next segment.
			mm_buffer_iterator_read_next(&buf->head);
		}
	}

leave:
	LEAVE();
	return rc;
}

char * NONNULL(1, 3)
mm_buffer_find(struct mm_buffer *buf, int c, size_t *poffset)
{
	ENTER();

	/* Seek the given char in the current segment. */
	char *ptr = buf->head.ptr;
	size_t len = buf->head.end - ptr;
	char *ret = memchr(ptr, c, len);

	/* If not found then scan the following segments and merge
	   them as necessary if the char is found there. */
	if (ret == NULL && buf->tail.seg != buf->head.seg) {
		struct mm_buffer_iterator iter = buf->head;
		while (mm_buffer_iterator_read_next(&iter)) {
			size_t n = iter.end - iter.ptr;
			char *p = memchr(iter.ptr, c, n);
			if (p != NULL) {
				len += p - iter.ptr;
				if (!mm_buffer_span_slow(buf, len + 1))
					mm_error(0, "too long buffer span");
				else
					ret = buf->head.ptr + len;
				break;
			}
			len += n;
		}
	}

	/* Store the char offset (if found) or the scanned data length (if
	   not found). */
	*poffset = (ret != NULL ? (size_t) (ret - buf->head.ptr) : len);

	LEAVE();
	return ret;
}
