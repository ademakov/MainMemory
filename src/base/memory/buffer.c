/*
 * base/memory/buffer.c - MainMemory data buffers.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#include "base/memory/memory.h"

#include <stdio.h>

#define MM_BUFFER_SPLICE_THRESHOLD	(128)

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

/* Release an external segment. */
static void NONNULL(1)
mm_buffer_segment_release(struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_external(seg)) {
		struct mm_buffer_xsegment *xseg = (struct mm_buffer_xsegment *) seg;
		if (xseg->release != NULL)
			(*xseg->release)(xseg->release_data);
	}
}

/* Insert a new segment at the current write position. */
static struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_insert(struct mm_buffer *buf, uint32_t type, uint32_t area, uint32_t size)
{
	ENTER();

	// Make sure that there is a viable buffer segment.
	if (!mm_buffer_valid(buf))
		mm_buffer_extend(buf, &buf->tail, area - MM_BUFFER_SEGMENT_SIZE);

	// Find out the available room in the current tail segment and the
	// area that is in use.
	uint32_t free_area = mm_buffer_segment_area(buf->tail.seg);
	uint32_t used_area = mm_buffer_segment_size(buf->tail.seg);
	if (used_area) {
		used_area += MM_BUFFER_SEGMENT_SIZE;
		used_area = mm_buffer_round_size(used_area);
		free_area -= used_area;
	}

	// If the available room is not sufficient then get more advancing
	// the tail segment to a next segment and allocating it if needed.
	while (free_area < area) {
		mm_buffer_write_more(buf, &buf->tail, area - MM_BUFFER_SEGMENT_SIZE);
		free_area = mm_buffer_segment_area(buf->tail.seg);
		used_area = 0;
	}

	// Check if the current tail segment is the last one in its chunk.
	struct mm_buffer_segment *seg = buf->tail.seg;
	uint32_t flag = seg->meta & MM_BUFFER_SEGMENT_TERMINAL;

	// If the segment is not empty it has to be split in two.
	if (used_area) {
		seg->meta = used_area;
		buf->tail.seg = mm_buffer_segment_next(seg);
		seg = buf->tail.seg;
	}

	// Setup the result segment.
	seg->size = size;
	if (free_area == area) {
		seg->meta = area | type | flag;
		// Move the buffer tail past the result segment.
		mm_buffer_write_more(buf, &buf->tail, 0);
	} else {
		seg->meta = area | type;
		// Move the buffer tail past the result segment.
		buf->tail.seg = mm_buffer_segment_next(seg);
		buf->tail.seg->meta = (free_area - area) | flag;
		buf->tail.seg->size = 0;
	}

	LEAVE();
	return seg;
}

/**********************************************************************
 * Buffer top-level routines.
 **********************************************************************/

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf, size_t chunk_size)
{
	ENTER();

	// Initialize the chunk list.
	mm_queue_prepare(&buf->chunks);

	// Initialize the read iterator.
	buf->head.chunk = NULL;
	buf->head.seg = NULL;
	buf->head.ptr = NULL;

	// Initialize the write iterator.
	buf->tail.chunk = NULL;
	buf->tail.seg = NULL;

	// Figure out the minimum chunk size.
	if (chunk_size == 0)
		chunk_size = MM_BUFFER_MIN_CHUNK_SIZE;
	else if (chunk_size > MM_BUFFER_MAX_CHUNK_SIZE)
		chunk_size = MM_BUFFER_MAX_CHUNK_SIZE;
	buf->chunk_size = chunk_size;

	LEAVE();
}

void NONNULL(1)
mm_buffer_cleanup(struct mm_buffer *buf)
{
	ENTER();

	struct mm_chunk *chunk = mm_chunk_queue_head(&buf->chunks);
	if (chunk == NULL)
		goto leave;

	// Release external segments.
	struct mm_buffer_reader iter;
	struct mm_buffer_segment *seg = mm_buffer_reader_setup(&iter, chunk);
	while (seg != NULL) {
		mm_buffer_segment_release(seg);
		seg = mm_buffer_reader_next(&iter);
	}

	// Release buffer chunks.
	mm_chunk_destroy_queue(&buf->chunks);

leave:
	LEAVE();
}

size_t NONNULL(1, 2)
mm_buffer_consume(struct mm_buffer *buf, const struct mm_buffer_reader *pos)
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
		consumed += mm_buffer_chunk_size(chunk);

		// Release external segments.
		struct mm_buffer_segment *seg = mm_buffer_segment_first(chunk);
		while (!mm_buffer_segment_terminal(seg)) {
			mm_buffer_segment_release(seg);
			seg = mm_buffer_segment_next(seg);
		}
		mm_buffer_segment_release(seg);

		// Destroy the chunk.
		mm_queue_remove(&buf->chunks);
		mm_chunk_destroy(chunk);
	}

	// Consume the segments that precede the given position.
	struct mm_buffer_segment *start = mm_buffer_segment_first(pos->chunk);
	if (start != pos->seg) {
		// Release an external segment.
		mm_buffer_segment_release(start);

		// Convert the first segment of a chunk to an empty internal
		// segment.
		start->meta = mm_buffer_segment_area(start);
		start->size = 0;

		// Combine the chunk segments.
		for (;;) {
			// Proceed to the next segment.
			struct mm_buffer_segment *seg = mm_buffer_segment_next(start);
			if (seg == pos->seg)
				break;

			// Release an external segment.
			mm_buffer_segment_release(seg);

			// Merge this segment with the first one.
			start->meta += mm_buffer_segment_area(seg);
		}

		// Account the segment size.
		consumed += start->meta;
	}

	// Handle the last consumed segment.
	char *data = mm_buffer_segment_data(pos->seg);
	char *data_end = data + mm_buffer_segment_size(pos->seg);
	if (data_end == pos->ptr) {
		// Release an external segment.
		mm_buffer_segment_release(pos->seg);

		// Merge this segment with the first one.
		uint32_t area = mm_buffer_segment_area(pos->seg);
		uint32_t flag = pos->seg->meta & MM_BUFFER_SEGMENT_TERMINAL;
		if (start == pos->seg) {
			start->meta = area | flag;
			start->size = 0;
		} else {
			start->meta += area | flag;
		}

		// Account the segment size.
		consumed += area;

		// Fix up the head and tail iterators if needed.
		if (buf->head.seg == pos->seg) {
			ASSERT(buf->head.ptr == pos->ptr);
			buf->head.seg = start;
			mm_buffer_reader_read_start(&buf->head);
			if (buf->tail.seg == pos->seg) {
				ASSERT(buf->tail.ptr == pos->ptr);
				buf->tail.seg = start;
			}
		}
	}

	// Remember the maximum consumed data size to optimize later
	// buffer chunk allocation.
	if (buf->chunk_size < consumed) {
		if (consumed > MM_BUFFER_MAX_CHUNK_SIZE)
			consumed = MM_BUFFER_MAX_CHUNK_SIZE;
		buf->chunk_size = consumed;
	}

	LEAVE();
	return consumed;
}

/* Improve space utilization of an empty buffer that was previously in use. */
void NONNULL(1)
mm_buffer_compact(struct mm_buffer *buf)
{
	ENTER();

	if (!mm_buffer_valid(buf))
		goto leave;

	// Get the last read position.
	struct mm_buffer_reader pos = buf->head;

	// Consume everything up to it.
	mm_buffer_consume(buf, &pos);

leave:
	LEAVE();
}

struct mm_buffer_segment * NONNULL(1, 2)
mm_buffer_extend(struct mm_buffer *buf, struct mm_buffer_writer *iter, size_t size)
{
	ENTER();

	// The chunk should have a reasonable size that does not strain
	// the memory allocator.
	if (size < buf->chunk_size)
		size = buf->chunk_size;
	else if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	size = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);

	// Check if the first buffer chunk is to be created.
	bool first = mm_queue_empty(&buf->chunks);

	// Create the buffer chunk.
	DEBUG("create a buffer chunk of %zu (%zu) bytes", size, buf->consumed_max);
	struct mm_chunk *chunk = mm_chunk_create_private(size);
	mm_chunk_queue_append(&buf->chunks, chunk);

	// Initialize the initial segment within the chunk.
	struct mm_buffer_segment *seg = mm_buffer_segment_first(chunk);
	seg->meta = mm_buffer_chunk_size(chunk) | MM_BUFFER_SEGMENT_TERMINAL;
	seg->size = 0;

	// Initialize the head and tail iterators for the first buffer chunk.
	if (first) {
		mm_buffer_reader_setup(&buf->head, chunk);
		mm_buffer_reader_read_start(&buf->head);
		if (&buf->tail != iter)
			mm_buffer_writer_begin(&buf->tail, chunk);
	}

	// Initialize the tail position.
	mm_buffer_writer_begin(iter, chunk);

	LEAVE();
	return seg;
}

size_t NONNULL(1)
mm_buffer_size(struct mm_buffer *buf)
{
	size_t size = 0;
	if (buf->head.ptr) {
		size = mm_buffer_reader_end(&buf->head) - buf->head.ptr;
		if (buf->tail.seg != buf->head.seg) {
			struct mm_buffer_reader iter = buf->head;
			while (mm_buffer_reader_filter_next(&iter) != NULL)
				size += mm_buffer_segment_size(iter.seg);
		}
	}
	return size;
}

size_t NONNULL(1)
mm_buffer_flush(struct mm_buffer *buf, size_t cnt)
{
	ENTER();

	size_t left = cnt;
	struct mm_buffer_reader *iter = &buf->head;

	for (;;) {
		char *p = iter->ptr;
		size_t n = mm_buffer_reader_end(iter) - p;
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
	struct mm_buffer_reader *iter = &buf->head;

	for (;;) {
		char *p = iter->ptr;
		size_t n = mm_buffer_reader_end(iter) - p;
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

void NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *data, size_t size)
{
	ENTER();

	// Make sure that there is a viable buffer segment.
	uint32_t n = mm_buffer_write_start(buf, size);

	// Copy data into buffer segments.
	while (n < size) {
		memcpy(mm_buffer_write_ptr(buf), data, n);
		buf->tail.seg->size += n;

		data += n;
		size -= n;

		n = mm_buffer_write_more(buf, &buf->tail, size);
	}
	memcpy(mm_buffer_write_ptr(buf), data, size);
	buf->tail.seg->size += size;

	LEAVE();
}

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va)
{
	ENTER();

	// Make sure that there is a viable buffer segment.
	uint32_t n = mm_buffer_write_start(buf, 0);
	char *p = mm_buffer_writer_data(&buf->tail);

	// Try to print into the available segment.
	va_list va2;
	va_copy(va2, va);
	int len = vsnprintf(p, n, fmt, va2);
	va_end(va2);

	if (unlikely(len < 0)) {
		mm_error(errno, "invalid format string");
	} else if ((unsigned) len < n) {
		// Success. Bump the data size.
		buf->tail.seg->size += len;
	} else {
		// It does not fit into the available segment. Use an
		// intermediate buffer.
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
mm_buffer_splice(struct mm_buffer *buf, char *data, uint32_t size,
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

	uint32_t area = mm_buffer_round_size(sizeof(struct mm_buffer_xsegment));
	struct mm_buffer_segment *seg = mm_buffer_segment_insert(buf, MM_BUFFER_SEGMENT_EXTERNAL, area, size);
	struct mm_buffer_xsegment *xseg = (struct mm_buffer_xsegment *) seg;

	xseg->data = data;
	xseg->room = size;
	xseg->release = release;
	xseg->release_data = release_data;

leave:
	LEAVE();
}

void * NONNULL(1)
mm_buffer_embed(struct mm_buffer *buf, uint32_t size)
{
	ENTER();

	// Check to see if the requested size is not too large.
	VERIFY(size <= MM_BUFFER_MAX_CHUNK_SIZE);

	// Create the required embedded segment at the buffer tail.
	uint32_t area = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);
	struct mm_buffer_segment *seg = mm_buffer_segment_insert(buf, MM_BUFFER_SEGMENT_EMBEDDED, area, size);
	struct mm_buffer_isegment *iseg = (struct mm_buffer_isegment *) seg;

	// Advance the read iterator if necessary.
	if (buf->head.seg == seg) {
		ASSERT(mm_buffer_segment_embedded(seg));
		mm_buffer_reader_try_next_unsafe(&buf->head);
	}

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
	size_t left = mm_buffer_size(buf);
	// TODO: Have more than one target chunks.
	if (unlikely(left > MM_BUFFER_MAX_CHUNK_SIZE))
		mm_fatal(0, "not implemented yet");

	// Find out how much room is required to fit the data.
	size_t size = max(left, cnt);

	// Find out how much room is available at the buffer tail.
	size_t room;
	if (buf->tail.seg != buf->head.seg)
		room = mm_buffer_segment_room(buf->tail.seg);
	else
		room = mm_buffer_writer_data_end(&buf->tail) - buf->head.ptr;

	// If the available room is not sufficient then get more advancing
	// the tail segment.
	while (room < size)
		room = mm_buffer_write_more(buf, &buf->tail, size);

	// The current head segment will do.
	if (buf->tail.seg == buf->head.seg)
		goto leave;

	// Consolidate the entire unread data in the tail segment.
	// If the original tail segment is not empty and at the same
	// time is large enough to fit the entire data then the tail
	// data have to be shifted towards the end of the segment.
	// The rest of the data have to be inserted just before it.
	size_t tail_left = mm_buffer_segment_size(buf->tail.seg);
	size_t rest_left = left - tail_left;
	if (rest_left == 0) {
		// There is no actual data to insert so just advance
		// the head iterator.
		while (buf->head.seg != buf->tail.seg)
			mm_buffer_reader_try_next_unsafe(&buf->head);
	} else {
		// Get the target address.
		char *data = mm_buffer_segment_data(buf->tail.seg);

		// Shift the tail data.
		if (tail_left)
			memmove(data + rest_left, data, tail_left);

		// Insert the rest of the data.
		while (buf->head.seg != buf->tail.seg) {
			char *p = buf->head.ptr;
			size_t n = mm_buffer_reader_end(&buf->head) - p;

			// Copy the current head segment.
			memcpy(data, p, n);
			data += n;

			// Account for the copied data.
			buf->head.seg->size -= n;
			buf->tail.seg->size += n;

			// Proceed to the next segment.
			mm_buffer_reader_try_next_unsafe(&buf->head);
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
	size_t len = mm_buffer_reader_end(&buf->head) - ptr;
	char *ret = memchr(ptr, c, len);

	/* If not found then scan the following segments and merge
	   them as necessary if the char is found there. */
	if (ret == NULL && buf->tail.seg != buf->head.seg) {
		struct mm_buffer_reader iter = buf->head;
		while (mm_buffer_reader_try_next_unsafe(&iter)) {
			size_t n = mm_buffer_reader_end(&iter) - iter.ptr;
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
