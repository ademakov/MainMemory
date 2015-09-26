/*
 * base/memory/slider.c - MainMemory data buffer sliding window.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/memory/slider.h"

#include "base/log/trace.h"

void __attribute__((nonnull(1)))
mm_slider_fforward(struct mm_slider *slider, const char *ptr)
{
	ENTER();

	for (;;) {
		if (mm_slider_contains(slider, ptr)) {
			slider->ptr = (char *) ptr;
			break;
		}
		if (!mm_slider_next_used(slider)) {
			struct mm_buffer *buffer = slider->buf;
			slider->ptr = buffer->tail_seg->data + buffer->tail_off;
			break;
		}
	}

	LEAVE();
}

size_t __attribute__((nonnull(1)))
mm_slider_fill(struct mm_slider *slider, size_t size)
{
	ENTER();
	size_t o_size = size;

	for (;;) {
		uint32_t n = slider->end - slider->ptr;
		if (n >= size) {
			slider->ptr += size;
			size = 0;
			break;
		}

		size -= n;

		if (!mm_slider_next_used(slider)) {
			slider->ptr += n;
			break;
		}
	}

	DEBUG("bytes: %lu", o_size - size);
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1)))
mm_slider_flush(struct mm_slider *slider, size_t size)
{
	ENTER();
	size_t o_size = size;

	for (;;) {
		uint32_t n = slider->end - slider->ptr;
		if (n >= size) {
			slider->ptr += size;
			size = 0;
			break;
		}

		size -= n;

		if (!mm_slider_next_free(slider)) {
			slider->ptr += n;
			break;
		}
	}

	DEBUG("bytes: %lu", o_size - size);
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1, 2)))
mm_slider_read(struct mm_slider *slider, void *ptr, size_t size)
{
	ENTER();
	size_t o_size = size;
	char *data = ptr;

	for (;;) {
		uint32_t n = slider->end - slider->ptr;
		if (n >= size) {
			memcpy(data, slider->ptr, size);
			slider->ptr += size;
			size = 0;
			break;
		}

		memcpy(data, slider->ptr, n);
		data += n;
		size -= n;

		if (!mm_slider_next_used(slider)) {
			slider->ptr += n;
			break;
		}
	}

	DEBUG("bytes: %lu", o_size - size);
	LEAVE();
	return (o_size - size);
}

size_t __attribute__((nonnull(1, 2)))
mm_slider_write(struct mm_slider *slider, const void *ptr, size_t size)
{
	ENTER();
	size_t o_size = size;
	const char *data = ptr;

	for (;;) {
		uint32_t n = slider->end - slider->ptr;
		if (n >= size) {
			memcpy(slider->ptr, data, size);
			slider->ptr += size;
			size = 0;
			break;
		}

		memcpy(slider->ptr, data, n);
		data += n;
		size -= n;

		if (!mm_slider_next_free(slider)) {
			slider->ptr += n;
			break;
		}
	}

	DEBUG("bytes: %lu", o_size - size);
	LEAVE();
	return (o_size - size);
}
