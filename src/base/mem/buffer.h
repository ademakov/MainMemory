/*
 * base/mem/buffer.h - MainMemory data buffers.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef BASE_MEM_BUFFER_H
#define BASE_MEM_BUFFER_H

#include "common.h"
#include "base/log/debug.h"
#include "base/mem/arena.h"
#include "base/mem/chunk.h"
#include <stdarg.h>

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
 * NOTE: Buffers are not thread-safe, care should be taken not to pass them
 * across cores.
 */

typedef void (*mm_buffer_release_t)(uintptr_t release_data);

struct mm_buffer
{
	/* The incoming data segment. */
	struct mm_buffer_segment *tail_seg;
	/* The outgoing data segment. */
	struct mm_buffer_segment *head_seg;
	/* The incoming data offset. */
	size_t tail_off;
	/* The outgoing data offset. */
	size_t head_off;

	/* Memory allocation info. */
	mm_arena_t arena;
	mm_chunk_t chunk_tag;
};

struct mm_buffer_segment
{
	/* The data block. */
	char *data;
	size_t size;

	/* The next segment in the buffer. */
	struct mm_buffer_segment *next;

	/* Release info. */
	mm_buffer_release_t release;
	uintptr_t release_data;
};

static inline bool __attribute__((nonnull(1)))
mm_buffer_empty(struct mm_buffer *buf)
{
	if (buf->head_seg == NULL)
		return true;
	if (buf->head_seg != buf->tail_seg)
		return false;
	return (buf->tail_off == buf->head_off);
}

static inline size_t __attribute__((nonnull(1)))
mm_buffer_getsize(struct mm_buffer *buf)
{
	size_t size = 0;
	struct mm_buffer_segment *seg = buf->head_seg;
	for (; seg != NULL; seg = seg->next)
		size += seg->size;
	return size;
}

static inline size_t __attribute__((nonnull(1)))
mm_buffer_getfree(struct mm_buffer *buf)
{
	size_t size = 0;
	struct mm_buffer_segment *seg = buf->tail_seg;
	for (; seg != NULL; seg = seg->next)
		size += seg->size;
	return size - buf->tail_off;
}

static inline size_t __attribute__((nonnull(1)))
mm_buffer_getused(struct mm_buffer *buf)
{
	size_t size = 0;
	struct mm_buffer_segment *seg = buf->head_seg;
	for (; seg != buf->tail_seg; seg = seg->next)
		size += seg->size;
	return size - buf->head_off + buf->tail_off;
}

void __attribute__((nonnull(1, 2)))
mm_buffer_prepare(struct mm_buffer *buf, mm_arena_t arena, mm_chunk_t chunk_tag);

void __attribute__((nonnull(1)))
mm_buffer_cleanup(struct mm_buffer *buf);

void __attribute__((nonnull(1)))
mm_buffer_rectify(struct mm_buffer *buf);

void __attribute__((nonnull(1)))
mm_buffer_demand(struct mm_buffer *buf, size_t size);

size_t __attribute__((nonnull(1)))
mm_buffer_fill(struct mm_buffer *buf, size_t size);

size_t __attribute__((nonnull(1)))
mm_buffer_flush(struct mm_buffer *buf, size_t size);

size_t __attribute__((nonnull(1, 2)))
mm_buffer_read(struct mm_buffer *buf, void *ptr, size_t size);

size_t __attribute__((nonnull(1, 2)))
mm_buffer_write(struct mm_buffer *buf, const void *ptr, size_t size);

void __attribute__((format(printf, 2, 3))) __attribute__((nonnull(1, 2)))
mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...);

void __attribute__((nonnull(1, 2)))
mm_buffer_vprintf(struct mm_buffer *buf, const char *restrict fmt, va_list va);

void __attribute__((nonnull(1, 2)))
mm_buffer_splice(struct mm_buffer *buf, char *data, size_t size,
		 mm_buffer_release_t release, uintptr_t release_data);

/**********************************************************************
 * Buffer cursor.
 **********************************************************************/

struct mm_buffer_cursor
{
	/* Current data pointer. */
	char *ptr;
	/* End of data pointer. */
	char *end;
	/* Current segment. */
	struct mm_buffer_segment *seg;
};

static inline bool
mm_buffer_tail(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (buf->tail_seg == NULL)
		return false;

	cur->seg = buf->tail_seg;
	cur->ptr = cur->seg->data + buf->tail_off;
	cur->end = cur->seg->data + cur->seg->size;
	return true;
}

static inline bool
mm_buffer_tail_next(struct mm_buffer *buf __attribute__((unused)),
		    struct mm_buffer_cursor *cur)
{
	if (cur->seg->next == NULL)
		return false;

	cur->seg = cur->seg->next;
	cur->ptr = cur->seg->data;
	cur->end = cur->seg->data + cur->seg->size;
	return true;
}

static inline void
mm_buffer_head_size(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (cur->seg != buf->tail_seg)
		cur->end = cur->seg->data + cur->seg->size;
	else
		cur->end = cur->seg->data + buf->tail_off;
}

static inline bool
mm_buffer_head(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (buf->head_seg == NULL)
		return false;

	cur->seg = buf->head_seg;
	cur->ptr = cur->seg->data + buf->head_off;
	mm_buffer_head_size(buf, cur);
	return true;
}

static inline bool
mm_buffer_head_next(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (cur->seg == buf->tail_seg)
		return false;

	cur->seg = cur->seg->next;
	cur->ptr = cur->seg->data;
	mm_buffer_head_size(buf, cur);
	return true;
}

static inline bool
mm_buffer_depleted(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	// Assume that the cursor size is up to date.
	ASSERT(cur->end == (cur->seg != buf->tail_seg
			    ? cur->seg->data + cur->seg->size
			    : cur->seg->data + buf->tail_off));
	if (cur->ptr < cur->end)
		return false;
	if (cur->seg == buf->tail_seg)
		return true;
	if (cur->seg->next == buf->tail_seg && buf->tail_off == 0)
		return true;
	return false;
}

static inline size_t
mm_buffer_leftover(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	size_t size = cur->end - cur->ptr;
	if (cur->seg != buf->tail_seg) {
		struct mm_buffer_segment *seg = cur->seg->next;
		while (seg != buf->tail_seg) {
			size += seg->size;
			seg = seg->next;
		}
		size += buf->tail_off;
	}
	return size;
}

#endif /* BASE_MEM_BUFFER_H */
