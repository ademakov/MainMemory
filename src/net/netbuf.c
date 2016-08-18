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

#include "core/core.h"

#define MM_NETBUF_MAXIOV	64

void NONNULL(1)
mm_netbuf_prepare(struct mm_netbuf_socket *sock)
{
	mm_buffer_prepare(&sock->rxbuf);
	mm_buffer_prepare(&sock->txbuf);
}

void NONNULL(1)
mm_netbuf_cleanup(struct mm_netbuf_socket *sock)
{
	mm_buffer_cleanup(&sock->rxbuf);
	mm_buffer_cleanup(&sock->txbuf);
}

ssize_t NONNULL(1)
mm_netbuf_fill(struct mm_netbuf_socket *sock, size_t cnt)
{
	ENTER();
	ASSERT(mm_netbuf_thread(sock) == mm_thread_self());
	ssize_t rc;

	struct mm_buffer *buf = &sock->rxbuf;
	char *ptr = buf->tail.ptr;
	size_t len = buf->tail.end - ptr;
	while (len == 0 && mm_buffer_iterator_next_check(&buf->tail)) {
		mm_buffer_iterator_write_next_unsafe(&buf->tail);
		ptr = buf->tail.ptr;
		len = buf->tail.end - ptr;
	}

	if (len >= cnt) {
		rc = mm_net_read(&sock->sock, ptr, len);
	} else {
		int iovcnt = 0;
		struct iovec iov[MM_NETBUF_MAXIOV];
		size_t nbytes = len;
		if (len) {
			iovcnt = 1;
			iov[0].iov_len = len;
			iov[0].iov_base = ptr;
		}

		struct mm_buffer_iterator iter = buf->tail;
		do {
			if (!mm_buffer_iterator_write_next(&iter)) {
				mm_buffer_extend(buf, &iter, cnt - nbytes);
				iter = buf->tail;
			}

			ptr = iter.ptr;
			len = iter.end - ptr;

			nbytes += len;
			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = ptr;
			if (unlikely(++iovcnt == MM_NETBUF_MAXIOV))
				break;

		} while (unlikely(nbytes < cnt));

		if (nbytes)
			rc = mm_net_readv(&sock->sock, iov, iovcnt, nbytes);
		else
			rc = 0;
	}

	if (rc > 0)
		mm_buffer_fill(buf, rc);

	DEBUG("rc: %ld", (long) rc);
	LEAVE();
	return rc;
}

ssize_t NONNULL(1)
mm_netbuf_flush(struct mm_netbuf_socket *sock)
{
	ENTER();
	ASSERT(mm_netbuf_thread(sock) == mm_thread_self());
	ssize_t rc;

	struct mm_buffer *buf = &sock->txbuf;
	char *ptr = buf->head.ptr;
	size_t len = buf->head.end - ptr;
	while (len == 0 && mm_buffer_iterator_next_check(&buf->head)) {
		mm_buffer_iterator_read_next_unsafe(&buf->head);
		ptr = buf->head.ptr;
		len = buf->head.end - ptr;
	}

	if (!mm_buffer_iterator_next_check(&buf->head)) {
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

		struct mm_buffer_iterator iter = buf->head;
		while (mm_buffer_iterator_read_next(&iter)) {

			ptr = iter.ptr;
			len = iter.end - ptr;

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

	DEBUG("rc: %ld", (long) rc);
	LEAVE();
	return rc;
}

ssize_t NONNULL(1, 2)
mm_netbuf_read(struct mm_netbuf_socket *sock, void *buffer, size_t nbytes)
{
	return mm_buffer_read(&sock->rxbuf, buffer, nbytes);
}

ssize_t NONNULL(1, 2)
mm_netbuf_write(struct mm_netbuf_socket *sock, const void *data, size_t size)
{
	return mm_buffer_write(&sock->txbuf, data, size);
}

void NONNULL(1, 2) FORMAT(2, 3)
mm_netbuf_printf(struct mm_netbuf_socket *sock, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(&sock->txbuf, fmt, va);
	va_end(va);
}
