/*
 * base/memory/buffer.h - MainMemory data buffers.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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

#define MM_BUFFER_MIN_CHUNK_SIZE	(1024u - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_MAX_CHUNK_SIZE	(512u * 1024u - MM_BUFFER_CHUNK_OVERHEAD)
#define MM_BUFFER_CHUNK_OVERHEAD	(MM_BUFFER_SEGMENT_SIZE + MM_BUFFER_TERMINAL_SIZE)

#define MM_BUFFER_SEGMENT_SIZE		sizeof(struct mm_buffer_segment)
#define MM_BUFFER_TERMINAL_SIZE		sizeof(struct mm_buffer_tsegment)

/* External segment release routine. */
typedef void (*mm_buffer_release_t)(uintptr_t release_data);

/* Segment flags. Must fit into MM_BUFFER_SEGMENT_MASK. */
enum {
	/* Internal data segment. */
	MM_BUFFER_SEGMENT_INTERNAL = 0,
	/* External data segment. */
	MM_BUFFER_SEGMENT_EXTERNAL = 1,
	/* Embedded (allocated) segment. */
	MM_BUFFER_SEGMENT_EMBEDDED = 2,
	/* Terminal segment (the last one in a chunk). */
	MM_BUFFER_SEGMENT_TERMINAL = 3,
};

#define MM_BUFFER_SEGMENT_MASK		3

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
	/* The basic segment info. */
	struct mm_buffer_segment base;
 	/* The data block. */
	char data[];
};

/* External buffer segment. */
struct mm_buffer_xsegment
{
	/* The basic segment info. */
	struct mm_buffer_segment base;
	/* The external data block. */
	char *data;

	/* The external data release info. */
	mm_buffer_release_t release;
	uintptr_t release_data;
};

/* Terminal segment in a buffer chunk. */
struct mm_buffer_tsegment
{
	/* The basic segment info. */
	struct mm_buffer_segment base;
	/* Pointer to the first segment of the next chunk. */
	struct mm_buffer_segment *next;
};

/* Buffer read iterator. */
struct mm_buffer_reader
{
	/* The current position. */
	char *ptr;
	/* The current segment. */
	struct mm_buffer_segment *seg;
};

/* Buffer write iterator. */
struct mm_buffer_writer
{
	/* The current segment. */
	struct mm_buffer_segment *seg;
};

/* Segmented data buffer. */
struct mm_buffer
{
	/* The current outgoing data position. */
	struct mm_buffer_reader head;
	/* The current incoming data position. */
	struct mm_buffer_writer tail;
	/* Initial pseudo-segment. */
	struct mm_buffer_tsegment stub;
	/* The minimum chunk size. */
	uint32_t chunk_size;
};

/**********************************************************************
 * Buffer segments.
 **********************************************************************/

static inline bool NONNULL(1)
mm_buffer_segment_internal(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_MASK) == MM_BUFFER_SEGMENT_INTERNAL;
}

static inline bool NONNULL(1)
mm_buffer_segment_external(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_MASK) == MM_BUFFER_SEGMENT_EXTERNAL;
}

static inline bool NONNULL(1)
mm_buffer_segment_embedded(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_MASK) == MM_BUFFER_SEGMENT_EMBEDDED;
}

static inline bool NONNULL(1)
mm_buffer_segment_terminal(const struct mm_buffer_segment *seg)
{
	return (seg->meta & MM_BUFFER_SEGMENT_MASK) == MM_BUFFER_SEGMENT_TERMINAL;
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
	return seg->size;
}

static inline char * NONNULL(1)
mm_buffer_segment_internal_data(const struct mm_buffer_segment *seg)
{
	ASSERT(!mm_buffer_segment_external(seg));
	return ((struct mm_buffer_isegment *) seg)->data;
}

static inline char * NONNULL(1)
mm_buffer_segment_external_data(const struct mm_buffer_segment *seg)
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
mm_buffer_segment_data(const struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_external(seg))
		return mm_buffer_segment_external_data(seg);
	else
		return mm_buffer_segment_internal_data(seg);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_adjacent_next(const struct mm_buffer_segment *seg)
{
	uint32_t area = mm_buffer_segment_area(seg);
	return (struct mm_buffer_segment *) (((char *) seg) + area);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_terminal_next(const struct mm_buffer_segment *seg)
{
	return ((struct mm_buffer_tsegment *) seg)->next;
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_next(const struct mm_buffer_segment *seg)
{
	if (mm_buffer_segment_terminal(seg))
		return mm_buffer_segment_terminal_next(seg);
	else
		return mm_buffer_segment_adjacent_next(seg);
}

static inline struct mm_buffer_segment * NONNULL(1)
mm_buffer_segment_first(struct mm_buffer *buffer)
{
	return mm_buffer_segment_terminal_next(&buffer->stub.base);
}

/**********************************************************************
 * Buffer initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_buffer_prepare(struct mm_buffer *buf, size_t chunk_size);

void NONNULL(1)
mm_buffer_cleanup(struct mm_buffer *buf);

/**********************************************************************
 * Buffer low-level read routines.
 **********************************************************************/

/* Set the reader at the start of the given segment. */
static inline void NONNULL(1)
mm_buffer_reader_set(struct mm_buffer_reader *pos, struct mm_buffer_segment *seg)
{
	pos->seg = seg;
	pos->ptr = mm_buffer_segment_data(seg);
}

/* Capture the current read position. */
static inline void NONNULL(1, 2)
mm_buffer_reader_save(struct mm_buffer_reader *pos, const struct mm_buffer *buf)
{
	*pos = buf->head;
}

/* Restore the current read position. */
static inline void NONNULL(1, 2)
mm_buffer_reader_restore(const struct mm_buffer_reader *pos, struct mm_buffer *buf)
{
	buf->head = *pos;
}

/* Get the start pointer for the current read segment. */
static inline char * NONNULL(1)
mm_buffer_reader_ptr(const struct mm_buffer_reader *pos)
{
	return pos->ptr;
}

/* Get the end pointer for the current read segment. */
static inline char * NONNULL(1)
mm_buffer_reader_end(const struct mm_buffer_reader *pos)
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

/* Advance to the next viable read segment if any. */
static inline uint32_t NONNULL(1, 2)
mm_buffer_reader_next(struct mm_buffer_reader *pos, struct mm_buffer *buf)
{
	/* Check if already at the last segment to read. */
	struct mm_buffer_segment *seg = pos->seg;
	if (seg == buf->tail.seg)
		return 0;

	/* Advance to the next segment. Skip any empty segments except the
	   last one. So embedded and terminal segments are skipped too. */
	for (;;) {
		seg = mm_buffer_segment_next(seg);
		size_t size = mm_buffer_segment_size(seg);
		if (size || seg == buf->tail.seg) {
			/* Update the position. */
			mm_buffer_reader_set(pos, seg);
			return size;
		}
	}
}

/* Try to get a viable read segment in a buffer. */
static inline uint32_t NONNULL(1)
mm_buffer_reader_ready(struct mm_buffer *buf)
{
	uint32_t size = mm_buffer_reader_end(&buf->head) - mm_buffer_reader_ptr(&buf->head);
	return size ? size : mm_buffer_reader_next(&buf->head, buf);
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
struct mm_buffer_segment * NONNULL(1, 2)
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
static inline uint32_t NONNULL(1)
mm_buffer_writer_next(struct mm_buffer_writer *pos)
{
	struct mm_buffer_segment *seg = pos->seg;
	while (!mm_buffer_segment_terminal(seg)) {
		seg = mm_buffer_segment_adjacent_next(seg);
		size_t room = mm_buffer_segment_internal_room(seg);
		if (room) {
			pos->seg = seg;
			return room;
		}
	}
	/* Here a terminal segment is either the last one or sometimes
	   an empty internal segment might follow it.*/
	struct mm_buffer_segment *nseg = mm_buffer_segment_terminal_next(seg);
	if (nseg != NULL) {
		pos->seg = nseg;
		return mm_buffer_segment_internal_room(nseg);
	}
	pos->seg = seg;
	return 0;
}

/* Advance to the next write segment creating it if needed. */
static inline uint32_t NONNULL(1, 2)
mm_buffer_writer_bump(struct mm_buffer_writer *pos, struct mm_buffer *buf, size_t size_hint)
{
	uint32_t room = mm_buffer_writer_next(pos);
	if (!room) {
		pos->seg = mm_buffer_writer_grow(pos, buf, size_hint);
		room = mm_buffer_segment_internal_room(pos->seg);
	}
	return room;
}

/* Make sure there is a viable write segment in a buffer. */
static inline uint32_t NONNULL(1)
mm_buffer_writer_make_ready(struct mm_buffer *buf, size_t size_hint)
{
	uint32_t room = mm_buffer_writer_room(&buf->tail);
	return room ? room : mm_buffer_writer_bump(&buf->tail, buf, size_hint);
}

/**********************************************************************
 * Buffer top-level routines.
 **********************************************************************/

static inline bool NONNULL(1)
mm_buffer_empty(struct mm_buffer *buf)
{
	return mm_buffer_reader_ptr(&buf->head) == mm_buffer_writer_ptr(&buf->tail);
}

size_t NONNULL(1)
mm_buffer_size(struct mm_buffer *buf);

size_t NONNULL(1)
mm_buffer_compact(struct mm_buffer *buf);

size_t NONNULL(1)
mm_buffer_skip(struct mm_buffer *buf, size_t size);

size_t NONNULL(1, 2)
mm_buffer_read(struct mm_buffer *buf, void *restrict data, size_t size);

void NONNULL(1, 2)
mm_buffer_write(struct mm_buffer *buf, const void *restrict data, size_t size);

void NONNULL(1, 2) FORMAT(2, 3)
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...);

void NONNULL(1, 2)
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va);

void NONNULL(1, 2)
mm_buffer_splice(struct mm_buffer *buf, char *restrict data, uint32_t size,
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

	if ((size_t)(end - mm_buffer_reader_ptr(&buf->head)) >= cnt)
		return true;

	return mm_buffer_span_slow(buf, cnt);
}

/* Seek for a given char and ensure a contiguous memory span up to it. */
char * NONNULL(1, 3)
mm_buffer_find(struct mm_buffer *buf, int c, size_t *poffset);

#endif /* BASE_MEMORY_BUFFER_H */
