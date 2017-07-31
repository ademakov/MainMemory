/*
 * net/netbuf.c - MainMemory buffered network I/O.
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

#include "net/netbuf.h"

#include "base/thread/thread.h"

#define MM_NETBUF_MAXIOV	64

void NONNULL(1)
mm_netbuf_prepare(struct mm_netbuf_socket *sock, size_t rx_chunk_size, size_t tx_chunk_size)
{
	mm_buffer_prepare(&sock->rxbuf, rx_chunk_size);
	mm_buffer_prepare(&sock->txbuf, tx_chunk_size);
}

void NONNULL(1)
mm_netbuf_cleanup(struct mm_netbuf_socket *sock)
{
	mm_buffer_cleanup(&sock->rxbuf);
	mm_buffer_cleanup(&sock->txbuf);
}

static __attribute__((noinline)) ssize_t
mm_netbuf_fill_iov(struct mm_netbuf_socket *sock, size_t size, struct mm_buffer *buf, uint32_t n, char *p)
{
	struct iovec iov[MM_NETBUF_MAXIOV];
	iov[0].iov_len = n;
	iov[0].iov_base = p;

	int iovcnt = 1;
	size_t room = n;
	struct mm_buffer_writer iter = buf->tail;
	do {
		n = mm_buffer_write_more(buf, &iter, size - room);
		p = mm_buffer_write_ptr(buf);

		room += n;
		iov[iovcnt].iov_len = n;
		iov[iovcnt].iov_base = p;
		++iovcnt;

	} while ((room < size) && (iovcnt < MM_NETBUF_MAXIOV));

	return mm_net_readv(&sock->sock, iov, iovcnt, room);
}

ssize_t NONNULL(1)
mm_netbuf_fill(struct mm_netbuf_socket *sock, size_t size)
{
	ENTER();
	ssize_t rc;
	struct mm_buffer *buf = &sock->rxbuf;

	// Ensure that at least one buffer segment is present.
	uint32_t n = mm_buffer_write_start(buf, size);
	char *p = mm_buffer_write_ptr(buf);

	if (n >= size) {
		// Try to read using the current segment.
		rc = mm_net_read(&sock->sock, p, n);

		// On success bump the occupied data size.
		if (rc > 0)
			buf->tail.seg->size += rc;
	} else {
		// Try to read using multiple segments.
		rc = mm_netbuf_fill_iov(sock, size, buf, n, p);

		// On success mark the segments occupied by data.
		if (rc > 0) {
			size_t u = rc;
			for (;;) {
				uint32_t s = mm_buffer_writer_size(&buf->tail);
				if (u <= s) {
					buf->tail.seg->size += u;
					break;
				}

				buf->tail.seg->size += s;
				u -= s;

				VERIFY(mm_buffer_writer_next(&buf->tail));
			}
		}
	}

	DEBUG("rc: %ld", (long) rc);
	LEAVE();
	return rc;
}

ssize_t NONNULL(1)
mm_netbuf_flush(struct mm_netbuf_socket *sock)
{
	ENTER();
	ssize_t rc;
	struct mm_buffer *buf = &sock->txbuf;

	if (!mm_buffer_valid(buf)) {
		rc = 0;
		goto leave;
	}

	char *ptr = mm_buffer_reader_ptr(&buf->head);
	size_t len = mm_buffer_reader_end(&buf->head) - ptr;
	while (len == 0 && mm_buffer_reader_next_check(&buf->head)) {
		mm_buffer_reader_try_next_unsafe(&buf->head);
		ptr = mm_buffer_reader_ptr(&buf->head);
		len = mm_buffer_reader_end(&buf->head) - ptr;
	}

	if (!mm_buffer_reader_next_check(&buf->head)) {
		rc = mm_net_write(&sock->sock, ptr, len);
	} else {
		int iovcnt = 0;
		struct iovec iov[MM_NETBUF_MAXIOV];
		size_t nbytes = len;
		if (len) {
			iovcnt = 1;
			iov[0].iov_len = len;
			iov[0].iov_base = ptr;
		}

		struct mm_buffer_reader iter = buf->head;
		while (mm_buffer_reader_try_next(&iter)) {
			ptr = mm_buffer_reader_ptr(&iter);
			len = mm_buffer_reader_end(&iter) - ptr;

			nbytes += len;
			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = ptr;
			if (unlikely(++iovcnt == MM_NETBUF_MAXIOV))
				break;
		}

		if (nbytes)
			rc = mm_net_writev(&sock->sock, iov, iovcnt, len);
		else
			rc = 0;
	}

	if (rc > 0)
		mm_buffer_flush(buf, rc);

leave:
	DEBUG("rc: %ld", (long) rc);
	LEAVE();
	return rc;
}

void NONNULL(1, 2) FORMAT(2, 3)
mm_netbuf_printf(struct mm_netbuf_socket *sock, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(&sock->txbuf, fmt, va);
	va_end(va);
}
