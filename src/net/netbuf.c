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

void
mm_netbuf_prepare(struct mm_netbuf_socket *sock)
{
	mm_buffer_prepare(&sock->rbuf);
	mm_buffer_prepare(&sock->tbuf);
}

void
mm_netbuf_cleanup(struct mm_netbuf_socket *sock)
{
	mm_buffer_cleanup(&sock->rbuf);
	mm_buffer_cleanup(&sock->tbuf);
}

ssize_t __attribute__((nonnull(1)))
mm_netbuf_fill(struct mm_netbuf_socket *sock)
{
	ENTER();
	ASSERT(mm_netbuf_core(sock) == mm_thread_self());

	struct mm_buffer *rbuf = &sock->rbuf;

	ssize_t n = 0;
	int iovcnt = 0;
	struct iovec iov[MM_NETBUF_MAXIOV];

	struct mm_slider cur;
	bool rc = mm_slider_first_free(&cur, rbuf);
	while (rc) {
		size_t len = cur.end - cur.ptr;
		if (likely(len)) {
			n += len;

			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = cur.ptr;
			++iovcnt;

			if (unlikely(iovcnt == MM_NETBUF_MAXIOV))
				break;
		}
		rc = mm_slider_next_free(&cur);
	}

	if (unlikely(n <= 0)) {
		n = -1;
		errno = EINVAL;
		goto leave;
	}

	if (iovcnt == 1) {
		n = mm_net_read(&sock->sock, iov[0].iov_base, iov[0].iov_len);
	} else {
		n = mm_net_readv(&sock->sock, iov, iovcnt, n);
	}
	if (n > 0) {
		mm_buffer_fill(rbuf, n);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t __attribute__((nonnull(1)))
mm_netbuf_flush(struct mm_netbuf_socket *sock)
{
	ENTER();
	ASSERT(mm_netbuf_core(sock) == mm_core_self());

	struct mm_buffer *tbuf = &sock->tbuf;

	ssize_t n = 0;
	int iovcnt = 0;
	struct iovec iov[MM_NETBUF_MAXIOV];

	struct mm_slider cur;
	bool rc = mm_slider_first_used(&cur, tbuf);
	while (rc) {
		size_t len = cur.end - cur.ptr;
		if (likely(len)) {
			n += len;

			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = cur.ptr;
			++iovcnt;

			if (unlikely(iovcnt == MM_NETBUF_MAXIOV))
				break;
		}
		rc = mm_slider_next_used(&cur);
	}

	if (unlikely(n <= 0)) {
		n = -1;
		errno = EINVAL;
		goto leave;
	}

	if (iovcnt == 1) {
		n = mm_net_write(&sock->sock, iov[0].iov_base, iov[0].iov_len);
	} else {
		n = mm_net_writev(&sock->sock, iov, iovcnt, n);
	}
	if (n > 0) {
		mm_buffer_flush(tbuf, n);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t __attribute__((nonnull(1, 2)))
mm_netbuf_read(struct mm_netbuf_socket *sock, void *buffer, size_t nbytes)
{
	return mm_buffer_read(&sock->rbuf, buffer, nbytes);
}

ssize_t __attribute__((nonnull(1, 2)))
mm_netbuf_write(struct mm_netbuf_socket *sock, const void *data, size_t size)
{
	return mm_buffer_write(&sock->tbuf, data, size);
}

void __attribute__((nonnull(1, 2))) __attribute__((format(printf, 2, 3)))
mm_netbuf_printf(struct mm_netbuf_socket *sock, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	mm_buffer_vprintf(&sock->tbuf, fmt, va);
	va_end(va);
}
