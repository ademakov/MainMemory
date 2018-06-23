/*
 * base/memory/buffer.h - MainMemory data buffers.
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

#ifndef BASE_MEMORY_BUFFER_H
#define BASE_MEMORY_BUFFER_H

#include "common.h"
#include "base/bitops.h"
#include "base/report.h"
#include "base/memory/chunk.h"

#include <stdarg.h>

/*
 * MainMemory buffers grow and shrink as necessary. Incoming data is added
 * to the tail of the buffer and outgoing data is dropped from its head.
 *
 * The data is stored in a series of separate memory segments. The segments
 * may either be internal or external. The memory for internal segments is
 * allocated and released by the buffer itself. The incoming data is copied
 * to the buffer memory.
 *
 * The memory for external segments is supplied by the user and contains the
 * original incoming data. The buffer trusts the user that the memory will
 * stay there intact until dropped from the outgoing buffer end. The buffer
 * calls the user-supplied release routine just before dropping the external
 * segment.
 *
 * The outgoing data is handled in two steps. The first step is to read the
 * data. The second step is to drop the data by marking it as consumed. This
 * two-step mechanism provides for easy recovery from incomplete data reads.
 * It is possible to restart reading from the last safe position by saving it
 * and then restoring if necessary. But as soon as the data is consumed it is
 * no longer possible to restart with it.
 *
 * Also a buffer can be used to allocate (embed) small blocks of memory. An
 * embedded block takes a whole buffer segment. It is ignored by the buffer
 * read and write machinery. The embedded blocks mechanism saves some memory
 * allocation costs for temporary data that might be needed when processing
 * a particular buffered piece of data. Embedded blocks are dropped from the
 * outgoing buffer end as any other segments. So the lifetime of an embedded
 * block is automatically coupled with the lifetime of the adjacent data.
 *
 * NOTE: Buffers are not thread-safe, care should be taken not to access them
 * concurrently.
 */

#define MM_BUFFER_MIN_CHUNK_SIZE	(1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(4 * 1024 * 1024 - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_CHUNK_OVERHEAD	(MM_CHUNK_OVERHEAD + MM_BUFFER_SEGMENT_SIZE)

#define MM_BUFFER_SEGMENT_SIZE		sizeof(struct mm_buffer_segment)
#define MM_BUFFER_SEGMENT_MASK		(MM_BUFFER_SEGMENT_SIZE - 1)

/* External segment release routine. */
typedef void (*mm_buffer_release_t)(uintptr_t release_data);

/* Segment flags. Must fit into MM_BUFFER_SEGMENT_MASK. */
enum {
	/* External data segment. */
	MM_BUFFER_SEGMENT_EXTERNAL = 1,
	/* Embedded (allocated) segment. */
	MM_BUFFER_SEGMENT_EMBEDDED = 2,
	/* Terminal segment (the last one in a chunk). */
	MM_BUFFER_SEGMENT_TERMINAL = 4,
};

/* Abstract buffer segment. */
struct mm_buffer_segment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The real data size in the segment. */
	uint32_t size;
};

/* Internal buffer segment. */
struct mm_buffer_isegment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The real data size in the segment. */
	uint32_t size;
	/* The data block. */
	char data[];
};

/* External buffer segment. */
struct mm_buffer_xsegment
{
	/* The size and type of the segment. */
	uint32_t meta;
	/* The real data size in the segment. */
	uint32_t size;
	/* The external data block. */
	char *data;

	/* Release info. */
	mm_buffer_release_t release;
	uintptr_t release_data;
};

/* Buffer read iterator. */
struct mm_buffer_reader
{
	/* The current position. */
	char *ptr;
	/* The current segment. */
	struct mm_buffer_segment *seg;
	/* The current chunk. */
	struct mm_chunk *chunk;
};

/* Buffer write iterator. */
struct mm_buffer_writer
{
	/* The current segment. */
	struct mm_buffer_segment *seg;
	/* The current chunk. */
	struct mm_chunk *chunk;
};

/* Segmented data buffer. */
struct mm_buffer
{
	/* The current outgoing data position. */
	struct mm_buffer_reader head;
	/* The current incoming data position. */
	struct mm_buffer_writer tail;
	/* Entire buffer memory as a list of chunks. */
	struct mm_queue chunks;
	/* The minimum chunk size. */
	uint32_t chunk_size;
};

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

static inline bool NONNULL(1)
mm_buffer_segment_external(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_EXTERNAL) != 0;
}

static inline bool NONNULL(1)
mm_buffer_segment_embedded(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_EMBEDDED) != 0;
}

static inline bool NONNULL(1)
mm_buffer_segment_terminal(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_TERMINAL) != 0;
}

/* The size a segment occupies in a buffer chunk. Includes the header size,
   the data size, and perhaps some padding for alignment. But for external
   segments the data is stored separately so its size is not included. */
static inline uint32_t NONNULL(1)
mm_buffer_segment_area(const struct mm_buffer_segment *seg)
{
	return (seg->meta & ~MM_BUFFER_SEGMENT_MASK);
}

static inline uint32_t NONNULL(1)
mm_buffer_segment_internal_room(const struct mm_buffer_segment *seg)
{
	ASSERT(!mm_buffer_segment_external(seg));
	return mm_buffer_segment_area(seg) - MM_BUFFER_SEGMENT_SIZE;
}

static inline uint32_t NONNULL(1)
mm_buffer_segment_external_room(const struct mm_buffer_segment *seg)
{
	ASSERT(mm_buffer_segment_external(seg));
	return ((struct mm_buffer_xsegment *) seg)->size;
}

static inline char * NONNULL(1)
mm_buffer_segment_internal_data(struct mm_buffer_segment *seg)
{
	ASSERT(!mm_buffer_segment_external(seg));
	return ((struct mm_buffer_isegment *) seg)->data;
}

static inline char * NONNULL(1)
mm_buffer_segment_external_data(struct mm_buffer_segment *seg)
{
	ASSERT(mm_buffer_segment_external(seg));
	return ((struct mm_buffer_xsegment *) seg)->data;
}

/* The size available for data storage in a segment. */
static inline uint32_t NONNULL(1)
mm_buffer_segment_room(const struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_external(seg))
		return mm_buffer_segment_external_room(seg);
	else
		return mm_buffer_segment_internal_room(seg);
}

/* The size occupied by data in a segment. */
static inline uint32_t NONNULL(1)
mm_buffer_segment_size(const struct mm_buffer_segment *seg)
{
	return seg->size;
}

/* The address of data in a segment. */
static inline char * NONNULL(1)
mm_buffer_segment_data(struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_external(seg))
		return mm_buffer_segment_external_data(seg);
	else
		return mm_buffer_segment_internal_data(seg);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_first(struct mm_chunk *chunk)
{
	return (struct mm_buffer_segment *) chunk->data;
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_next(struct mm_buffer_segment *seg)
{
	uint32_t area = mm_buffer_segment_area(seg);
	return (struct mm_buffer_segment *) (((char *) seg) + area);
}

/**********************************************************************
 * Buffer initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf, size_t chunk_size);

void NONNULL(1)
mm_buffer_cleanup(struct mm_buffer *buf);

/* Check if a buffer already has at least one memory chunk. */
static inline bool NONNULL(1)
mm_buffer_ready(struct mm_buffer *buf)
{
	return buf->head.seg != NULL;
}

/* Make sure that a buffer has at least one memory chunk. */
void NONNULL(1)
mm_buffer_make_ready(struct mm_buffer *buf, size_t size_hint);

/**********************************************************************
 * Buffer low-level read routines.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_buffer_reader_save(struct mm_buffer_reader *pos, const struct mm_buffer *buf)
{
	*pos = buf->head;
}

static inline void NONNULL(1, 2)
mm_buffer_reader_restore(const struct mm_buffer_reader *pos, struct mm_buffer *buf)
{
	buf->head = *pos;
}

/* Set the read pointer to the start of the current segment. */
static inline void NONNULL(1)
mm_buffer_reader_reset_ptr(struct mm_buffer_reader *pos)
{
	pos->ptr = mm_buffer_segment_data(pos->seg);
}

/* Get the start pointer for the current read segment. */
static inline char * NONNULL(1)
mm_buffer_reader_ptr(struct mm_buffer_reader *pos)
{
	return pos->ptr;
}

/* Get the end pointer for the current read segment. */
static inline char * NONNULL(1)
mm_buffer_reader_end(struct mm_buffer_reader *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	return mm_buffer_segment_data(seg) + mm_buffer_segment_size(seg);
}

/* Check if the current reader segment is the last written. */
static inline bool NONNULL(1, 2)
mm_buffer_reader_last(struct mm_buffer_reader *pos, struct mm_buffer *buf)
{
	return pos->seg == buf->tail.seg;
}

/* Advance to the next read segment. It must be present. */
static inline void NONNULL(1)
mm_buffer_reader_next_unsafe(struct mm_buffer_reader *pos)
{
	if (mm_buffer_segment_terminal(pos->seg)) {
		pos->chunk = mm_chunk_queue_next(pos->chunk);
		pos->seg = mm_buffer_segment_first(pos->chunk);
	} else {
		pos->seg = mm_buffer_segment_next(pos->seg);
	}
}

/* Advance to the next viable read segment if any. */
static inline uint32_t NONNULL(1, 2)
mm_buffer_reader_next(struct mm_buffer_reader *pos, struct mm_buffer *buf)
{
	if (mm_buffer_reader_last(pos, buf))
		return 0;

	/* Advance to the next segment. */
	mm_buffer_reader_next_unsafe(pos);

	/* Skip any empty segments. So embedded segments are skipped too. */
	size_t size = mm_buffer_segment_size(pos->seg);
	while (size == 0 && !mm_buffer_reader_last(pos, buf)) {
		mm_buffer_reader_next_unsafe(pos);
		size = mm_buffer_segment_size(pos->seg);
	}

	mm_buffer_reader_reset_ptr(&buf->head);
	return size;
}

/* Try to get a viable read segment in a buffer. */
static inline uint32_t NONNULL(1)
mm_buffer_reader_ready(struct mm_buffer *buf)
{
	if (!mm_buffer_ready(buf))
		return 0;

	uint32_t size = mm_buffer_reader_end(&buf->head) - mm_buffer_reader_ptr(&buf->head);
	if (size)
		return size;

	return mm_buffer_reader_next(&buf->head, buf);
}

/**********************************************************************
 * Buffer low-level write routines.
 **********************************************************************/

/*
 * Typically a write segment is the last buffer segment. But sometimes it
 * can be followed by one or more empty internal segments. For example, a
 * buffer might previously be prepared for a readv() call that filled only
 * a part of the expected data leaving extra unused segments.
 *
 * As a write segment cannot be followed by an embedded segment so there is
 * no point to use embedded-filter logic here.
 */

/* Append a new buffer chunk. */
void NONNULL(1, 2)
mm_buffer_writer_grow(struct mm_buffer_writer *pos, struct mm_buffer *buf, size_t size_hint);

/* Capture the current write position. */
static inline void NONNULL(1, 2)
mm_buffer_writer_save(struct mm_buffer_writer *pos, const struct mm_buffer *buf)
{
	*pos = buf->tail;
}

/* Get the available room at the current write segment. */
static inline uint32_t NONNULL(1)
mm_buffer_writer_room(const struct mm_buffer_writer *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	return mm_buffer_segment_room(seg) - mm_buffer_segment_size(seg);
}

/* Get the start pointer for the current write segment. */
static inline char * NONNULL(1)
mm_buffer_writer_ptr(const struct mm_buffer_writer *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	return mm_buffer_segment_data(seg) + mm_buffer_segment_size(seg);
}

/* Get the end pointer for the current write segment. */
static inline char * NONNULL(1)
mm_buffer_writer_end(const struct mm_buffer_writer *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	return mm_buffer_segment_data(seg) + mm_buffer_segment_room(seg);
}

/* Advance to the next write segment if any. */
static inline bool NONNULL(1)
mm_buffer_writer_next(struct mm_buffer_writer *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	if (!mm_buffer_segment_terminal(seg)) {
		pos->seg = mm_buffer_segment_next(seg);
		return true;
	}

	struct mm_chunk *chunk = mm_chunk_queue_next(pos->chunk);
	if (chunk == NULL)
		return false;

	pos->seg = mm_buffer_segment_first(chunk);
	pos->chunk = chunk;
	return true;
}

/* Advance to the next write segment creating it if needed. */
static inline uint32_t NONNULL(1, 2)
mm_buffer_writer_bump(struct mm_buffer_writer *pos, struct mm_buffer *buf, size_t size_hint)
{
	if (!mm_buffer_writer_next(pos))
		mm_buffer_writer_grow(pos, buf, size_hint);
	return mm_buffer_segment_internal_room(pos->seg);
}

/* Make sure there is a viable write segment in a buffer. */
static inline uint32_t NONNULL(1)
mm_buffer_writer_make_ready(struct mm_buffer *buf, size_t size_hint)
{
	if (!mm_buffer_ready(buf)) {
		mm_buffer_make_ready(buf, size_hint);
		return mm_buffer_segment_internal_room(buf->tail.seg);
	}

	uint32_t room = mm_buffer_writer_room(&buf->tail);
	if (room)
		return room;

	return mm_buffer_writer_bump(&buf->tail, buf, size_hint);
}

/**********************************************************************
 * Buffer top-level routines.
 **********************************************************************/

static inline bool NONNULL(1)
mm_buffer_empty(struct mm_buffer *buf)
{
	if (!mm_buffer_ready(buf))
		return true;
	return buf->head.ptr == mm_buffer_writer_ptr(&buf->tail);
}

size_t NONNULL(1)
mm_buffer_size(struct mm_buffer *buf);

size_t NONNULL(1, 2)
mm_buffer_consume(struct mm_buffer *buf, const struct mm_buffer_reader *pos);

/* Improve space utilization of a buffer that was previously in use. */
static inline void NONNULL(1)
mm_buffer_compact(struct mm_buffer *buf)
{
	struct mm_buffer_reader pos;
	/* Get the last read position. */
	mm_buffer_reader_save(buf, &pos);
	/* Consume everything up to it. */
	mm_buffer_consume(buf, &pos);
}

size_t NONNULL(1)
mm_buffer_skip(struct mm_buffer *buf, size_t size);

size_t NONNULL(1, 2)
mm_buffer_read(struct mm_buffer *buf, void *data, size_t size);

void NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *data, size_t size);

void NONNULL(1, 2) FORMAT(2, 3)
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...);

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va);

void NONNULL(1, 2)
mm_buffer_splice(struct mm_buffer *buf, char *data, uint32_t size,
		 mm_buffer_release_t release, uintptr_t release_data);

void * NONNULL(1)
mm_buffer_embed(struct mm_buffer *buf, uint32_t size);

/**********************************************************************
 * Buffer in-place parsing support.
 **********************************************************************/

/* Ensure a contiguous memory span at the current read position (slow
   path of the inline mm_buffer_span() function). */
bool NONNULL(1)
mm_buffer_span_slow(struct mm_buffer *buf, size_t cnt);

/* Ensure a contiguous memory span at the current read position. */
static inline bool NONNULL(1)
mm_buffer_span(struct mm_buffer *buf, size_t cnt)
{
	char *end;
	if (buf->head.seg != buf->tail.seg)
		end = mm_buffer_reader_end(&buf->head);
	else
		end = mm_buffer_writer_end(&buf->tail);
	if ((size_t)(end - buf->head.ptr) >= cnt)
		return true;
	return mm_buffer_span_slow(buf, cnt);
}

/* Seek for a given char and ensure a contiguous memory span up to it. */
char * NONNULL(1, 3)
mm_buffer_find(struct mm_buffer *buf, int c, size_t *poffset);

#endif /* BASE_MEMORY_BUFFER_H */
