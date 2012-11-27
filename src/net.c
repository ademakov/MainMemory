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
#include "pool.h"
#include "port.h"
#include "task.h"
#include "util.h"

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

static struct mm_pool mm_cli_pool;

static void
mm_net_init_client_table(void)
{
	ENTER();

	mm_pool_init(&mm_cli_pool, sizeof (struct mm_net_client));

	LEAVE();
}

static void
mm_net_free_client_table(void)
{
	ENTER();

	mm_pool_discard(&mm_cli_pool);

	LEAVE();
}

static struct mm_net_client *
mm_net_create_client(void)
{
	ENTER();

	struct mm_net_client *cli = mm_pool_alloc(&mm_cli_pool);

	LEAVE();
	return cli;
}

static void
mm_net_destroy_client(struct mm_net_client *cli)
{
	ENTER();

	mm_pool_free(&mm_cli_pool, cli);

	LEAVE();
}

/**********************************************************************
 * Net I/O routines.
 **********************************************************************/

#define MM_ACCEPT_COUNT		10
#define MM_IO_COUNT		10

static struct mm_port *mm_accept_port;
static mm_io_handler mm_accept_handler;

static void
mm_net_accept(void *dummy __attribute__((unused)))
{
	ENTER();

	/* Client socket data. */
	int sock;
	socklen_t salen;
	struct sockaddr_storage sa;

	/* Repeat count. */
	uint32_t count = 0;
	/* The server index received from I/O handler. */
	uint32_t index;
next:
	/* Receive a ready server index. */
	if (mm_port_receive(mm_accept_port, &index, 1) < 0) {
		goto done;
	}
	ASSERT(index < mm_srv_count);

	/* Find the pertinent server. */
	struct mm_net_server *srv = &mm_srv_table[index];
	ASSERT(srv->proto != NULL);

	/* Accept a client socket. */
retry:
	salen = sizeof sa;
	sock = accept(srv->sock, (struct sockaddr *) &sa, &salen);
	if (unlikely(sock < 0)) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			mm_error(errno, "accept()");
		goto next;
	}
	if (unlikely(mm_event_verify_fd(sock) != MM_FD_VALID)) {
		mm_error(0, "socket no is too high: %d", sock);
		close(sock);
		goto done;
	}

	/* Allocate a new client structure. */
	struct mm_net_client *cli = mm_net_create_client();
	if (cli == NULL) {
		mm_error(0, "client table overflow");
		close(sock);
		goto done;
	}

	/* Initialize the client structure. */
	cli->sock = sock;
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

	/* Register the socket with the event loop. */
	uint32_t cli_index = mm_pool_ptr2idx(&mm_cli_pool, cli);
	mm_event_register_fd(cli->sock, srv->io_handler, cli_index);

	/* Let the protocol layer to check the socket. */
	if (srv->proto->accept && !(srv->proto->accept)(cli)) {
		mm_error(0, "connection refused");
		mm_net_destroy_client(cli);
		// TODO: set linger off and/or close concurrently to avoid stalls.
		close(sock);
		goto done;
	}

	/* Check to see if there is some more work. */
	if (++count < MM_ACCEPT_COUNT)
		goto next;
done:
	LEAVE();
}

static void
mm_net_read_ready(struct mm_net_server *srv)
{
	ENTER();

	/* Repeat count. */
	uint32_t count = 0;
	/* The client index received from I/O handler. */
	uint32_t index;
next:
	/* Receive a ready client index. */
	if (mm_port_receive(srv->read_ready_port, &index, 1) < 0) {
		goto done;
	}

	/* Find the pertinent client. */
	struct mm_net_client *cli = mm_pool_idx2ptr(&mm_cli_pool, index);

	/* Handle read on the protocol layer. */
	(cli->srv->proto->read_ready)(cli);

	/* Check to see if there is some more work. */
	if (++count < MM_IO_COUNT)
		goto next;
done:
	LEAVE();
}

static void
mm_net_write_ready(struct mm_net_server *srv)
{
	ENTER();

	/* Repeat count. */
	uint32_t count = 0;
	/* The client index received from I/O handler. */
	uint32_t index;
next:
	/* Receive a ready client index. */
	if (mm_port_receive(srv->write_ready_port, &index, 1) < 0) {
		goto done;
	}

	/* Find the pertinent client. */
	struct mm_net_client *cli = mm_pool_idx2ptr(&mm_cli_pool, index);

	/* Handle write on the protocol layer. */
	(cli->srv->proto->write_ready)(cli);

	/* Check to see if there is some more work. */
	if (++count < MM_IO_COUNT)
		goto next;
done:
	LEAVE();
}

static void
mm_net_init_accept_task(void)
{
	/* Create accept task. */
	struct mm_task *task = mm_task_create(0, (mm_routine) mm_net_accept, 0);

	/* Create accept port. */
	mm_accept_port = mm_port_create(task);

	/* Queue the accept task. */
	mm_task_start(task);

	/* Add I/O handler. */
	mm_accept_handler = mm_event_add_io_handler(mm_accept_port, NULL);
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
	mm_net_init_accept_task();

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
		if (srv->sock >= 0) {
			mm_net_close_server_socket(&srv->addr, srv->sock);
		}
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
			if (srv->sock >= 0) {
				mm_net_remove_unix_socket(&srv->addr);
			}
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
mm_net_start_server(struct mm_net_server *srv, struct mm_net_proto *proto)
{
	ENTER();
	ASSERT(srv->sock == -1);

	/* Store protocol handlers. */
	srv->proto = proto;

	/* Create the server socket. */
	srv->sock = mm_net_open_server_socket(&srv->addr, 0);
	if (mm_event_verify_fd(srv->sock)) {
		mm_fatal(0, "socket no is too high: %d", srv->sock);
	}

	/* Create server tasks. */
	struct mm_task *read_ready_task = mm_task_create(
		0, (mm_routine) mm_net_read_ready, (intptr_t) srv);
	struct mm_task *write_ready_task = mm_task_create(
		0, (mm_routine) mm_net_write_ready, (intptr_t) srv);

	/* Create server ports. */
	srv->read_ready_port = mm_port_create(read_ready_task);
	srv->write_ready_port = mm_port_create(write_ready_task);

	/* Queue the server tasks. */
	mm_task_start(read_ready_task);
	mm_task_start(write_ready_task);

	/* Register the ports with the event loop. */
	srv->io_handler = mm_event_add_io_handler(srv->read_ready_port,
						  srv->write_ready_port);

	/* Register the socket with the event loop. */
	mm_event_register_fd(srv->sock, mm_accept_handler, (uint32_t) mm_net_server_index(srv));

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
