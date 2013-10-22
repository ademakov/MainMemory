/*
 * buffer.h - MainMemory data buffers.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef BUFFER_H
#define BUFFER_H

#include "common.h"
#include "trace.h"

/* Forward declarations. */
struct mm_buffer;

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

typedef void (*mm_buffer_release_t)(struct mm_buffer *buf, uintptr_t release_data);

struct mm_buffer
{
	/* The incoming data end. */
	struct mm_buffer_segment *in_seg;
	size_t in_off;

	/* The outgoing data end. */
	struct mm_buffer_segment *out_seg;
	size_t out_off;

	/* Internal data store. */
	struct mm_chunk *store_head;
	struct mm_chunk *store_tail;
	size_t store_size;
};

struct mm_buffer_segment
{
	/* The next segment in the buffer. */
	struct mm_buffer_segment *next;

	/* The data block. */
	char *data;
	size_t size;

	/* Release info. */
	mm_buffer_release_t release;
	uintptr_t release_data;
};

static inline bool
mm_buffer_empty(struct mm_buffer *buf)
{
	return (buf->in_seg == buf->out_seg && buf->in_off == buf->out_off);
}

void mm_buffer_prepare(struct mm_buffer *buf)
	__attribute__((nonnull(1)));

void mm_buffer_cleanup(struct mm_buffer *buf)
	__attribute__((nonnull(1)));

void mm_buffer_rectify(struct mm_buffer *buf)
	__attribute__((nonnull(1)));

void mm_buffer_append(struct mm_buffer *buf, const char *data, size_t size)
	__attribute__((nonnull(1)));

void mm_buffer_printf(struct mm_buffer *buf, const char *restrict fmt, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(1, 2)));

void mm_buffer_demand(struct mm_buffer *buf, size_t size)
	__attribute__((nonnull(1)));

size_t mm_buffer_expand(struct mm_buffer *buf, size_t size)
	__attribute__((nonnull(1)));

size_t mm_buffer_reduce(struct mm_buffer *buf, size_t size)
	__attribute__((nonnull(1)));

void mm_buffer_splice(struct mm_buffer *buf, char *data, size_t size,
		      mm_buffer_release_t release, uintptr_t release_data)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Buffer cursor.
 **********************************************************************/

struct mm_buffer_cursor
{
	/* Current data block. */
	char *data;
	size_t size;

	/* Current segment. */
	struct mm_buffer_segment *seg;
};

static inline bool
mm_buffer_first_in(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (buf->in_seg != NULL) {
		cur->seg = buf->in_seg;
		cur->size = cur->seg->size - buf->in_off;
		cur->data = cur->seg->data + buf->in_off;
		return true;
	}
	return false;
}

static inline bool
mm_buffer_next_in(struct mm_buffer *buf __attribute__((unused)), struct mm_buffer_cursor *cur)
{
	if (cur->seg->next != NULL) {
		cur->seg = cur->seg->next;
		cur->size = cur->seg->size;
		cur->data = cur->seg->data;
		return true;
	}
	return false;
}

static inline bool
mm_buffer_first_out(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (buf->out_seg != buf->in_seg) {
		cur->seg = buf->out_seg;
		cur->size = cur->seg->size - buf->out_off;
		cur->data = cur->seg->data + buf->out_off;
		return true;
	} else if (buf->out_seg != NULL) {
		cur->seg = buf->out_seg;
		cur->size = buf->in_off - buf->out_off;
		cur->data = cur->seg->data + buf->out_off;
		return true;
	}
	return false;
}

static inline bool
mm_buffer_next_out(struct mm_buffer *buf, struct mm_buffer_cursor *cur)
{
	if (cur->seg != buf->in_seg) {
		if (cur->seg->next != buf->in_seg) {
			cur->seg = cur->seg->next;
			cur->size = cur->seg->size;
			cur->data = cur->seg->data;
			return true;
		} else {
			cur->seg = cur->seg->next;
			cur->size = buf->in_off;
			cur->data = cur->seg->data;
			return true;
		}
	}
	return false;
}

#endif /* BUFFER_H */
