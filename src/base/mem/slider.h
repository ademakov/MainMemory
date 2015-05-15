/*
 * base/mem/slider.h - MainMemory data buffer sliding window.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#ifndef BASE_MEM_SLIDER_H
#define BASE_MEM_SLIDER_H

#include "common.h"
#include "base/mem/buffer.h"

struct mm_slider
{
	/* Current data pointer. */
	char *ptr;
	/* End of data pointer. */
	char *end;
	/* Current buffer segment. */
	struct mm_buffer_segment *seg;
	/* Associated buffer. */
	struct mm_buffer *buf;
};

static inline bool __attribute__((nonnull(1, 2)))
mm_slider_first_free(struct mm_slider *slider,
		     struct mm_buffer *buffer)
{
	if (buffer->tail_seg == NULL)
		return false;

	slider->buf = buffer;
	slider->seg = buffer->tail_seg;
	slider->ptr = slider->seg->data + buffer->tail_off;
	slider->end = slider->seg->data + slider->seg->size;
	return true;
}

static inline bool __attribute__((nonnull(1)))
mm_slider_next_free(struct mm_slider *slider)
{
	if (slider->seg->next == NULL)
		return false;

	slider->seg = slider->seg->next;
	slider->ptr = slider->seg->data;
	slider->end = slider->seg->data + slider->seg->size;
	return true;
}

static inline size_t __attribute__((nonnull(1)))
mm_slider_getsize_free(struct mm_slider *slider)
{
	size_t size = slider->end - slider->ptr;
	struct mm_buffer_segment *seg = slider->seg->next;
	while (seg != NULL) {
		size += seg->size;
		seg = seg->next;
	}
	return size;
}

static inline void __attribute__((nonnull(1)))
mm_slider_reset_used(struct mm_slider *slider)
{
	if (slider->seg != slider->buf->tail_seg)
		slider->end = slider->seg->data + slider->seg->size;
	else
		slider->end = slider->seg->data + slider->buf->tail_off;
}

static inline bool __attribute__((nonnull(1, 2)))
mm_slider_first_used(struct mm_slider *slider,
		     struct mm_buffer *buffer)
{
	if (buffer->head_seg == NULL)
		return false;

	slider->buf = buffer;
	slider->seg = buffer->head_seg;
	slider->ptr = slider->seg->data + buffer->head_off;
	mm_slider_reset_used(slider);
	return true;
}

static inline bool __attribute__((nonnull(1)))
mm_slider_next_used(struct mm_slider *slider)
{
	if (slider->seg == slider->buf->tail_seg)
		return false;

	slider->seg = slider->seg->next;
	slider->ptr = slider->seg->data;
	mm_slider_reset_used(slider);
	return true;
}

static inline void __attribute__((nonnull(1)))
mm_slider_fill_free(struct mm_slider *slider)
{
	slider->buf->tail_seg = slider->seg;
	slider->buf->tail_off = slider->ptr - slider->seg->data;
}

static inline void __attribute__((nonnull(1)))
mm_slider_flush_used(struct mm_slider *slider)
{
	while (slider->buf->head_seg != slider->seg) {
		struct mm_buffer_segment *seg = slider->buf->head_seg;
		slider->buf->head_seg = seg->next;
		mm_buffer_segment_destroy(slider->buf, seg);
	}
	slider->buf->head_off = slider->ptr - slider->seg->data;
}

static inline bool __attribute__((nonnull(1)))
mm_slider_empty(struct mm_slider *slider)
{
	struct mm_buffer *buffer = slider->buf;
	// Assume that the cursor size is up to date.
	ASSERT((slider->end == (slider->seg != buffer->tail_seg
				? slider->seg->data + slider->seg->size
				: slider->seg->data + buffer->tail_off)));
	if (slider->ptr < slider->end)
		return false;
	if (slider->seg == buffer->tail_seg)
		return true;
	if (slider->seg->next == buffer->tail_seg && buffer->tail_off == 0)
		return true;
	return false;
}

static inline size_t __attribute__((nonnull(1)))
mm_slider_getsize_used(struct mm_slider *slider)
{
	struct mm_buffer *buffer = slider->buf;
	// Assume that the cursor size is up to date.
	ASSERT((slider->end == (slider->seg != buffer->tail_seg
				? slider->seg->data + slider->seg->size
				: slider->seg->data + buffer->tail_off)));
	size_t size = slider->end - slider->ptr;
	if (slider->seg != buffer->tail_seg) {
		struct mm_buffer_segment *seg = slider->seg->next;
		while (seg != buffer->tail_seg) {
			size += seg->size;
			seg = seg->next;
		}
		size += buffer->tail_off;
	}
	return size;
}

static inline bool __attribute__((nonnull(1)))
mm_slider_contains(struct mm_slider *slider, const char *ptr)
{
	return ptr >= slider->ptr && ptr < slider->end;
}

void __attribute__((nonnull(1)))
mm_slider_fforward(struct mm_slider *slider, const char *ptr);

size_t __attribute__((nonnull(1)))
mm_slider_fill(struct mm_slider *slider, size_t size);

size_t __attribute__((nonnull(1)))
mm_slider_flush(struct mm_slider *slider, size_t size);

size_t __attribute__((nonnull(1, 2)))
mm_slider_read(struct mm_slider *slider, void *ptr, size_t size);

size_t __attribute__((nonnull(1, 2)))
mm_slider_write(struct mm_slider *slider, const void *ptr, size_t size);

#endif /* BASE_MEM_SLIDER_H */
