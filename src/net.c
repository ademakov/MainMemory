/*
 * net.c - MainMemory networking.
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

#include "net.h"

#include "event.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

/**********************************************************************
 * Address manipulation routines.
 **********************************************************************/

static inline socklen_t
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

static int __attribute__((nonnull(1, 2)))
mm_net_set_un_addr(struct mm_net_addr *addr, const char *path)
{
	ENTER();
	ASSERT(path != NULL);

	int rc = 0;

	size_t len = strlen(path);
	if (len < sizeof(addr->un_addr.sun_path)) {
		memcpy(addr->un_addr.sun_path, path, len + 1);
		addr->un_addr.sun_family = AF_UNIX;
	} else {
		mm_error(0, "unix-domain socket path is too long.");
		rc = -1;
	}

	LEAVE();
	return rc;
}

static int __attribute__((nonnull(1)))
mm_net_set_in_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	ENTER();

	int rc = 0;

	if (addrstr && *addrstr) {
		int pr = inet_pton(AF_INET, addrstr, &addr->in_addr.sin_addr);
		if (pr != 1) {
			if (pr < 0)
				mm_fatal(errno, "IP address parsing failure");

			mm_error(0, "IP address parsing failure");
			rc = -1;
			goto done;
		}
	} else {
		addr->in_addr.sin_addr = (struct in_addr) { INADDR_ANY };
	}
	addr->in_addr.sin_family = AF_INET;
	addr->in_addr.sin_port = htons(port);
	memset(addr->in_addr.sin_zero, 0, sizeof addr->in_addr.sin_zero);

done:
	LEAVE();
	return rc;
}

static int __attribute__((nonnull(1)))
mm_net_set_in6_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	ENTER();

	int rc = 0;

	if (addrstr && *addrstr) {
		int pr = inet_pton(AF_INET6, addrstr, &addr->in6_addr.sin6_addr);
		if (pr != 1) {
			if (pr < 0)
				mm_fatal(errno, "IPv6 address parsing failure");

			mm_error(0, "IPv6 address parsing failure");
			rc = -1;
			goto done;
		}
	} else {
		addr->in6_addr.sin6_addr = (struct in6_addr) IN6ADDR_ANY_INIT;
	}
	addr->in6_addr.sin6_family = AF_INET6;
	addr->in6_addr.sin6_port = htons(port);
	addr->in6_addr.sin6_flowinfo = 0;
	addr->in6_addr.sin6_scope_id = 0;

done:
	LEAVE();
	return rc;
}

/**********************************************************************
 * Socket helper routines.
 **********************************************************************/

static void
mm_net_set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		mm_fatal(errno, "fcntl(..., F_GETFL, ...)");

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		mm_fatal(errno, "fcntl(..., F_SETFL, ...)");
}

static int __attribute__((nonnull(1)))
mm_net_open_server_socket(struct mm_net_addr *addr, int backlog)
{
	ENTER();

	/* Create the socket. */
	int sock = socket(addr->addr.sa_family, SOCK_STREAM, 0);
	if (sock < 0)
		mm_fatal(errno, "socket()");
	if (mm_event_verify_fd(sock) != MM_FD_VALID)
		mm_fatal(0, "server socket no is too high: %d", sock);

	/* Set socket options. */
	int val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) < 0)
		mm_fatal(errno, "setsockopt(..., SO_REUSEADDR, ...)");
	if (addr->addr.sa_family == AF_INET6
	    && setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) < 0)
		mm_fatal(errno, "setsockopt(..., IPV6_V6ONLY, ...)");

	/* Bind the socket to the given address. */
	socklen_t salen = mm_net_sockaddr_len(addr->addr.sa_family);
	if (bind(sock, &addr->addr, salen) < 0)
		mm_fatal(errno, "bind()");

	/* Make the socket ready to accept connections. */
	if (listen(sock, backlog > 0 ? backlog : SOMAXCONN) < 0)
		mm_fatal(errno, "listen()");

	/* Make the socket non-blocking. */
	mm_net_set_nonblocking(sock);

	TRACE("sock: %d", sock);
	LEAVE();
	return sock;
}

static void __attribute__((nonnull(1)))
mm_net_remove_unix_socket(struct mm_net_addr *addr)
{
	ENTER();

	if (addr->addr.sa_family == AF_UNIX) {
		mm_print("removing %s", addr->un_addr.sun_path);
		if (unlink(addr->un_addr.sun_path) < 0)
			mm_error(errno, "unlink(\"%s\")", addr->un_addr.sun_path);
	}

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_net_close_server_socket(struct mm_net_addr *addr, int sock)
{
	ENTER();
	TRACE("sock: %d", sock);

	/* Close the socket. */
	close(sock);

	/* Remove the Unix-domain socket file. */
	mm_net_remove_unix_socket(addr);

	LEAVE();
}

/**********************************************************************
 * Server table.
 **********************************************************************/

static struct mm_net_server *mm_srv_table;
static uint32_t mm_srv_table_size;
static uint32_t mm_srv_count;

static inline size_t
mm_net_server_index(struct mm_net_server *srv)
{
	ASSERT(srv < (mm_srv_table + mm_srv_count));
	ASSERT(srv >= mm_srv_table);
	return srv - mm_srv_table;
}

static void
mm_net_init_server_table(void)
{
	mm_srv_table_size = 4;
	mm_srv_table = mm_alloc(mm_srv_table_size * sizeof(struct mm_net_server));
	mm_srv_count = 0;
}

static void
mm_net_free_server_table(void)
{
	mm_free(mm_srv_table);
}

static struct mm_net_server *
mm_net_alloc_server(void)
{
	if (mm_srv_table_size == mm_srv_count) {
		mm_srv_table_size += 4;
		mm_srv_table = mm_realloc(
			mm_srv_table,
			mm_srv_table_size * sizeof(struct mm_net_server));
	}

	struct mm_net_server *srv = &mm_srv_table[mm_srv_count++];
	srv->sock = -1;

	return srv;
}

/**********************************************************************
 * Client table.
 **********************************************************************/

#define MM_CLI_NIL ((uint32_t) -1)

static struct mm_net_client *mm_cli_table;
static uint32_t mm_cli_table_size;
static uint32_t mm_cli_count;
static uint32_t mm_cli_free_index;

static inline size_t
mm_net_client_index(struct mm_net_client *cli)
{
	ASSERT(cli < (mm_cli_table + mm_cli_count));
	ASSERT(cli >= mm_cli_table);
	return cli - mm_cli_table;
}

static void
mm_net_init_client_table(void)
{
	mm_cli_table_size = 100;
	mm_cli_table = mm_alloc(mm_cli_table_size * sizeof(struct mm_net_client));
	mm_cli_count = 0;
	mm_cli_free_index = MM_CLI_NIL;
}

static void
mm_net_free_client_table(void)
{
	mm_free(mm_cli_table);
}

static struct mm_net_client *
mm_net_alloc_client(void)
{
	struct mm_net_client *cli;
	if (mm_cli_free_index != MM_CLI_NIL) {
		cli = &mm_cli_table[mm_cli_free_index];
		mm_cli_free_index = cli->free_index;
	} else {
		if (unlikely(mm_cli_table_size == mm_cli_count)) {
			/* Check for integer overflow. */
			uint32_t size = mm_cli_table_size * 2;
			if (unlikely(size < mm_cli_table_size))
				return NULL;

			mm_cli_table_size = size;
			mm_print("client table size: %lu", (unsigned long) mm_cli_table_size);
			mm_cli_table = mm_realloc(
				mm_cli_table,
				mm_cli_table_size * sizeof(struct mm_net_client));
		}
		cli = &mm_cli_table[mm_cli_count++];
	}
	return cli;
}

static void
mm_net_free_client(struct mm_net_client *cli)
{
	cli->free_index = mm_cli_free_index;
	mm_cli_free_index = mm_net_client_index(cli);
}

/**********************************************************************
 * Net I/O event handlers.
 **********************************************************************/

static mm_event_id mm_net_accept_id;
static mm_event_id mm_net_read_id;
static mm_event_id mm_net_write_id;

static void
mm_net_accept_event(mm_event event, uintptr_t ident, uint32_t data)
{
	ENTER();
	ASSERT(data < mm_srv_count);

	/* Find the pertinent server. */
	struct mm_net_server *srv = &mm_srv_table[data];
	ASSERT(srv->proto != NULL);

	/* Accept a client socket. */
	struct sockaddr_storage sa;
	socklen_t salen = sizeof sa;
	int sock = accept(srv->sock, (struct sockaddr *) &sa, &salen);
	if (unlikely(sock < 0)) {
		mm_error(errno, "accept()");
		goto done;
	}
	if (unlikely(mm_event_verify_fd(sock) != MM_FD_VALID)) {
		mm_error(0, "socket no is too high: %d", sock);
		close(sock);
		goto done;
	}

	/* Allocate a new client structure. */
	struct mm_net_client *cli = mm_net_alloc_client();
	if (cli == NULL) {
		mm_error(0, "client table overflow");
		close(sock);
		goto done;
	}

	/* Initialize the client structure. */
	cli->sock = sock;
	cli->flags = (MM_NET_READ_READY | MM_NET_WRITE_READY);
	cli->srv = srv;
	if (sa.ss_family == AF_INET)
		memcpy(&cli->peer.in_addr, &sa, sizeof(cli->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&cli->peer.in6_addr, &sa, sizeof(cli->peer.in6_addr));
	else
		cli->peer.addr.sa_family = sa.ss_family;

	/* Set the socket options. */
	int val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., TCP_NODELAY, ...)");

	/* Make the socket non-blocking. */
	mm_net_set_nonblocking(sock);

	/* Register the socket with event loop. */
	mm_event_register_fd(cli->sock, mm_net_read_id, mm_net_write_id);
	mm_event_set_fd_data(cli->sock, (uint32_t) mm_net_client_index(cli));

done:
	LEAVE();
}

static void
mm_net_read_event(mm_event event, uintptr_t ident, uint32_t data)
{
	ENTER();
	ASSERT(data < mm_cli_count);

	/* Find the pertinent client. */
	struct mm_net_client *cli = &mm_cli_table[data];

	char buf[1026];
	int n = read(cli->sock, buf, sizeof(buf));
	if (n <= 0) {
		if (n < 0)
			mm_error(errno, "read()");
		mm_event_unregister_fd(cli->sock);
		close(cli->sock);
	}

	LEAVE();
}

static void
mm_net_write_event(mm_event event, uintptr_t ident, uint32_t data)
{
	ENTER();
	ASSERT(data < mm_cli_count);

	/* Find the pertinent client. */
	struct mm_net_client *cli = &mm_cli_table[data];

	write(cli->sock, "test\n", 5);
	mm_event_unregister_fd(cli->sock);
	close(cli->sock);

	LEAVE();
}

static void
mm_net_init_handlers(void)
{
	ENTER();

	mm_net_accept_id = mm_event_install_handler(mm_net_accept_event);
	mm_net_read_id = mm_event_install_handler(mm_net_read_event);
	mm_net_write_id = mm_event_install_handler(mm_net_write_event);

	LEAVE();
}

/**********************************************************************
 * Network initialization.
 **********************************************************************/

static int mm_net_initialized = 0;

void
mm_net_init(void)
{
	ENTER();

	mm_net_init_server_table();
	mm_net_init_client_table();
	mm_net_init_handlers();

	mm_net_initialized = 1;

	LEAVE();
}

void
mm_net_free(void)
{
	ENTER();

	mm_net_initialized = 0;

	for (int i = 0; i < mm_srv_count; i++) {
		struct mm_net_server *srv = &mm_srv_table[i];
		if (srv->sock >= 0)
			mm_net_close_server_socket(&srv->addr, srv->sock);
	}

	mm_net_free_client_table();
	mm_net_free_server_table();

	LEAVE();
}

void
mm_net_exit(void)
{
	ENTER();

	if (mm_net_initialized) {
		for (int i = 0; i < mm_srv_count; i++) {
			struct mm_net_server *srv = &mm_srv_table[i];
			if (srv->sock >= 0)
				mm_net_remove_unix_socket(&srv->addr);
		}
	}

	LEAVE();
}

struct mm_net_server *
mm_net_create_unix_server(const char *path)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_un_addr(&srv->addr, path) < 0)
		mm_fatal(0, "invalid server socket address");

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet_server(const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_in_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "invalid server socket address");

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet6_server(const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_in6_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "invalid server socket address");

	LEAVE();
	return srv;
}

void
mm_net_set_server_proto(struct mm_net_server *srv, struct mm_net_proto *proto, uintptr_t proto_data)
{
	ENTER();

	srv->proto = proto;
	srv->proto_data = proto_data;

	LEAVE();
}

void
mm_net_start_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->sock == -1);

	/* Create the server socket. */
	srv->sock = mm_net_open_server_socket(&srv->addr, 0);

	/* Register the socket with event loop. */
	mm_event_register_fd(srv->sock, mm_net_accept_id, 0);
	mm_event_set_fd_data(srv->sock, (uint32_t) mm_net_server_index(srv));

	LEAVE();
}

void
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->sock != -1);

	/* Unregister the socket. */
	mm_event_unregister_fd(srv->sock);

	/* Close the socket. */
	mm_net_close_server_socket(&srv->addr, srv->sock);
	srv->sock = -1;

	LEAVE();
}
