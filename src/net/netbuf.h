/*
 * net/netbuf.h - MainMemory buffered network I/O.
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

#ifndef NET_NETBUF_H
#define NET_NETBUF_H

#include "common.h"
#include "base/log/debug.h"
#include "base/memory/buffer.h"
#include "net/net.h"

struct mm_netbuf_socket
{
	/* The client socket. */
	struct mm_net_socket sock;
	/* Receive buffer. */
	struct mm_buffer rxbuf;
	/* Transmit buffer. */
	struct mm_buffer txbuf;
};

static inline mm_thread_t NONNULL(1)
mm_netbuf_thread(struct mm_netbuf_socket *sock)
{
	return mm_event_target(&sock->sock.event);
}

void NONNULL(1)
mm_netbuf_prepare(struct mm_netbuf_socket *sock);

void NONNULL(1)
mm_netbuf_cleanup(struct mm_netbuf_socket *sock);

ssize_t NONNULL(1)
mm_netbuf_fill(struct mm_netbuf_socket *sock, size_t cnt);

ssize_t NONNULL(1, 2)
mm_netbuf_read(struct mm_netbuf_socket *sock, void *buffer, size_t nbytes);

ssize_t NONNULL(1, 2)
mm_netbuf_write(struct mm_netbuf_socket *sock, const void *data, size_t size);

ssize_t NONNULL(1)
mm_netbuf_flush(struct mm_netbuf_socket *sock);

static inline size_t NONNULL(1)
mm_netbuf_empty(struct mm_netbuf_socket *sock)
{
	return mm_buffer_empty(&sock->rxbuf);
}

static inline size_t NONNULL(1)
mm_netbuf_getleft(struct mm_netbuf_socket *sock)
{
	return mm_buffer_getleft(&sock->rxbuf);
}

static inline void NONNULL(1, 2)
mm_netbuf_save_position(struct mm_netbuf_socket *sock, struct mm_buffer_position *pos)
{
	DEBUG("save read position");
	mm_buffer_position_save(pos, &sock->rxbuf);
}

static inline void NONNULL(1, 2)
mm_netbuf_restore_position(struct mm_netbuf_socket *sock, struct mm_buffer_position *pos)
{
	DEBUG("restore read position");
	mm_buffer_position_save(pos, &sock->rxbuf);
}

static inline bool NONNULL(1)
mm_netbuf_read_next(struct mm_netbuf_socket *sock)
{
	return mm_buffer_read_next(&sock->rxbuf);
}

static inline void NONNULL(1)
mm_netbuf_read_reset(struct mm_netbuf_socket *sock)
{
	mm_buffer_rectify(&sock->rxbuf);
}

static inline void NONNULL(1)
mm_netbuf_write_reset(struct mm_netbuf_socket *sock)
{
	mm_buffer_rectify(&sock->txbuf);
}

static inline ssize_t NONNULL(1)
mm_netbuf_reduce(struct mm_netbuf_socket *sock, size_t size)
{
	return mm_buffer_flush(&sock->rxbuf, size);
}

void NONNULL(1, 2) FORMAT(2, 3)
mm_netbuf_printf(struct mm_netbuf_socket *sock, const char *restrict fmt, ...);

static inline void NONNULL(1, 2)
mm_netbuf_splice(struct mm_netbuf_socket *sock, char *data, size_t size,
		 mm_buffer_release_t release, uintptr_t release_data)
{
	mm_buffer_splice(&sock->txbuf, data, size, size, release, release_data);
}

static inline void NONNULL(1)
mm_netbuf_close(struct mm_netbuf_socket *sock)
{
	mm_net_close(&sock->sock);
}

/**********************************************************************
 * Receive buffer in-place parsing support.
 **********************************************************************/

/* Get current read pointer. */
static inline char * NONNULL(1)
mm_netbuf_rptr(struct mm_netbuf_socket *sock)
{
	return sock->rxbuf.head.ptr;
}

/* Get current read segment end. */
static inline char * NONNULL(1)
mm_netbuf_rend(struct mm_netbuf_socket *sock)
{
	return sock->rxbuf.head.end;
}

static inline void NONNULL(1)
mm_netbuf_radd(struct mm_netbuf_socket *sock, size_t len)
{
	sock->rxbuf.head.ptr += len;
}

/* Ensure the current */
static inline bool NONNULL(1)
mm_netbuf_rspan(struct mm_netbuf_socket *sock, size_t nbytes)
{
	return mm_buffer_span(&sock->rxbuf, nbytes);
}

static inline char * NONNULL(1, 3)
mm_netbuf_rfind(struct mm_netbuf_socket *sock, int c, size_t *poffset)
{
	return mm_buffer_find(&sock->rxbuf, c, poffset);
}

#endif /* NET_NETBUF_H */
