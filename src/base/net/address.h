/*
 * base/net/address.h - MainMemory network addresses.
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

#ifndef BASE_NET_ADDRESS_H
#define BASE_NET_ADDRESS_H

#include "common.h"

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

/**********************************************************************
 * Network address manipulation routines.
 **********************************************************************/

socklen_t
mm_net_sockaddr_len(int sa_family);

bool NONNULL(1)
mm_net_parse_in_addr(struct sockaddr_in *addr, const char *addrstr, uint16_t port);

bool NONNULL(1)
mm_net_parse_in6_addr(struct sockaddr_in6 *addr, const char *addrstr, uint16_t port);

bool NONNULL(1, 2)
mm_net_set_unix_addr(struct mm_net_addr *addr, const char *path);

static inline bool NONNULL(1)
mm_net_set_inet_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	return mm_net_parse_in_addr(&addr->in_addr, addrstr, port);
}

static inline bool NONNULL(1)
mm_net_set_inet6_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	return mm_net_parse_in6_addr(&addr->in6_addr, addrstr, port);
}

#endif /* BASE_NET_ADDRESS_H */
