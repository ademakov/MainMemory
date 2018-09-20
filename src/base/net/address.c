/*
 * base/net/address.c - MainMemory network addresses.
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

#include "base/net/address.h"

#include "base/report.h"

#include <arpa/inet.h>

/**********************************************************************
 * Network address manipulation routines.
 **********************************************************************/

socklen_t
mm_net_sockaddr_len(int sa_family)
{
	switch (sa_family) {
	case AF_UNIX:
		return sizeof(struct sockaddr_un);
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
	default:
		ABORT();
	}
}

bool NONNULL(1)
mm_net_parse_in_addr(struct sockaddr_in *addr, const char *addrstr, uint16_t port)
{
	if (addrstr == NULL || *addrstr == 0) {
		addr->sin_addr = (struct in_addr) { INADDR_ANY };
	} else {
		int rc = inet_pton(AF_INET, addrstr, &addr->sin_addr);
		if (rc != 1) {
			if (rc < 0)
				mm_fatal(errno, "IP address parsing failure: %s", addrstr);
			return false;
		}
	}
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	memset(addr->sin_zero, 0, sizeof addr->sin_zero);
	return true;
}

bool NONNULL(1)
mm_net_parse_in6_addr(struct sockaddr_in6 *addr, const char *addrstr, uint16_t port)
{
	if (addrstr == NULL || *addrstr == 0) {
		addr->sin6_addr = (struct in6_addr) IN6ADDR_ANY_INIT;
	} else {
		int rc = inet_pton(AF_INET6, addrstr, &addr->sin6_addr);
		if (rc != 1) {
			if (rc < 0)
				mm_fatal(errno, "IPv6 address parsing failure: %s", addrstr);
			return false;
		}
	}
	addr->sin6_family = AF_INET6;
	addr->sin6_port = htons(port);
	addr->sin6_flowinfo = 0;
	addr->sin6_scope_id = 0;
	return true;
}

bool NONNULL(1, 2)
mm_net_set_unix_addr(struct mm_net_addr *addr, const char *path)
{
	size_t len = strlen(path);
	if (len >= sizeof(addr->un_addr.sun_path))
		return false;
	memcpy(addr->un_addr.sun_path, path, len + 1);
	addr->un_addr.sun_family = AF_UNIX;
	return true;
}
