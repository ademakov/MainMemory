/*
 * net.h - MainMemory networking.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

/* Forward declaration. */
struct mm_port;

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

/* Server flags. */
#define MM_NET_ACCEPT_READY	1

/* Network server data. */
struct mm_net_server
{
	/* Server socket. */
	int sock;
	/* Server flags. */
	int flags;

	/* A list of all clients. */
	struct mm_list clients;
	/* A list of read ready clients. */
	struct mm_list read_queue;
	/* A list of write ready clients. */
	struct mm_list write_queue;

	/* A link in the accept ready list. */
	struct mm_list accept_ready;

	/* Protocol handlers. */
	struct mm_net_proto *proto;
	struct mm_task *read_ready_task;
	struct mm_task *write_ready_task;
	struct mm_port *read_ready_port;
	struct mm_port *write_ready_port;
	mm_io_handler io_handler;

	/* Server name. */
	char *name;
	/* Server address. */
	struct mm_net_addr addr;
};

/* Client flags. */
#define MM_NET_READ_READY	0x01
#define MM_NET_WRITE_READY	0x02
#define MM_NET_READ_QUEUE	0x04
#define MM_NET_WRITE_QUEUE	0x08

/* Network client data. */
struct mm_net_client
{
	/* Client socket. */
	int sock;
	/* Client flags. */
	int flags;

	/* Client server. */
	struct mm_net_server *srv;

	/* A link in the server's list of all clients. */
	struct mm_list clients;
	/* A link in the server's list of read ready clients. */
	struct mm_list read_queue;
	/* A link in the server's list of write ready clients. */
	struct mm_list write_queue;

	/* Client address. */
	struct mm_net_peer_addr peer;
};

/* Protocol handler. */
struct mm_net_proto
{
	bool (*accept)(struct mm_net_client *client);
	void (*read_ready)(struct mm_net_client *client);
	void (*write_ready)(struct mm_net_client *client);
	void (*cleanup)(struct mm_net_client *client);
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
void mm_net_stop_server(struct mm_net_server *srv)
	__attribute__((nonnull(1)));

void mm_net_close(struct mm_net_client *cli);

#endif /* NET_H */
