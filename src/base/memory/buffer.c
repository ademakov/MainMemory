/*
 * base/memory/buffer.c - MainMemory data buffers.
 *
 * Copyright (C) 2013-2018  Aleksey Demakov
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
mm_buffer_chunk_area(const struct mm_chunk *chunk)
{
	uint32_t room = mm_chunk_getsize(chunk) - MM_BUFFER_TERMINAL_SIZE;
	return mm_buffer_round_room(room);
}

static inline struct mm_buffer_tsegment * NONNULL(1)
mm_buffer_chunk_tseg(const struct mm_chunk *chunk, uint32_t chunk_area)
{
	return (struct mm_buffer_tsegment *) (chunk->data + chunk_area);
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
	DEBUG("insert buffer segement: type=%d area=%u size=%u", type, area, size);

	// Get the current tail segment.
	struct mm_buffer_segment *seg = buf->tail.seg;

	// Find out the available room in the current tail segment and the area
	// that is in use.
	uint32_t free_area = mm_buffer_segment_area(seg);
	uint32_t used_area = mm_buffer_segment_size(seg);
	if (used_area) {
		used_area = mm_buffer_round_size(used_area + MM_BUFFER_SEGMENT_SIZE);
		free_area -= used_area;
	}

	// Determine the segment location.
	if (free_area < area) {
		// If the available room is not sufficient then get more advancing
		// the tail segment to a next segment and allocating it if needed.
		used_area = 0;
		do {
			free_area = mm_buffer_writer_bump(&buf->tail, buf, area - MM_BUFFER_SEGMENT_SIZE);
			free_area += MM_BUFFER_SEGMENT_SIZE;
		} while (free_area < area);
		seg = buf->tail.seg;
	} else if (used_area) {
		// If the segment is not empty it has to be split in two.
		DEBUG("shorten segment %p: %u -> %u", seg, seg->meta, used_area);
		seg->meta = used_area;
		seg = mm_buffer_segment_adjacent_next(seg);
		buf->tail.seg = seg;
	}

	// Setup the result segment.
	DEBUG("setup extra segment %p: %u", seg, area);
	seg->meta = area | type;
	seg->size = size;
	// Move the buffer tail past the result segment.
	buf->tail.seg = mm_buffer_segment_adjacent_next(seg);
	if (free_area > area) {
		DEBUG("setup spare segment %p: %u", buf->tail.seg, free_area - area);
		buf->tail.seg->meta = free_area - area;
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

	// Initialize the pseudo-segment.
	buf->stub.base.meta = MM_BUFFER_SEGMENT_SIZE | MM_BUFFER_SEGMENT_TERMINAL;
	buf->stub.base.size = 0;
	buf->stub.next = NULL;

	// Initialize the read iterator.
	mm_buffer_reader_set(&buf->head, &buf->stub.base);
	// Initialize the write iterator.
	buf->tail.seg = &buf->stub.base;

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
		do {
			mm_buffer_segment_release(seg);
			seg = mm_buffer_segment_adjacent_next(seg);
		} while (!mm_buffer_segment_terminal(seg));
	}

	// Release buffer chunks.
	mm_chunk_destroy_queue(&buf->chunks);

	LEAVE();
}

/**********************************************************************
 * Buffer low-level write routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_buffer_writer_grow(struct mm_buffer_writer *pos, struct mm_buffer *buf, size_t size)
{
	ENTER();

	// The chunk should have a reasonable size that does not stress
	// the memory allocator.
	if (size < buf->chunk_size)
		size = buf->chunk_size;
	else if (size > MM_BUFFER_MAX_CHUNK_SIZE)
		size = MM_BUFFER_MAX_CHUNK_SIZE;
	size = mm_buffer_round_size(size + MM_BUFFER_SEGMENT_SIZE) + MM_BUFFER_TERMINAL_SIZE;

	DEBUG("create a buffer chunk of %zu (min %u) bytes", size, buf->chunk_size);

	// Allocate a memory chunk.
	struct mm_chunk *chunk = mm_chunk_create_private(size);
	// Append the chunk to the buffer chunk list.
	mm_chunk_queue_append(&buf->chunks, chunk);

	// Initialize the initial segment.
	struct mm_buffer_segment *seg = mm_buffer_segment_first(chunk);
	seg->meta = mm_buffer_chunk_area(chunk);
	seg->size = 0;
	DEBUG("setup initial segment %p: %u", seg, seg->meta);

	// Initialize the new terminal segment. It has no room for data.
	struct mm_buffer_tsegment *tseg = mm_buffer_chunk_tseg(chunk, seg->meta);
	tseg->base.meta = MM_BUFFER_SEGMENT_SIZE | MM_BUFFER_SEGMENT_TERMINAL;
	tseg->base.size = 0;
	tseg->next = NULL;
	DEBUG("setup terminal segment %p", tseg);

	// Link the initial segment with the previous terminal segmant.
	VERIFY(mm_buffer_segment_terminal(pos->seg));
	((struct mm_buffer_tsegment *) pos->seg)->next = seg;
	// Update the writer.
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
	size_t consumed = 0;

	// Consume the segments that precede the given position and release no longer
	// used chunks.
	struct mm_chunk *chunk = mm_chunk_queue_head(&buf->chunks);
	struct mm_buffer_segment *first = mm_buffer_segment_first(chunk);
	for (struct mm_buffer_segment *seg = first; seg != pos->seg; ) {
		// Account for the segment size.
		consumed += mm_buffer_segment_area(seg);
		// Release the external segment.
		mm_buffer_segment_release(seg);
		// Move to the next segment.
		seg = mm_buffer_segment_next(seg);
		// On a terminal segment move to the next chunk.
		if (mm_buffer_segment_terminal(seg)) {
			struct mm_chunk *next = mm_chunk_queue_next(chunk);
			seg = first = mm_buffer_segment_first(next);

			// Destroy the previous chunk.
			mm_queue_remove(&buf->chunks);
			mm_chunk_destroy(chunk);
			chunk = next;
		}
	}

	// Handle the segment situated at the given position.
	char *end = mm_buffer_reader_end(pos);
	if (end > pos->ptr) {
		// The last segment is not yet completely consumed. Account for
		// the consumed data size.
		if (mm_buffer_segment_internal(pos->seg))
			consumed += pos->ptr - mm_buffer_segment_internal_data(pos->seg);
		else
			consumed += mm_buffer_segment_area(pos->seg);
		// Merge the preceding segments in the current chunk.
		if (first != pos->seg) {
			first->meta = ((char *) pos->seg) - ((char *) first);
			first->size = 0;
		}
	} else {
		// Release an external segment.
		mm_buffer_segment_release(pos->seg);
		// Account the segment size.
		uint32_t area = mm_buffer_segment_area(pos->seg);
		consumed += area;
		// Merge this segment with the first one.
		if (first == pos->seg)
			first->meta = area;
		else
			first->meta = area + ((char *) pos->seg) - ((char *) first);
		first->size = 0;
		// Fix up the head and tail iterators if needed.
		if (buf->head.seg == pos->seg) {
			ASSERT(buf->head.ptr == pos->ptr);
			if (buf->tail.seg == pos->seg)
				buf->tail.seg = first;
			mm_buffer_reader_set(&buf->head, first);
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

size_t NONNULL(1)
mm_buffer_size(struct mm_buffer *buf)
{
	uint32_t size = mm_buffer_reader_end(&buf->head) - mm_buffer_reader_ptr(&buf->head);

	struct mm_buffer_segment *seg = buf->head.seg;
	while (seg != buf->tail.seg) {
		seg = mm_buffer_segment_next(seg);
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
	// Get the address of the embedded block.
	void *data = mm_buffer_segment_internal_data(seg);

	LEAVE();
	return data;
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
		mm_buffer_reader_set(&buf->head, buf->tail.seg);
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
