/*
 * memcache.c - MainMemory memcached protocol support.
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

#include "memcache.h"

#include "net.h"

static struct mm_net_server *mm_mc_server;

static void
mm_memcache_prepare(struct mm_net_socket *sock)
{
}

static void
mm_memcache_cleanup(struct mm_net_socket *sock)
{
}

static void
mm_memcache_read_ready(struct mm_net_socket *sock)
{
}

static void
mm_memcache_write_ready(struct mm_net_socket *sock)
{
}

void
mm_memcache_init(void)
{
	static struct mm_net_proto proto = {
		.prepare = mm_memcache_prepare,
		.cleanup = mm_memcache_cleanup,

		.read_ready = mm_memcache_read_ready,
		.write_ready = mm_memcache_write_ready,
	};

	mm_mc_server = mm_net_create_inet_server("memcache", "127.0.0.1", 11211);
	mm_net_start_server(mm_mc_server, &proto);
}

void
mm_memcache_term(void)
{
	mm_net_stop_server(mm_mc_server);
}
