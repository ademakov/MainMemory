/*
 * base/memory/buffer.h - MainMemory data buffers.
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

#ifndef BASE_MEMORY_BUFFER_H
#define BASE_MEMORY_BUFFER_H

#include "common.h"
#include "base/bitops.h"
#include "base/log/debug.h"
#include "base/memory/chunk.h"

#include <stdarg.h>

#define MM_BUFFER_ALIGN		sizeof(struct mm_buffer_segment)

#define MM_BUFFER_SEGMENT_SIZE	sizeof(struct mm_buffer_segment)
#define MM_BUFFER_SEGMENT_TYPE	(3)


/*
 * MainMemory buffers grow and shrink as necessary. Incoming data is appended
 * to the tail of the buffer and outgoing data is cut from its head. The data
 * is stored in a series of separate memory segments. The segments may either
 * be internal or external. The memory for internal segments is allocated and
 * released by the buffer itself. The incoming data is copied to this memory.
 * The memory for external segments is the original memory that contains the
 * incoming data. The buffer trusts the user that the memory will stay there
 * intact until it is used by the buffer. The buffer calls the user-supplied
 * release routine to let the user know that the external segment is no longer
 * used.
 * 
 * NOTE: Buffers are not thread-safe, care should be taken not to access them
 * concurrently.
 */

typedef void (*mm_buffer_release_t)(uintptr_t release_data);

typedef enum {
	/* External data segment. */
	MM_BUFFER_EXTERNAL = 0,
	/* Internal data segment. */
	MM_BUFFER_INTERNAL = 1,
	/* Embedded (allocated) segment. */
	MM_BUFFER_EMBEDDED = 2,
} mm_buffer_segment_t;

/* Abstract buffer segment. */
struct mm_buffer_segment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The occupied space in the segment. */
	uint32_t used;
};

/* Internal buffer segment. */
struct mm_buffer_isegment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The occupied space in the segment. */
	uint32_t used;
	/* The data block. */
	char data[];
};

/* External buffer segment. */
struct mm_buffer_xsegment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The occupied space in the segment. */
	uint32_t used;
	/* The external data block size. */
	uint32_t size;
	/* The external data block. */
	char *data;

	/* Release info. */
	mm_buffer_release_t release;
	uintptr_t release_data;
};

/* Buffer iterator. */
struct mm_buffer_iterator
{
	/* The current position. */
	char *ptr;
	/* The current data end. */
	char *end;
	/* The current segment. */
	struct mm_buffer_segment *seg;
	/* The sentinel pointer. */
	struct mm_buffer_segment *sen;
	/* The current chunk. */
	struct mm_chunk *chunk;
};

/* Segmented data buffer. */
struct mm_buffer
{
	/* The current outgoing data position. */
	struct mm_buffer_iterator head;
	/* The current incoming data position. */
	struct mm_buffer_iterator tail;
	/* Entire buffer memory as a list of chunks. */
	struct mm_queue chunks;
	/* The maximum consumed size. */
	size_t consumed_max;
};

/* Buffer read position. */
struct mm_buffer_position
{
	/* The current position. */
	char *ptr;
	/* The current segment. */
	struct mm_buffer_segment *seg;
	/* The current chunk. */
	struct mm_chunk *chunk;
};

/**********************************************************************
 * Buffer size calculation.
 **********************************************************************/

static inline uint32_t
mm_buffer_round_size(uint32_t size)
{
	return mm_round_up(size, MM_BUFFER_ALIGN);
}

static inline uint32_t
mm_buffer_round_room(uint32_t size)
{
	return mm_round_down(size, MM_BUFFER_ALIGN);
}

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

static inline mm_buffer_segment_t NONNULL(1)
mm_buffer_segment_gettype(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_TYPE);
}

static inline bool NONNULL(1)
mm_buffer_segment_ignored(const struct mm_buffer_segment *seg)
{
	return (mm_buffer_segment_gettype(seg) == MM_BUFFER_EMBEDDED);
}

static inline uint32_t NONNULL(1)
mm_buffer_segment_getarea(const struct mm_buffer_segment *seg)
{
	return (seg->meta & ~MM_BUFFER_SEGMENT_TYPE);
}

static inline uint32_t NONNULL(1)
mm_buffer_segment_getsize(const struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_gettype(seg) == MM_BUFFER_EXTERNAL)
		return ((struct mm_buffer_xsegment *) seg)->size;
	else
		return mm_buffer_segment_getarea(seg) - MM_BUFFER_SEGMENT_SIZE;
}

static inline uint32_t NONNULL(1)
mm_buffer_segment_getused(const struct mm_buffer_segment *seg)
{
	return seg->used;
}

static inline char * NONNULL(1)
mm_buffer_segment_getdata(struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_gettype(seg) == MM_BUFFER_EXTERNAL)
		return ((struct mm_buffer_xsegment *) seg)->data;
	else
		return ((struct mm_buffer_isegment *) seg)->data;
}

static inline void NONNULL(1)
mm_buffer_segment_release(struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_gettype(seg) == MM_BUFFER_EXTERNAL) {
		struct mm_buffer_xsegment *xseg = (struct mm_buffer_xsegment *) seg;
		if (xseg->release != NULL)
			(*xseg->release)(xseg->release_data);
	}
}

/**********************************************************************
 * Buffer chunks.
 **********************************************************************/

static inline uint32_t NONNULL(1)
mm_buffer_chunk_getsize(const struct mm_chunk *chunk)
{
	return mm_buffer_round_room(mm_chunk_getsize(chunk));
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_chunk_begin(struct mm_chunk *chunk)
{
	return (struct mm_buffer_segment *) chunk->data;
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_chunk_end(struct mm_chunk *chunk)
{
	uint32_t size = mm_buffer_chunk_getsize(chunk);
	return (struct mm_buffer_segment *) (chunk->data + size);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_chunk_next(struct mm_buffer_segment *seg)
{
	uint32_t area = mm_buffer_segment_getarea(seg);
	return (struct mm_buffer_segment *) (((char *) seg) + area);
}

/**********************************************************************
 * Buffer iterators.
 **********************************************************************/

static inline void NONNULL(1)
mm_buffer_iterator_prepare(struct mm_buffer_iterator *iter)
{
	iter->chunk = NULL;
	iter->seg = iter->sen = NULL;
	iter->ptr = iter->end = NULL;
}

static inline struct mm_buffer_segment * NONNULL(1, 2)
mm_buffer_iterator_chunk_start(struct mm_buffer_iterator *iter, struct mm_chunk *chunk)
{
	iter->chunk = chunk;
	iter->seg = mm_buffer_chunk_begin(chunk);
	iter->sen = mm_buffer_chunk_end(chunk);
	return iter->seg;
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_iterator_start(struct mm_buffer_iterator *iter, struct mm_chunk *chunk)
{
	return chunk == NULL ? NULL : mm_buffer_iterator_chunk_start(iter, chunk);
}

static inline struct mm_buffer_segment * NONNULL(1, 2)
mm_buffer_iterator_begin(struct mm_buffer_iterator *iter, struct mm_buffer *buf)
{
	return mm_buffer_iterator_start(iter, mm_chunk_queue_head(&buf->chunks));
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_iterator_next(struct mm_buffer_iterator *iter)
{
	struct mm_buffer_segment *seg = mm_buffer_chunk_next(iter->seg);
	if (seg != iter->sen) {
		iter->seg = seg;
		return seg;
	}
	return mm_buffer_iterator_start(iter, mm_chunk_queue_next(iter->chunk));
}

static inline void NONNULL(1)
mm_buffer_iterator_read_reset(struct mm_buffer_iterator *iter)
{
	iter->end = mm_buffer_segment_getdata(iter->seg) + mm_buffer_segment_getused(iter->seg);
}

static inline void NONNULL(1)
mm_buffer_iterator_write_reset(struct mm_buffer_iterator *iter)
{
	iter->end = mm_buffer_segment_getdata(iter->seg) + mm_buffer_segment_getsize(iter->seg);
}

static inline void NONNULL(1)
mm_buffer_iterator_read_start(struct mm_buffer_iterator *iter)
{
	iter->ptr = mm_buffer_segment_getdata(iter->seg);
	mm_buffer_iterator_read_reset(iter);
}

static inline void NONNULL(1)
mm_buffer_iterator_write_start(struct mm_buffer_iterator *iter)
{
	iter->ptr = mm_buffer_segment_getdata(iter->seg);
	mm_buffer_iterator_write_reset(iter);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_iterator_filter_next(struct mm_buffer_iterator *iter)
{
	struct mm_buffer_segment *seg = mm_buffer_iterator_next(iter);
	if (seg != NULL) {
		/* An embedded segment cannot be the last one in
		   a buffer so if one is found then there should
		   be another one after it. */
		while (mm_buffer_segment_ignored(seg)) {
			seg = mm_buffer_iterator_next(iter);
			ASSERT(seg != NULL);
		}
	}
	return seg;
}

static inline bool NONNULL(1)
mm_buffer_iterator_read_next_unsafe(struct mm_buffer_iterator *iter)
{
	struct mm_buffer_segment *seg = mm_buffer_iterator_filter_next(iter);
	if (seg != NULL) {
		mm_buffer_iterator_read_start(iter);
		return true;
	}
	return false;
}

static inline bool NONNULL(1)
mm_buffer_iterator_write_next_unsafe(struct mm_buffer_iterator *iter)
{
	/* A write segment cannot be followed by an embedded segment
	   so there is no need to use filter_next() here. Typically
	   the write segment is the last one in the buffer thus this
	   function returns nothing. But in certain cases it could be
	   followed by an empty segment. For instance, a buffer might
	   be extended for a readv() call but the call filled just a
	   part of the reserved space. */
	struct mm_buffer_segment *seg = mm_buffer_iterator_next(iter);
	if (seg != NULL) {
		mm_buffer_iterator_write_start(iter);
		return true;
	}
	return false;
}

static inline bool NONNULL(1)
mm_buffer_iterator_read_next(struct mm_buffer_iterator *iter)
{
	return iter->seg != NULL && mm_buffer_iterator_read_next_unsafe(iter);
}

static inline bool NONNULL(1)
mm_buffer_iterator_write_next(struct mm_buffer_iterator *iter)
{
	return iter->seg != NULL && mm_buffer_iterator_write_next_unsafe(iter);
}

/**********************************************************************
 * Buffer top-level routines.
 **********************************************************************/

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf);

void NONNULL(1)
mm_buffer_cleanup(struct mm_buffer *buf);

struct mm_buffer_segment * NONNULL(1, 2)
mm_buffer_extend(struct mm_buffer *buf, struct mm_buffer_iterator *iter, size_t size_hint);

size_t NONNULL(1, 2)
mm_buffer_consume(struct mm_buffer *buf, const struct mm_buffer_position *pos);

void NONNULL(1)
mm_buffer_rectify(struct mm_buffer *buf);

static inline bool NONNULL(1)
mm_buffer_valid(struct mm_buffer *buf)
{
	return buf->head.seg != NULL;
}

static inline bool NONNULL(1)
mm_buffer_empty(struct mm_buffer *buf)
{
	return buf->head.ptr == buf->tail.ptr;
}

static inline void NONNULL(1)
mm_buffer_update(struct mm_buffer *buf)
{
	mm_buffer_iterator_read_reset(&buf->head);
}

static inline bool NONNULL(1)
mm_buffer_read_next(struct mm_buffer *buf)
{
	return mm_buffer_iterator_read_next(&buf->head);
}

static inline void NONNULL(1)
mm_buffer_write_next(struct mm_buffer *buf, size_t size_hint)
{
	if (!mm_buffer_iterator_write_next(&buf->tail))
		mm_buffer_extend(buf, &buf->tail, size_hint);
}

size_t NONNULL(1)
mm_buffer_getsize(struct mm_buffer *buf);

size_t NONNULL(1)
mm_buffer_getarea(struct mm_buffer *buf);

size_t NONNULL(1)
mm_buffer_getleft(struct mm_buffer *buf);

size_t NONNULL(1)
mm_buffer_fill(struct mm_buffer *buf, size_t cnt);

size_t NONNULL(1)
mm_buffer_flush(struct mm_buffer *buf, size_t cnt);

size_t NONNULL(1, 2)
mm_buffer_read(struct mm_buffer *buf, void *ptr, size_t cnt);

size_t NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *ptr, size_t cnt);

void NONNULL(1, 2) FORMAT(2, 3)
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...);

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va);

void NONNULL(1, 2)
mm_buffer_splice(struct mm_buffer *buf, char *data, uint32_t size, uint32_t used,
		 mm_buffer_release_t release, uintptr_t release_data);

void * NONNULL(1)
mm_buffer_embed(struct mm_buffer *buf, uint32_t size);

/**********************************************************************
 * Buffer position.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_buffer_position_save(struct mm_buffer_position *pos, struct mm_buffer *buf)
{
	pos->chunk = buf->head.chunk;
	pos->seg = buf->head.seg;
	pos->ptr = buf->head.ptr;
}

static inline void NONNULL(1, 2)
mm_buffer_position_restore(struct mm_buffer_position *pos, struct mm_buffer *buf)
{
	buf->head.chunk = pos->chunk;
	buf->head.seg = pos->seg;
	buf->head.sen = mm_buffer_chunk_end(pos->chunk);
	buf->head.ptr = pos->ptr;
	mm_buffer_update(buf);
}

/**********************************************************************
 * Buffer in-place parsing support.
 **********************************************************************/

/* Ensure a contiguous memory span at the current read position (slow
 * path of the inline mm_buffer_span() function).
 */
bool NONNULL(1)
mm_buffer_span_slow(struct mm_buffer *buf, size_t cnt);

/* Ensure a contiguous memory span at the current read position. */
static inline bool NONNULL(1)
mm_buffer_span(struct mm_buffer *buf, size_t cnt)
{
	size_t len;
	if (buf->tail.seg == buf->head.seg)
		len = buf->tail.end - buf->head.ptr;
	else
		len = buf->head.end - buf->head.ptr;
	return (len < cnt) || mm_buffer_span_slow(buf, cnt);
}

/* Seek for a given char and ensure a contiguous memory span up to it. */
char * NONNULL(1, 3)
mm_buffer_find(struct mm_buffer *buf, int c, size_t *poffset);

#endif /* BASE_MEMORY_BUFFER_H */
