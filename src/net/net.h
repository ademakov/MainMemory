/*
 * net/net.h - MainMemory networking.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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

#ifndef NET_NET_H
#define NET_NET_H

#include "common.h"
#include "base/bitset.h"
#include "base/list.h"
#include "base/event/event.h"
#include "core/work.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>

/* Forward declaration. */
struct mm_task;

/* Protocol flags. */
#define MM_NET_INBOUND		0x000001
#define MM_NET_OUTBOUND		0x000002

/* Client flags. */
#define MM_NET_CLIENT		0x000004
#define MM_NET_CONNECTING	0x000008

/* Socket close flags. */
#define MM_NET_CLOSED		0x000010
#define MM_NET_READER_SHUTDOWN	0x000020
#define MM_NET_WRITER_SHUTDOWN	0x000040

/* Socket event dispatch is bound to a certain thread. */
#define MM_NET_BOUND_EVENTS	0x000080

/* Socket I/O status flags. */
#define MM_NET_READ_READY	0x000100
#define MM_NET_WRITE_READY	0x000200
#define MM_NET_READ_ERROR	0x000400
#define MM_NET_WRITE_ERROR	0x000800
#define MM_NET_READER_SPAWNED	0x001000
#define MM_NET_WRITER_SPAWNED	0x002000
#define MM_NET_READER_PENDING	0x004000
#define MM_NET_WRITER_PENDING	0x008000

/* Connection options. */
#define MM_NET_KEEPALIVE	0x010000
#define MM_NET_NODELAY		0x020000

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
	/* Event handling data. */
	struct mm_event_fd event;

	/* Protocol handlers. */
	struct mm_net_proto *proto;

	/* Core affinity. */
	struct mm_bitset affinity;

	/* Server name. */
	char *name;
	/* Server address. */
	struct mm_net_addr addr;
};

/* Network client socket data. */
struct mm_net_socket
{
	/* Event handling data. */
	struct mm_event_fd event;

	/* Tasks bound to perform socket I/O. */
	struct mm_task *reader;
	struct mm_task *writer;

	/* I/O timeouts. */
	mm_timeout_t read_timeout;
	mm_timeout_t write_timeout;

	/* Socket flags. */
	uint32_t flags;

	/* Work items for I/O tasks. */
	struct mm_work read_work;
	struct mm_work write_work;
	struct mm_work reclaim_work;

	/* Socket protocol handlers. */
	struct mm_net_proto *proto;

	/* A client socket destruction routine. */
	void (*destroy)(struct mm_net_socket *);

	/* Client address. */
	struct mm_net_peer_addr peer;
};

/* Protocol handler. */
struct mm_net_proto
{
	uint32_t flags;

	struct mm_net_socket * (*create)(void);
	void (*destroy)(struct mm_net_socket *);

	bool (*detach)(struct mm_net_socket *);

	void (*reader)(struct mm_net_socket *);
	void (*writer)(struct mm_net_socket *);
};

/**********************************************************************
 * Network subsystem initialization and termination.
 **********************************************************************/

void mm_net_init(void);
void mm_net_term(void);

/**********************************************************************
 * Network address manipulation routines.
 **********************************************************************/

bool NONNULL(1, 2)
mm_net_set_unix_addr(struct mm_net_addr *addr, const char *path);

bool NONNULL(1)
mm_net_set_inet_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port);

bool NONNULL(1)
mm_net_set_inet6_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port);

/**********************************************************************
 * Network servers.
 **********************************************************************/

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_unix_server(const char *name, struct mm_net_proto *proto, const char *path);

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_inet_server(const char *name, struct mm_net_proto *proto, const char *addrstr, uint16_t port);

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_inet6_server(const char *name, struct mm_net_proto *proto, const char *addrstr, uint16_t port);

void NONNULL(1, 2)
mm_net_set_server_affinity(struct mm_net_server *srv, struct mm_bitset *mask);

void NONNULL(1)
mm_net_start_server(struct mm_net_server *srv);

void NONNULL(1)
mm_net_stop_server(struct mm_net_server *srv);

/**********************************************************************
 * Network I/O tasks for server sockets.
 **********************************************************************/

void NONNULL(1)
mm_net_spawn_reader(struct mm_net_socket *sock);
void NONNULL(1)
mm_net_spawn_writer(struct mm_net_socket *sock);

void NONNULL(1)
mm_net_yield_reader(struct mm_net_socket *sock);
void NONNULL(1)
mm_net_yield_writer(struct mm_net_socket *sock);

/**********************************************************************
 * Network client connection sockets.
 **********************************************************************/

void NONNULL(1, 2)
mm_net_prepare(struct mm_net_socket *sock, void (*destroy)(struct mm_net_socket *));

struct mm_net_socket *
mm_net_create(void);

/*
 * BEWARE!!!
 *
 * As long as a socket was successfully connected it becomes registered in
 * the event loop. Therefore it is forbidden to destroy a connected socket.
 * This function is to be called only if the socket failed to connect.
 *
 * A connected socket is automatically destroyed at an appropriate moment
 * after closing it with mm_net_close(). In turn closing a socket makes any
 * access to it after that point dangerous.
 */
void NONNULL(1)
mm_net_destroy(struct mm_net_socket *sock);

int NONNULL(1, 2)
mm_net_connect(struct mm_net_socket *sock, const struct mm_net_addr *addr);

int NONNULL(1, 2)
mm_net_connect_inet(struct mm_net_socket *sock, const char *addrstr, uint16_t port);

int NONNULL(1, 2)
mm_net_connect_inet6(struct mm_net_socket *sock, const char *addrstr, uint16_t port);

/**********************************************************************
 * Network socket I/O.
 **********************************************************************/

ssize_t NONNULL(1, 2)
mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes);
ssize_t NONNULL(1, 2)
mm_net_write(struct mm_net_socket *sock, const void *buffer, size_t nbytes);

ssize_t NONNULL(1, 2)
mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt, ssize_t nbytes);
ssize_t NONNULL(1, 2)
mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt, ssize_t nbytes);

void NONNULL(1)
mm_net_close(struct mm_net_socket *sock);
void NONNULL(1)
mm_net_reset(struct mm_net_socket *sock);

void NONNULL(1)
mm_net_shutdown_reader(struct mm_net_socket *sock);
void NONNULL(1)
mm_net_shutdown_writer(struct mm_net_socket *sock);

static inline bool NONNULL(1)
mm_net_is_closed(struct mm_net_socket *sock)
{
	return (sock->flags & MM_NET_CLOSED) != 0;
}

static inline bool NONNULL(1)
mm_net_is_reader_shutdown(struct mm_net_socket *sock)
{
	return (sock->flags & (MM_NET_CLOSED | MM_NET_READER_SHUTDOWN)) != 0;
}

static inline bool NONNULL(1)
mm_net_is_writer_shutdown(struct mm_net_socket *sock)
{
	return (sock->flags & (MM_NET_CLOSED | MM_NET_WRITER_SHUTDOWN)) != 0;
}

static inline void NONNULL(1)
mm_net_set_read_timeout(struct mm_net_socket *sock, mm_timeout_t timeout)
{
	sock->read_timeout = timeout;
}

static inline void NONNULL(1)
mm_net_set_write_timeout(struct mm_net_socket *sock, mm_timeout_t timeout)
{
	sock->write_timeout = timeout;
}

#endif /* NET_NET_H */
