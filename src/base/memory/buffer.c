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
 * Buffer size calculation.
 **********************************************************************/

static inline uint32_t
mm_buffer_round_size(uint32_t size)
{
	return mm_round_up(size, MM_BUFFER_SEGMENT_SIZE);
}

static inline uint32_t
mm_buffer_round_room(uint32_t size)
{
	return mm_round_down(size, MM_BUFFER_SEGMENT_SIZE);
}

static inline uint32_t NONNULL(1)
mm_buffer_chunk_size(const struct mm_chunk *chunk)
{
	return mm_buffer_round_room(mm_chunk_getsize(chunk));
}

/**********************************************************************
 * Buffer chunks.
 **********************************************************************/

static inline struct mm_chunk * NONNULL(1)
mm_buffer_create_chunk(struct mm_buffer *buf, size_t size)
{
	// The chunk should have a reasonable size that does not strain
	// the memory allocator.
	if (size < buf->chunk_size)
		size = buf->chunk_size;
	else if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	size = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE);

	// Allocate a memory chunk.
	DEBUG("create a buffer chunk of %zu (%u) bytes", size, buf->chunk_size);
	return mm_chunk_create_private(size);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_append_chunk(struct mm_buffer *buf, struct mm_chunk *chunk)
{
	// Append the chunk to the buffer chunk list.
	mm_chunk_queue_append(&buf->chunks, chunk);

	// Initialize the initial segment.
	struct mm_buffer_segment *seg = mm_buffer_segment_first(chunk);
	seg->meta = mm_buffer_chunk_size(chunk) | MM_BUFFER_SEGMENT_TERMINAL;
	seg->size = 0;

	return seg;
}

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
	if (!mm_buffer_ready(buf))
		mm_buffer_make_ready(buf, area - MM_BUFFER_SEGMENT_SIZE);

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
		free_area = mm_buffer_writer_bump(&buf->tail, buf, area - MM_BUFFER_SEGMENT_SIZE);
		used_area = 0;
	}

	// Check if the current tail segment is the last one in its chunk.
	struct mm_buffer_segment *seg = buf->tail.seg;
	uint32_t flag = seg->meta & MM_BUFFER_SEGMENT_TERMINAL;

	// If the segment is not empty it has to be split in two.
	if (used_area) {
		seg->meta = used_area;
		seg = mm_buffer_segment_next(seg);
		buf->tail.seg = seg;
	}

	// Setup the result segment.
	seg->size = size;
	if (free_area == area) {
		seg->meta = area | type | flag;
		// Move the buffer tail past the result segment.
		mm_buffer_writer_bump(&buf->tail, buf, 0);
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
 * Buffer initialization and termination.
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

	// Release external segments.
	struct mm_chunk *chunk = mm_chunk_queue_head(&buf->chunks);
	for (; chunk != NULL; chunk = mm_chunk_queue_next(chunk)) {
		struct mm_buffer_segment *seg = mm_buffer_segment_first(chunk);
		while (!mm_buffer_segment_terminal(seg)) {
			mm_buffer_segment_release(seg);
			seg = mm_buffer_segment_next(seg);
		}
		mm_buffer_segment_release(seg);
	}

	// Release buffer chunks.
	mm_chunk_destroy_queue(&buf->chunks);

	LEAVE();
}

void NONNULL(1)
mm_buffer_make_ready(struct mm_buffer *buf, size_t size_hint)
{
	ENTER();

	// Create the first buffer chunk.
	ASSERT(!mm_buffer_ready(buf));
	struct mm_chunk *chunk = mm_buffer_create_chunk(buf, size_hint);
	struct mm_buffer_segment *seg = mm_buffer_append_chunk(buf, chunk);

	// Initialize the reader.
	buf->head.chunk = chunk;
	buf->head.seg = seg;
	mm_buffer_reader_reset_ptr(&buf->head);

	// Initialize the writer.
	buf->tail.chunk = chunk;
	buf->tail.seg = seg;

	LEAVE();
}

/**********************************************************************
 * Buffer low-level write routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_buffer_writer_grow(struct mm_buffer_writer *pos, struct mm_buffer *buf, size_t size_hint)
{
	ENTER();

	// Create another buffer chunk.
	ASSERT(mm_buffer_ready(buf));
	struct mm_chunk *chunk = mm_buffer_create_chunk(buf, size_hint);
	struct mm_buffer_segment *seg = mm_buffer_append_chunk(buf, chunk);

	// Update the writer.
	pos->chunk = chunk;
	pos->seg = seg;

	LEAVE();
}

/**********************************************************************
 * Buffer top-level routines.
 **********************************************************************/

size_t NONNULL(1, 2)
mm_buffer_consume(struct mm_buffer *buf, const struct mm_buffer_reader *pos)
{
	ENTER();
	ASSERT(mm_buffer_ready(buf));
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

			// Move the read pointer to the segment start.
			if (buf->head.seg == pos->seg) {
				ASSERT(buf->head.ptr == pos->ptr);
				mm_buffer_reader_reset_ptr(&buf->head);
			}
		} else {
			start->meta += area | flag;

			// Fix up the head and tail iterators if needed.
			if (buf->head.seg == pos->seg) {
				buf->head.seg = start;
				if (buf->tail.seg == pos->seg)
					buf->tail.seg = start;
				// Move the read pointer to the segment start.
				ASSERT(buf->head.ptr == pos->ptr);
				mm_buffer_reader_reset_ptr(&buf->head);
			}
		}

		// Account the segment size.
		consumed += area;
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

size_t NONNULL(1)
mm_buffer_size(struct mm_buffer *buf)
{
	if (!mm_buffer_ready(buf))
		return 0;

	uint32_t size = mm_buffer_reader_end(&buf->head) - mm_buffer_reader_ptr(&buf->head);

	struct mm_buffer_segment *seg = buf->head.seg;
	struct mm_chunk *chunk = buf->head.chunk;
	while (seg != buf->tail.seg) {
		if (mm_buffer_segment_terminal(seg)) {
			chunk = mm_chunk_queue_next(chunk);
			seg = mm_buffer_segment_first(chunk);
		} else {
			seg = mm_buffer_segment_next(seg);
		}
		size += mm_buffer_segment_size(seg);
	}

	return size;
}

size_t NONNULL(1)
mm_buffer_skip(struct mm_buffer *buf, size_t size)
{
	ENTER();

	// Store the original size.
	const size_t orig = size;

	// Check for a viable buffer segment.
	uint32_t n = mm_buffer_reader_ready(buf);

	// Skip data from buffer segments.
	while (n < size && n) {
		buf->head.ptr += n;
		size -= n;

		n = mm_buffer_reader_next(&buf->head, buf);
	}
	if (n) {
		buf->head.ptr += size;
		size = 0;
	}

	LEAVE();
	return (orig - size);
}

size_t NONNULL(1, 2)
mm_buffer_read(struct mm_buffer *buf, void *data, size_t size)
{
	ENTER();

	// Store the original size.
	const size_t orig = size;

	// Check for a viable buffer segment.
	uint32_t n = mm_buffer_reader_ready(buf);

	// Copy data into buffer segments.
	while (n < size && n) {
		memcpy(data, mm_buffer_reader_ptr(&buf->head), n);
		buf->head.ptr += n;
		data += n;
		size -= n;

		n = mm_buffer_reader_next(&buf->head, buf);
	}
	if (n) {
		memcpy(data, mm_buffer_reader_ptr(&buf->head), size);
		buf->head.ptr += size;
		size = 0;
	}

	LEAVE();
	return (orig - size);
}

void NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *data, size_t size)
{
	ENTER();

	// Make sure that there is a viable buffer segment.
	uint32_t n = mm_buffer_writer_make_ready(buf, size);

	// Copy data into buffer segments.
	while (n < size) {
		memcpy(mm_buffer_writer_ptr(&buf->tail), data, n);
		buf->tail.seg->size += n;
		data += n;
		size -= n;

		n = mm_buffer_writer_bump(&buf->tail, buf, size);
	}
	memcpy(mm_buffer_writer_ptr(&buf->tail), data, size);
	buf->tail.seg->size += size;

	LEAVE();
}

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va)
{
	ENTER();

	// Make sure that there is a viable buffer segment.
	uint32_t n = mm_buffer_writer_make_ready(buf, 0);
	char *p = mm_buffer_writer_ptr(&buf->tail);

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
	struct mm_buffer_segment *seg = mm_buffer_segment_insert(buf, MM_BUFFER_SEGMENT_EMBEDDED, area, 0);
	struct mm_buffer_isegment *iseg = (struct mm_buffer_isegment *) seg;

	// Advance the reader if necessary.
	if (buf->head.seg == seg) {
		ASSERT(mm_buffer_segment_embedded(seg));
		mm_buffer_reader_next_unsafe(&buf->head);
		mm_buffer_reader_reset_ptr(&buf->head);
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
		room = mm_buffer_writer_end(&buf->tail) - buf->head.ptr;

	// If the available room is not sufficient then get more advancing
	// the tail segment.
	while (room < size)
		room = mm_buffer_writer_bump(&buf->tail, buf, size);

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
		// the reader.
		buf->head.seg = buf->tail.seg;
		buf->head.chunk = buf->tail.chunk;
		mm_buffer_reader_reset_ptr(&buf->head);
	} else {
		// Get the target address.
		char *data = mm_buffer_segment_data(buf->tail.seg);

		// Shift the tail data.
		if (tail_left)
			memmove(data + rest_left, data, tail_left);

		// Insert the rest of the data.
		while (buf->head.seg != buf->tail.seg) {
			char *p = mm_buffer_reader_ptr(&buf->head);
			size_t n = mm_buffer_reader_end(&buf->head) - p;

			// Copy the current head segment.
			memcpy(data, p, n);
			data += n;

			// Account for the copied data.
			buf->head.seg->size -= n;
			buf->tail.seg->size += n;

			// Proceed to the next segment.
			mm_buffer_reader_next(&buf->head, buf);
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
	char *ptr = mm_buffer_reader_ptr(&buf->head);
	size_t len = mm_buffer_reader_end(&buf->head) - ptr;
	char *ret = memchr(ptr, c, len);

	/* If not found then scan the following segments and merge them as
	   necessary if the char is found there. */
	if (ret == NULL && buf->tail.seg != buf->head.seg) {
		struct mm_buffer_reader reader;
		mm_buffer_reader_save(&reader, buf);

		while (mm_buffer_reader_next(&buf->head, buf)) {
			char *p = mm_buffer_reader_ptr(&buf->head);
			size_t n = mm_buffer_reader_end(&buf->head) - p;
			ret = memchr(p, c, n);
			if (ret != NULL) {
				len += ret - p;
				break;
			}
			len += n;
		}

		mm_buffer_reader_restore(&reader, buf);
		if (ret != NULL) {
			if (mm_buffer_span_slow(buf, len + 1)) {
				mm_error(0, "too long buffer span");
				ret = NULL;
			} else {
				ret = buf->head.ptr + len;
			}
		}
	}

	/* Store the char offset (if found) or the scanned data length (if
	   not found). */
	*poffset = (ret != NULL ? (size_t) (ret - buf->head.ptr) : len);

	LEAVE();
	return ret;
}
