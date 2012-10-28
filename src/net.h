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

#ifndef MM_NET_H
#define MM_NET_H

#include <event.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

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
	int sock;
	/* Server socket address. */
	struct mm_net_addr addr;
};

void mm_net_init(void);
void mm_net_free(void);
void mm_net_exit(void);

struct mm_net_server *mm_net_create_unix_server(const char *path) __attribute__((nonnull(1)));
struct mm_net_server *mm_net_create_inet_server(const char *addrstr, uint16_t port);
struct mm_net_server *mm_net_create_inet6_server(const char *addrstr, uint16_t port);

void mm_net_start_server(struct mm_net_server *srv) __attribute__((nonnull(1)));
void mm_net_stop_server(struct mm_net_server *srv) __attribute__((nonnull(1)));

#endif
