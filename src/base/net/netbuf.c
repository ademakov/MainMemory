/*
 * net/netbuf.c - MainMemory buffered network I/O.
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

#include "base/net/netbuf.h"

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
	// Save the current write position.
	struct mm_buffer_writer writer;
	mm_buffer_writer_save(&writer, buf);

	// Construct I/O vector using buffer segments.
	int iovcnt = 1;
	size_t room = n;
	struct iovec iov[MM_NETBUF_MAXIOV];
	iov[0].iov_len = n;
	iov[0].iov_base = p;
	do {
		n = mm_buffer_writer_bump(&writer, buf, size - room);
		p = mm_buffer_segment_internal_data(writer.seg);

		room += n;
		iov[iovcnt].iov_len = n;
		iov[iovcnt].iov_base = p;
		++iovcnt;

	} while ((room < size) && (iovcnt < MM_NETBUF_MAXIOV));

	// Perform the read operation.
	return mm_net_readv(&sock->sock, iov, iovcnt, room);
}

static __attribute__((noinline)) ssize_t
mm_netbuf_flush_iov(struct mm_netbuf_socket *sock, struct mm_buffer *buf, uint32_t n, char *p)
{
	// Save the current read position.
	struct mm_buffer_reader reader;
	mm_buffer_reader_save(&reader, buf);

	// Construct I/O vector using buffer segments.
	int iovcnt = 1;
	size_t size = n;
	struct iovec iov[MM_NETBUF_MAXIOV];
	iov[0].iov_len = n;
	iov[0].iov_base = p;
	do {
		n = mm_buffer_reader_next(&reader, buf);
		if (n == 0)
			break;
		p = mm_buffer_reader_ptr(&reader);

		size += n;
		iov[iovcnt].iov_len = n;
		iov[iovcnt].iov_base = p;
		++iovcnt;

	} while (iovcnt < MM_NETBUF_MAXIOV);

	// Perform the write operation.
	return mm_net_writev(&sock->sock, iov, iovcnt, size);
}

ssize_t NONNULL(1)
mm_netbuf_fill(struct mm_netbuf_socket *sock, size_t size)
{
	ENTER();
	ssize_t rc;
	struct mm_buffer *buf = &sock->rxbuf;

	// Make sure that there is a viable buffer segment.
	uint32_t n = mm_buffer_writer_make_ready(buf, size);
	char *p = mm_buffer_writer_ptr(&buf->tail);

	if (n >= size) {
		// Try to read using the current segment.
		rc = mm_net_read(&sock->sock, p, n);

		// On success bump the occupied data size.
		if (rc > 0) {
			buf->tail.seg->size += rc;
			mm_buffer_reader_ready(buf);
		}
	} else {
		// Try to read using multiple segments.
		rc = mm_netbuf_fill_iov(sock, size, buf, n, p);

		// On success mark the segments occupied by data.
		if (rc > 0) {
			size = rc;
			n = mm_buffer_writer_room(&buf->tail);
			while (n < size) {
				buf->tail.seg->size += n;
				size -= n;

				n = mm_buffer_writer_next(&buf->tail);
				VERIFY(n);
			}
			buf->tail.seg->size += size;
			mm_buffer_reader_ready(buf);
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
	ssize_t rc = 0;
	struct mm_buffer *buf = &sock->txbuf;

	// Ensure that at least one buffer segment is present.
	uint32_t n = mm_buffer_reader_ready(buf);
	if (n == 0)
		goto leave;
	char *p = mm_buffer_reader_ptr(&buf->head);

	if (mm_buffer_reader_last(&buf->head, buf)) {
		// Try to write using the current segment.
		rc = mm_net_write(&sock->sock, p, n);

		// On success bump the consumed data size.
		if (rc > 0)
			buf->head.ptr += rc;
	} else {
		// Try to write using multiple segments.
		rc = mm_netbuf_flush_iov(sock, buf, n, p);

		// On success skip the consumed segment.
		if (rc > 0) {
			size_t size = rc;
			while (n < size && n) {
				buf->head.ptr += n;
				size -= n;

				n = mm_buffer_reader_next(&buf->head, buf);
			}
			buf->head.ptr += size;
		}
	}

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
