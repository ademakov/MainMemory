/*
 * net.h - MainMemory networking.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef NET_H
#define NET_H

#include "common.h"
#include "event.h"
#include "list.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>

/* Forward declarations. */
struct mm_port;
struct mm_task;

/* Net task communication codes. */
typedef enum {
	MM_NET_MSG_REGISTER,
	MM_NET_MSG_UNREGISTER,
	MM_NET_MSG_READ_READY,
	MM_NET_MSG_WRITE_READY,
	MM_NET_MSG_READ_ERROR,
	MM_NET_MSG_WRITE_ERROR,
	MM_NET_MSG_SPAWN_READER,
	MM_NET_MSG_SPAWN_WRITER,
	MM_NET_MSG_YIELD_READER,
	MM_NET_MSG_YIELD_WRITER,
} mm_net_msg_t;

/* Protocol flags. */
#define MM_NET_INBOUND		0x0001
#define MM_NET_OUTBOUND		0x0002

/* Socket flags. */
#define MM_NET_CLOSED		0x0001
#define MM_NET_READ_READY	0x0010
#define MM_NET_WRITE_READY	0x0020
#define MM_NET_READ_ERROR	0x0040
#define MM_NET_WRITE_ERROR	0x0080
#define MM_NET_READER_SPAWNED	0x0100
#define MM_NET_WRITER_SPAWNED	0x0200
#define MM_NET_READER_PENDING	0x0400
#define MM_NET_WRITER_PENDING	0x0800

/* Socket address. */
struct mm_net_addr
{
	union
	{
		struct sockaddr addr;
		struct sockaddr_un un_addr;	/* Unix-domain socket address. */
		struct sockaddr_in in_addr;	/* IPv4 socket address. */
		struct sockaddr_in6 in6_addr;	/* IPv6 socket address. */
	};
};

/* Socket peer address. */
struct mm_net_peer_addr
{
	union
	{
		struct sockaddr addr;
		struct sockaddr_in in_addr;
		struct sockaddr_in6 in6_addr;
	};
};

/* Network server data. */
struct mm_net_server
{
	/* Server socket. */
	int fd;
	/* Server flags. */
	int flags;

	/* I/O event handler IDs. */
	mm_event_hid_t input_handler;
	mm_event_hid_t output_handler;
	mm_event_hid_t control_handler;

	/* I/O event handler task. */
	struct mm_task *io_task;
	struct mm_port *io_port;

	/* A list of all client sockets. */
	struct mm_list clients;

	/* Protocol handlers. */
	struct mm_net_proto *proto;

	/* Server name. */
	char *name;
	/* Server address. */
	struct mm_net_addr addr;
};

/* Network client socket data. */
struct mm_net_socket
{
	/* Socket file descriptor. */
	int fd;

	/* Socket flags. */
	uint32_t flags;

	/* I/O timeouts. */
	mm_timeout_t read_timeout;
	mm_timeout_t write_timeout;

	/* Tasks pending on the socket I/O. */
	struct mm_task *reader;
	struct mm_task *writer;

	/* Protocol data. */
	intptr_t proto_data;

	/* Socket server. */
	struct mm_net_server *server;

	/* A link in the server's list of all client sockets. */
	struct mm_list clients;

	/* Client address. */
	struct mm_net_peer_addr peer;
};

/* Protocol handler. */
struct mm_net_proto
{
	int flags;

	void (*prepare)(struct mm_net_socket *sock);
	void (*cleanup)(struct mm_net_socket *sock);

	void (*reader)(struct mm_net_socket *sock);
	void (*writer)(struct mm_net_socket *sock);
};

void mm_net_init(void);
void mm_net_term(void);

struct mm_net_server *mm_net_create_unix_server(const char *name, const char *path)
	__attribute__((nonnull(1, 2)));
struct mm_net_server *mm_net_create_inet_server(const char *name, const char *addrstr, uint16_t port)
	__attribute__((nonnull(1, 2)));
struct mm_net_server *mm_net_create_inet6_server(const char *name, const char *addrstr, uint16_t port)
	__attribute__((nonnull(1, 2)));

void mm_net_start_server(struct mm_net_server *srv, struct mm_net_proto *proto)
	__attribute__((nonnull(1, 2)));

ssize_t mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes);
ssize_t mm_net_write(struct mm_net_socket *sock, const void *buffer, size_t nbytes);

ssize_t mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt);
ssize_t mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt);

void mm_net_spawn_reader(struct mm_net_socket *sock);
void mm_net_spawn_writer(struct mm_net_socket *sock);

void mm_net_close(struct mm_net_socket *sock);

static inline bool
mm_net_is_closed(struct mm_net_socket *sock)
{
	return (sock->flags & MM_NET_CLOSED) != 0;
}

static inline bool
mm_net_is_readable(struct mm_net_socket *sock)
{
	return (sock->flags & (MM_NET_CLOSED | MM_NET_READ_READY))
		== MM_NET_READ_READY;
}

static inline bool
mm_net_is_writable(struct mm_net_socket *sock)
{
	return (sock->flags & (MM_NET_CLOSED | MM_NET_WRITE_READY))
		== MM_NET_WRITE_READY;
}

static inline void
mm_net_set_read_timeout(struct mm_net_socket *sock, mm_timeout_t timeout)
{
	sock->read_timeout = timeout;
}

static inline void
mm_net_set_write_timeout(struct mm_net_socket *sock, mm_timeout_t timeout)
{
	sock->write_timeout = timeout;
}

#endif /* NET_H */
