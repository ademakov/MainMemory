/*
 * net/netbuf.h - MainMemory buffered network I/O.
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

#ifndef NET_NETBUF_H
#define NET_NETBUF_H

#include "common.h"
#include "base/mem/buffer.h"
#include "base/mem/slider.h"
#include "net/net.h"

struct mm_netbuf_socket
{
	/* The client socket. */
	struct mm_net_socket sock;
	/* Receive buffer. */
	struct mm_buffer rbuf;
	/* Transmit buffer. */
	struct mm_buffer tbuf;
};

void mm_netbuf_prepare(struct mm_netbuf_socket *sock)
        __attribute__((nonnull(1)));

void mm_netbuf_cleanup(struct mm_netbuf_socket *sock)
        __attribute__((nonnull(1)));

ssize_t __attribute__((nonnull(1)))
mm_netbuf_fill(struct mm_netbuf_socket *sock);

ssize_t __attribute__((nonnull(1)))
mm_netbuf_flush(struct mm_netbuf_socket *sock);

ssize_t __attribute__((nonnull(1, 2)))
mm_netbuf_read(struct mm_netbuf_socket *sock, void *buffer, size_t nbytes);

ssize_t __attribute__((nonnull(1, 2)))
mm_netbuf_write(struct mm_netbuf_socket *sock, const char *data, size_t size);

static inline mm_core_t
mm_netbuf_core(struct mm_netbuf_socket *sock)
{
	return sock->sock.event.core;
}

static inline void
mm_netbuf_read_reset(struct mm_netbuf_socket *sock)
{
	mm_buffer_rectify(&sock->rbuf);
}

static inline void
mm_netbuf_write_reset(struct mm_netbuf_socket *sock)
{
	mm_buffer_rectify(&sock->tbuf);
}

static inline void
mm_netbuf_demand(struct mm_netbuf_socket *sock, size_t size)
{
	mm_buffer_demand(&sock->rbuf, size);
}

static inline void
mm_netbuf_reduce(struct mm_netbuf_socket *sock, size_t size)
{
	mm_buffer_flush(&sock->rbuf, size);
}

static inline bool
mm_netbuf_read_empty(struct mm_netbuf_socket *sock)
{
	return mm_buffer_empty(&sock->rbuf);
}

static inline bool
mm_netbuf_read_first(struct mm_netbuf_socket *sock, struct mm_slider *cur)
{
	return mm_slider_first_used(cur, &sock->rbuf);
}

void __attribute__((nonnull(1, 2))) __attribute__((format(printf, 2, 3)))
mm_netbuf_printf(struct mm_netbuf_socket *sock, const char *restrict fmt, ...);

static inline void
mm_netbuf_splice(struct mm_netbuf_socket *sock, char *data, size_t size,
		 mm_buffer_release_t release, uintptr_t release_data)
{
	mm_buffer_splice(&sock->tbuf, data, size, release, release_data);
}

static inline void
mm_netbuf_close(struct mm_netbuf_socket *sock)
{
	mm_net_close(&sock->sock);
}

#endif /* NET_NETBUF_H */
