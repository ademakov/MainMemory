/*
 * net.c - MainMemory networking.
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

#include "net.h"

#include "alloc.h"
#include "core.h"
#include "event.h"
#include "exit.h"
#include "log.h"
#include "pool.h"
#include "port.h"
#include "task.h"
#include "sched.h"
#include "timer.h"
#include "trace.h"
#include "util.h"
#include "work.h"

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

static int __attribute__((nonnull(1)))
mm_net_open_server_socket(struct mm_net_addr *addr, int backlog)
{
	ENTER();

	/* Create the socket. */
	int sock = socket(addr->addr.sa_family, SOCK_STREAM, 0);
	if (sock < 0)
		mm_fatal(errno, "socket()");
	if (mm_event_verify_fd(sock) != MM_EVENT_FD_VALID)
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
	mm_set_nonblocking(sock);

	TRACE("sock: %d", sock);
	LEAVE();
	return sock;
}

static void __attribute__((nonnull(1)))
mm_net_remove_unix_socket(struct mm_net_addr *addr)
{
	ENTER();

	if (addr->addr.sa_family == AF_UNIX) {
		mm_brief("removing %s", addr->un_addr.sun_path);
		if (unlink(addr->un_addr.sun_path) < 0) {
			mm_error(errno, "unlink(\"%s\")", addr->un_addr.sun_path);
		}
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
	for (uint32_t i = 0; i < mm_srv_count; i++) {
		mm_free(mm_srv_table[i].name);
	}

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
	srv->fd = -1;
	srv->flags = 0;

	/* Initialize the client list. */
	mm_list_init(&srv->clients);

	return srv;
}

/**********************************************************************
 * Socket table.
 **********************************************************************/

static struct mm_pool mm_socket_pool;

static void
mm_net_init_socket_table(void)
{
	ENTER();

	mm_pool_init(&mm_socket_pool, "net-socket",
		     &mm_alloc_global, sizeof (struct mm_net_socket));

	LEAVE();
}

static void
mm_net_free_socket_table(void)
{
	ENTER();

	mm_pool_discard(&mm_socket_pool);

	LEAVE();
}

static struct mm_net_socket *
mm_net_create_socket(int fd, struct mm_net_server *srv)
{
	ENTER();

	/* Allocate the socket. */
	struct mm_net_socket *sock = mm_pool_alloc(&mm_socket_pool);

	/* Initialize the fields. */
	sock->fd = fd;
	sock->flags = 0;
	sock->read_timeout = MM_TIMEOUT_INFINITE;
	sock->write_timeout = MM_TIMEOUT_INFINITE;
	sock->proto_data = 0;
	sock->reader = NULL;
	sock->writer = NULL;
	sock->srv = srv;

	/* Register with the server. */
	mm_list_append(&srv->clients, &sock->clients);

	LEAVE();
	return sock;
}

static void
mm_net_destroy_socket(struct mm_net_socket *sock)
{
	ENTER();

	mm_list_delete(&sock->clients);
	mm_pool_free(&mm_socket_pool, sock);

	LEAVE();
}

/**********************************************************************
 * Network I/O event handling.
 **********************************************************************/

static void
mm_net_input_handler(mm_event_t event __attribute__((unused)),
		     uintptr_t handler_data, uint32_t data)
{
	struct mm_port *port = (struct mm_port *) handler_data;
	uint32_t msg[2] = { MM_NET_MSG_READ_READY, data };
	mm_port_send_blocking(port, msg, 2);
}

static void
mm_net_output_handler(mm_event_t event __attribute__((unused)),
		      uintptr_t handler_data, uint32_t data)
{
	struct mm_port *port = (struct mm_port *) handler_data;
	uint32_t msg[2] = { MM_NET_MSG_WRITE_READY, data };
	mm_port_send_blocking(port, msg, 2);
}

static void
mm_net_control_handler(mm_event_t event, uintptr_t handler_data, uint32_t data)
{
	struct mm_port *port = (struct mm_port *) handler_data;

	uint32_t net_msg;
	switch (event) {
	case MM_EVENT_REGISTER:
		net_msg = MM_NET_MSG_REGISTER;
		break;
	case MM_EVENT_UNREGISTER:
		net_msg = MM_NET_MSG_UNREGISTER;
		break;
	case MM_EVENT_INPUT_ERROR:
		net_msg = MM_NET_MSG_READ_ERROR;
		break;
	case MM_EVENT_OUTPUT_ERROR:
		net_msg = MM_NET_MSG_WRITE_ERROR;
		break;
	default:
		ABORT();
	}

	uint32_t msg[2] = { net_msg, data };
	mm_port_send_blocking(port, msg, 2);
}

/**********************************************************************
 * Server acceptor tasks.
 **********************************************************************/

#define MM_ACCEPT_COUNT		10

/* The accept event handler task. */
static struct mm_task *mm_net_accept_task;

/* The accept event handler port. */
static struct mm_port *mm_net_accept_port;

/* Accept event handler cookie. */
static mm_event_hid_t mm_net_accept_handler;

static bool
mm_net_accept(struct mm_net_server *srv)
{
	ENTER();

	bool rc = true;

	/* Client socket. */
	int fd;
	socklen_t salen;
	struct sockaddr_storage sa;

retry:
	/* Try to accept a connection. */
	salen = sizeof sa;
	fd = accept(srv->fd, (struct sockaddr *) &sa, &salen);
	if (unlikely(fd < 0)) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			mm_error(errno, "%s: accept()", srv->name);
		else
			rc = false;
		goto leave;
	}
	if (unlikely(mm_event_verify_fd(fd) != MM_EVENT_FD_VALID)) {
		mm_error(0, "%s: socket no is too high: %d", srv->name, fd);
		close(fd);
		goto leave;
	}

	/* Set the socket options. */
	int val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., TCP_NODELAY, ...)");

	/* Make the socket non-blocking. */
	mm_set_nonblocking(fd);

	/* Allocate a new socket structure. */
	struct mm_net_socket *sock = mm_net_create_socket(fd, srv);
	if (sock == NULL) {
		mm_error(0, "%s: socket table overflow", srv->name);
		close(fd);
		goto leave;
	}

	/* Initialize the socket structure. */
	if (sa.ss_family == AF_INET)
		memcpy(&sock->peer.in_addr, &sa, sizeof(sock->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&sock->peer.in6_addr, &sa, sizeof(sock->peer.in6_addr));
	else
		sock->peer.addr.sa_family = sa.ss_family;

	/* Register the socket with the event loop. */
	uint32_t sock_index = mm_pool_ptr2idx(&mm_socket_pool, sock);
	mm_event_register_fd(sock->fd,
			     sock_index,
			     srv->input_handler,
			     srv->output_handler,
			     srv->control_handler);

leave:
	LEAVE();
	return rc;
}

static mm_result_t
mm_net_acceptor(uintptr_t arg)
{
	ENTER();

	/* Find the pertinent server. */
	struct mm_net_server *srv = &mm_srv_table[arg];

	/* Accept incoming connections. */
	while (mm_net_accept(srv)) {
		mm_sched_yield();
	}

	LEAVE();
	return 0;
}

static mm_result_t
mm_net_accept_loop(uintptr_t dummy __attribute__((unused)))
{
	ENTER();

	int count = 0;
	uintptr_t items[MM_ACCEPT_COUNT];

	bool block = true;

	/* Handle events on server sockets. */
	for (;;) {
		uint32_t msg[2];

		if (block ? count != 0 : count == MM_ACCEPT_COUNT) {
			mm_work_addv(0, mm_net_acceptor, items, count);
			count = 0;
		}

		if (block) {
			block = false;
			mm_port_receive_blocking(mm_net_accept_port, msg, 2);
		} else if (mm_port_receive(mm_net_accept_port, msg, 2) < 0) {
			block = true;
			continue;
		}

		if (msg[0] == MM_NET_MSG_READ_READY) {
			items[count++] = msg[1];
		}
	}

	LEAVE();
	return 0;
}

static void
mm_net_init_accept_task(void)
{
	ENTER();

	// Create the event handler task.
	mm_net_accept_task = mm_task_create("net-accept", 0, mm_net_accept_loop, 0);

	// Make the task priority higher.
	mm_net_accept_task->priority /= 2;

	// Create the event handler port.
	mm_net_accept_port = mm_port_create(mm_net_accept_task);

	// Register I/O handlers.
	mm_net_accept_handler = mm_event_register_handler(
		mm_net_input_handler, (uintptr_t) mm_net_accept_port);

	LEAVE();
}

static void
mm_net_term_accept_task()
{
	ENTER();

	// TODO: Destroy the event handler task.
	//mm_task_destroy(mm_net_accept_task);

	LEAVE();
}

/**********************************************************************
 * Socket I/O tasks.
 **********************************************************************/

#define MM_IO_COUNT		10

static void
mm_net_attach_reader(struct mm_net_socket *sock)
{
	ASSERT(sock->reader == NULL);
	sock->reader = mm_running_task;
}

static void
mm_net_detach_reader(struct mm_net_socket *sock)
{
	ASSERT(sock->reader == mm_running_task);
	sock->reader = NULL;
}

static void
mm_net_attach_writer(struct mm_net_socket *sock)
{
	ASSERT(sock->writer == NULL);
	sock->writer = mm_running_task;
}

static void
mm_net_detach_writer(struct mm_net_socket *sock)
{
	ASSERT(sock->writer == mm_running_task);
	sock->writer = NULL;
}

static void
mm_net_reset_read_ready(struct mm_net_socket *sock)
{
	sock->flags &= ~MM_NET_READ_READY;
}

static void
mm_net_reset_write_ready(struct mm_net_socket *sock)
{
	sock->flags &= ~MM_NET_WRITE_READY;
}

void
mm_net_spawn_reader(struct mm_net_socket *sock)
{
	ENTER();

	if (mm_net_is_closed(sock))
		goto done;

	uint32_t id = mm_pool_ptr2idx(&mm_socket_pool, sock);
	uint32_t msg[2] = { MM_NET_MSG_SPAWN_READER, id };
	mm_port_send_blocking(sock->srv->io_port, msg, 2);

done:
	LEAVE();
}

void
mm_net_spawn_writer(struct mm_net_socket *sock)
{
	ENTER();

	if (mm_net_is_closed(sock))
		goto done;

	uint32_t id = mm_pool_ptr2idx(&mm_socket_pool, sock);
	uint32_t msg[2] = { MM_NET_MSG_SPAWN_WRITER, id };
	mm_port_send_blocking(sock->srv->io_port, msg, 2);

done:
	LEAVE();
}

static void
mm_net_yield_reader(struct mm_net_socket *sock)
{
	ENTER();

	if (mm_net_is_closed(sock))
		goto done;

	uint32_t id = mm_pool_ptr2idx(&mm_socket_pool, sock);
	uint32_t msg[2] = { MM_NET_MSG_YIELD_READER, id };
	mm_port_send_blocking(sock->srv->io_port, msg, 2);

done:
	LEAVE();
}

static void
mm_net_yield_writer(struct mm_net_socket *sock)
{
	ENTER();

	if (mm_net_is_closed(sock))
		goto done;

	uint32_t id = mm_pool_ptr2idx(&mm_socket_pool, sock);
	uint32_t msg[2] = { MM_NET_MSG_YIELD_WRITER, id };
	mm_port_send_blocking(sock->srv->io_port, msg, 2);

done:
	LEAVE();
}

static void
mm_net_reader_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	/* Enable creating new reader tasks on the socket if it was so far
	   bound to this one. */
	if ((mm_running_task->flags & MM_TASK_READING) != 0) {
		// TODO: check that the socket is bound to mm_running_task?
		mm_running_task->flags &= ~MM_TASK_READING;

		mm_net_yield_reader(sock);
	}

	LEAVE();
}

static mm_result_t
mm_net_reader(uintptr_t arg)
{
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, arg);

	// Ensure the task yields socket on exit.
	mm_task_cleanup_push(mm_net_reader_cleanup, sock);

	// Run the protocol handler routine.
	(sock->srv->proto->reader_routine)(sock);

	// Yield the socket on return.
	mm_task_cleanup_pop(true);
	return 0;
}

static void
mm_net_writer_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	/* Enable creating new writer tasks on the socket if it was so far
	   bound to this one. */
	if ((mm_running_task->flags & MM_TASK_WRITING) != 0) {
		// TODO: check that the socket is bound to mm_running_task?
		mm_running_task->flags &= ~MM_TASK_WRITING;

		mm_net_yield_writer(sock);
	}

	LEAVE();
}

static mm_result_t
mm_net_writer(uintptr_t arg)
{
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, arg);

	// Ensure the task yields socket on exit.
	mm_task_cleanup_push(mm_net_writer_cleanup, sock);

	// Run the protocol handler routine.
	(sock->srv->proto->writer_routine)(sock);

	// Yield the socket on return.
	mm_task_cleanup_pop(true);
	return 0;
}

static mm_result_t
mm_net_io_loop(uintptr_t arg)
{
	ENTER();

	/* Find the pertinent server. */
	struct mm_net_server *srv = (struct mm_net_server *) arg;

	int read_count = 0;
	uintptr_t read_items[MM_IO_COUNT];

	int write_count = 0;
	uintptr_t write_items[MM_IO_COUNT];

	bool block = true;

	/* Check if spawn a reader as soon as the socket becomes read-ready
	   (otherwise a mm_net_spawn_reader() call is needed). */
	int rf = 0;
	if ((srv->proto->flags & MM_NET_INBOUND) != 0)
		rf = MM_NET_READER_PENDING;

	/* Check if spawn a writer as soon as the socket becomes write-ready
	   (otherwise a mm_net_spawn_writer() call is needed). */
	int wf = 0;
	if ((srv->proto->flags & MM_NET_OUTBOUND) != 0)
		wf = MM_NET_WRITER_PENDING;

#define MM_NET_IS_READER_PENDING(sock_flags)					\
	(((sock_flags) & (MM_NET_READER_SPAWNED | MM_NET_READER_PENDING))	\
	 == MM_NET_READER_PENDING)
#define MM_NET_IS_WRITER_PENDING(sock_flags)					\
	(((sock_flags) & (MM_NET_WRITER_SPAWNED | MM_NET_WRITER_PENDING))	\
	 == MM_NET_WRITER_PENDING)

#define MM_NET_MAY_SPAWN_READER(sock_flags)					\
	(((sock_flags) & (MM_NET_READ_READY | MM_NET_READ_ERROR)) != 0		\
	 && ((sock_flags) & MM_NET_READER_SPAWNED) == 0)
#define MM_NET_MAY_SPAWN_WRITER(sock_flags)					\
	(((sock_flags) & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR)) != 0	\
	 && ((sock_flags) & MM_NET_WRITER_SPAWNED) == 0)

#define MM_NET_RESPAWN_READER(sock_flags)					\
	(((sock_flags) & (MM_NET_READ_READY | MM_NET_READ_ERROR)) != 0		\
	 && ((sock_flags) & MM_NET_READER_PENDING) != 0)
#define MM_NET_RESPAWN_WRITER(sock_flags)					\
	(((sock_flags) & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR)) != 0	\
	 && ((sock_flags) & MM_NET_WRITER_PENDING) != 0)

	/* Handle I/O events. */
	for (;;) {
		uint32_t msg[2];

		/* Submit read work. */
		if (block ? read_count != 0 : read_count == MM_IO_COUNT) {
			mm_work_addv(MM_TASK_READING, mm_net_reader, read_items, read_count);
			read_count = 0;
		}

		/* Submit write work. */
		if (block ? write_count != 0 : write_count == MM_IO_COUNT) {
			mm_work_addv(MM_TASK_WRITING, mm_net_writer, write_items, write_count);
			write_count = 0;
		}

		/* Get I/O event. */
		if (block) {
			block = false;
			mm_port_receive_blocking(srv->io_port, msg, 2);
		} else if (mm_port_receive(srv->io_port, msg, 2) < 0) {
			block = true;
			continue;
		}

		/* Find the pertinent socket. */
		struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, msg[1]);

		/* Handle the event. */
		switch (msg[0]) {
		case MM_NET_MSG_REGISTER:
			ASSERT((sock->flags & MM_NET_CLOSED) == 0);

			/* Let the protocol layer prepare the socket data. */
			if (srv->proto->prepare != NULL)
				(srv->proto->prepare)(sock);
			break;

		case MM_NET_MSG_UNREGISTER:
			ASSERT((sock->flags & MM_NET_CLOSED) != 0);

			/* Let the protocol layer cleanup the socket data. */
			if (srv->proto->cleanup != NULL)
				(srv->proto->cleanup)(sock);

			/* Close the socket. */
			// TODO: set linger off and/or close concurrently to avoid stalls.
			close(sock->fd);

			/* Remove the socket from the server lists. */
			mm_net_destroy_socket(sock);
			break;

		case MM_NET_MSG_READ_READY:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			sock->flags |= MM_NET_READ_READY;
			if (sock->reader != NULL) {
				mm_sched_run(sock->reader);
			} else if (MM_NET_IS_READER_PENDING(sock->flags | rf)) {
				read_items[read_count++] = msg[1];
				sock->flags |= MM_NET_READER_SPAWNED;
				sock->flags &= ~MM_NET_READER_PENDING;
			}
			break;

		case MM_NET_MSG_WRITE_READY:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			sock->flags |= MM_NET_WRITE_READY;
			if (sock->writer != NULL) {
				mm_sched_run(sock->writer);
			} else if (MM_NET_IS_WRITER_PENDING(sock->flags | wf)) {
				write_items[write_count++] = msg[1];
				sock->flags |= MM_NET_WRITER_SPAWNED;
				sock->flags &= ~MM_NET_WRITER_PENDING;
			}
			break;

		case MM_NET_MSG_READ_ERROR:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			sock->flags |= MM_NET_READ_ERROR;
			if (sock->reader != NULL) {
				mm_sched_run(sock->reader);
			} else if (MM_NET_IS_READER_PENDING(sock->flags | rf)) {
				read_items[read_count++] = msg[1];
				sock->flags |= MM_NET_READER_SPAWNED;
				sock->flags &= ~MM_NET_READER_PENDING;
			}
			break;

		case MM_NET_MSG_WRITE_ERROR:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			sock->flags |= MM_NET_WRITE_ERROR;
			if (sock->writer != NULL) {
				mm_sched_run(sock->writer);
			} else if (MM_NET_IS_WRITER_PENDING(sock->flags | wf)) {
				write_items[write_count++] = msg[1];
				sock->flags |= MM_NET_WRITER_SPAWNED;
				sock->flags &= ~MM_NET_WRITER_PENDING;
			}
			break;

		case MM_NET_MSG_SPAWN_READER:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			if (MM_NET_MAY_SPAWN_READER(sock->flags)) {
				read_items[read_count++] = msg[1];
				sock->flags |= MM_NET_READER_SPAWNED;
			} else {
				sock->flags |= MM_NET_READER_PENDING;
			}
			break;

		case MM_NET_MSG_SPAWN_WRITER:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			if (MM_NET_MAY_SPAWN_WRITER(sock->flags)) {
				write_items[write_count++] = msg[1];
				sock->flags |= MM_NET_WRITER_SPAWNED;
			} else {
				sock->flags |= MM_NET_WRITER_PENDING;
			}
			break;

		case MM_NET_MSG_YIELD_READER:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			ASSERT((sock->flags & MM_NET_READER_SPAWNED) != 0);
			if (MM_NET_RESPAWN_READER(sock->flags | rf)) {
				read_items[read_count++] = msg[1];
				sock->flags &= ~MM_NET_READER_PENDING;
			} else {
				sock->flags &= ~MM_NET_READER_SPAWNED;
			}
			break;

		case MM_NET_MSG_YIELD_WRITER:
			if ((sock->flags & MM_NET_CLOSED) != 0)
				break;

			ASSERT((sock->flags & MM_NET_WRITER_SPAWNED) != 0);
			if (MM_NET_RESPAWN_WRITER(sock->flags | wf)) {
				write_items[write_count++] = msg[1];
				sock->flags &= ~MM_NET_WRITER_PENDING;
			} else {
				sock->flags &= ~MM_NET_WRITER_SPAWNED;
			}
			break;

		default:
			mm_brief("%x %x", msg[0], msg[1]);
			ABORT();
		}
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Network initialization and termination.
 **********************************************************************/

static int mm_net_initialized = 0;

static void
mm_net_exit_cleanup(void)
{
	ENTER();

	if (!mm_net_initialized)
		goto done;

	for (uint32_t i = 0; i < mm_srv_count; i++) {
		struct mm_net_server *srv = &mm_srv_table[i];
		if (srv->fd >= 0) {
			mm_net_remove_unix_socket(&srv->addr);
		}
	}

done:
	LEAVE();
}

void
mm_net_init(void)
{
	ENTER();

	mm_atexit(mm_net_exit_cleanup);

	mm_net_init_server_table();
	mm_net_init_socket_table();

	mm_core_hook_start(mm_net_init_accept_task);

	mm_net_initialized = 1;

	LEAVE();
}

void
mm_net_term(void)
{
	ENTER();

	mm_net_initialized = 0;

	for (uint32_t i = 0; i < mm_srv_count; i++) {
		struct mm_net_server *srv = &mm_srv_table[i];
		if (srv->fd >= 0) {
			mm_net_close_server_socket(&srv->addr, srv->fd);
		}

		// TODO: close client sockets
	}

	mm_net_term_accept_task();
	mm_net_free_socket_table();
	mm_net_free_server_table();

	LEAVE();
}

/**********************************************************************
 * Network servers.
 **********************************************************************/

struct mm_net_server *
mm_net_create_unix_server(const char *name, const char *path)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_un_addr(&srv->addr, path) < 0)
		mm_fatal(0, "failed to create '%s' server with path '%s'",
			 name, path);

	srv->name = mm_asprintf("%s (%s)", name, path);

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet_server(const char *name, const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_in_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'",
			 name, addrstr, port);

	srv->name = mm_asprintf("%s (%s:%d)", name, addrstr, port);

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet6_server(const char *name, const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	if (mm_net_set_in6_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'",
			 name, addrstr, port);

	srv->name = mm_asprintf("%s (%s:%d)", name, addrstr, port);

	LEAVE();
	return srv;
}

static void
mm_net_start_server_hook(void *arg)
{
	ENTER();

	struct mm_net_server *srv = arg;

	// Create the event handler task.
	srv->io_task = mm_task_create("net-io", 0, mm_net_io_loop, (intptr_t) srv);

	// Make the task priority higher.
	srv->io_task->priority /= 2;

	// Create the event handler port.
	srv->io_port = mm_port_create(srv->io_task);

	// Allocate an event handler ID for the port.
	srv->input_handler = mm_event_register_handler(
		mm_net_input_handler, (uintptr_t) srv->io_port);
	srv->output_handler = mm_event_register_handler(
		mm_net_output_handler, (uintptr_t) srv->io_port);
	srv->control_handler = mm_event_register_handler(
		mm_net_control_handler, (uintptr_t) srv->io_port);

	// Register the server socket with the event loop.
	mm_event_register_fd(srv->fd,
			     (uint32_t) mm_net_server_index(srv),
			     mm_net_accept_handler, 0, 0);

	LEAVE();
}

static void
mm_net_stop_server_hook(void *arg)
{
	ENTER();

	struct mm_net_server *srv = arg;
	ASSERT(srv->fd != -1);

	mm_brief("stop server: %s", srv->name);

	// Unregister the socket.
	mm_event_unregister_fd(srv->fd);

	// TODO: Destroy the event handler task.
	// mm_task_destroy(srv->io_task);

	// Close the socket.
	mm_net_close_server_socket(&srv->addr, srv->fd);
	srv->fd = -1;

	LEAVE();

}

void
mm_net_start_server(struct mm_net_server *srv, struct mm_net_proto *proto)
{
	ENTER();
	ASSERT(srv->fd == -1);

	mm_brief("start server '%s'", srv->name);

	// Store the protocol handlers.
	srv->proto = proto;

	// Create the server socket.
	srv->fd = mm_net_open_server_socket(&srv->addr, 0);

	// Register the server start hook.
	mm_core_hook_param_start(mm_net_start_server_hook, srv);

	// Register the server stop hook.
	mm_core_hook_param_stop(mm_net_stop_server_hook, srv);

	LEAVE();
}

/**********************************************************************
 * Network sockets.
 **********************************************************************/

static void
mm_net_rblock(struct mm_net_socket *sock)
{
	ENTER();

	// Register the current task as reader.
	mm_net_attach_reader(sock);

	// Block the task waiting to become read ready.
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		mm_timer_block(sock->read_timeout);
	else
		mm_sched_block();

	// Unregister the task as reader.
	mm_net_detach_reader(sock);

	// Check if the task is canceled.
	mm_task_testcancel();

	LEAVE();
}

static void
mm_net_wblock(struct mm_net_socket *sock)
{
	ENTER();

	// Register the current task as reader.
	mm_net_attach_writer(sock);

	// Block the task waiting to become write ready.
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		mm_timer_block(sock->write_timeout);
	else
		mm_sched_block();

	// Unregister the task as reader.
	mm_net_detach_writer(sock);

	// Check if the task is canceled.
	mm_task_testcancel();

	LEAVE();
}

static int
mm_net_may_rblock(struct mm_net_socket *sock, mm_timeval_t start)
{
	// Check to see if it is allowed to block on reading from a socket.
	if ((sock->flags & (MM_NET_CLOSED | MM_NET_READ_ERROR | MM_NET_NONBLOCK)) == 0) {
		if (sock->read_timeout != MM_TIMEOUT_INFINITE
		    && (start + sock->read_timeout) < mm_core->time_value) {
			// Cannot block as there is no time left.
			errno = ETIMEDOUT;
		} else {
			// Okay to block.
			return 1;
		}
	} else if ((sock->flags & (MM_NET_CLOSED | MM_NET_READ_ERROR)) == 0) {
		// Cannot block as the socket is in the non-block mode.
		errno = EAGAIN;
	} else if ((sock->flags & MM_NET_CLOSED) != 0) {
		// Cannot block as the socket is closed.
		errno = EBADF;
	} else {
		// Cannot block as there was an error on the socket.
		// FIXME: find the actual error errno?
		return 0;
	}
	return -1;
}

static int
mm_net_may_wblock(struct mm_net_socket *sock, mm_timeval_t start)
{
	// Check to see if it is allowed to block on writing to a socket.
	if ((sock->flags & (MM_NET_CLOSED | MM_NET_WRITE_ERROR | MM_NET_NONBLOCK)) == 0) {
		if (sock->write_timeout != MM_TIMEOUT_INFINITE
		    && (start + sock->write_timeout) < mm_core->time_value) {
			// Cannot block as there is no time left.
			errno = ETIMEDOUT;
		} else {
			// Okay to block.
			return 1;
		}
	} else if ((sock->flags & (MM_NET_CLOSED | MM_NET_WRITE_ERROR)) == 0) {
		// Cannot block as the socket is in the non-block mode.
		errno = EAGAIN;
	} else if ((sock->flags & MM_NET_CLOSED) != 0) {
		// Cannot block as the socket is closed.
		errno = EBADF;
	} else {
		// Cannot block as there was an error on the socket.
		// FIXME: find the actual error errno?
		return 0;
	}
	return -1;
}

ssize_t
mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes)
{
	ENTER();
	ssize_t n;

	// Remember the start time.
	mm_timeval_t start = mm_core->time_value;

retry:
	// Check to see if the socket is ready for reading.
	while (!mm_net_is_readable(sock)) {
		n = mm_net_may_rblock(sock, start);
		if (n <= 0) {
			goto done;
		}
		mm_net_rblock(sock);
	}

	// Try to read (nonblocking).
	n = read(sock->fd, buffer, nbytes);
	if (n > 0) {
		if ((size_t) n < nbytes) {
			mm_net_reset_read_ready(sock);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_read_ready(sock);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "read()");
			errno = saved_errno;
		}
	}

done:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_write(struct mm_net_socket *sock, const void *buffer, size_t nbytes)
{
	ENTER();
	ssize_t n;

	// Remember the start time.
	mm_timeval_t start = mm_core->time_value;

retry:
	// Check to see if the socket is ready for writing.
	while (!mm_net_is_writable(sock)) {
		n = mm_net_may_wblock(sock, start);
		if (n <= 0) {
			goto done;
		}
		mm_net_wblock(sock);
	}

	// Try to write (nonblocking).
	n = write(sock->fd, buffer, nbytes);
	if (n > 0) {
		if ((size_t) n < nbytes) {
			mm_net_reset_write_ready(sock);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_write_ready(sock);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "write()");
			errno = saved_errno;
		}
	}

done:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt)
{
	ENTER();
	ssize_t n;

	// Remember the start time.
	mm_timeval_t start = mm_core->time_value;

retry:
	// Check to see if the socket is ready for reading.
	while (!mm_net_is_readable(sock)) {
		n = mm_net_may_rblock(sock, start);
		if (n <= 0) {
			goto done;
		}
		mm_net_rblock(sock);
	}

	// Try to read (nonblocking).
	n = readv(sock->fd, iov, iovcnt);
	if (n > 0) {
		// FIXME: count nbytes in iov or do nothing here and let
		// the next call to hit EAGAIN.
#if 0
		if (n < nbytes) {
			mm_net_reset_read_ready(sock);
		}
#endif
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_read_ready(sock);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "readv()");
			errno = saved_errno;
		}
	}

done:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt)
{
	ENTER();
	ssize_t n;

	// Remember the start time.
	mm_timeval_t start = mm_core->time_value;

retry:
	// Check to see if the socket is ready for writing.
	while (!mm_net_is_writable(sock)) {
		n = mm_net_may_wblock(sock, start);
		if (n <= 0) {
			goto done;
		}
		mm_net_wblock(sock);
	}

	// Try to write (nonblocking).
	n = writev(sock->fd, iov, iovcnt);
	if (n > 0) {
		// FIXME: count nbytes in iov or do nothing here and let
		// the next call to hit EAGAIN.
#if 0
		if (n < nbytes) {
			mm_net_reset_write_ready(sock);
		}
#endif
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_write_ready(sock);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "writev()");
			errno = saved_errno;
		}
	}

done:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

void
mm_net_close(struct mm_net_socket *sock)
{
	ENTER();

	if ((sock->flags & MM_NET_CLOSED) == 0) {
		DEBUG("closing");

		sock->flags = MM_NET_CLOSED;

		// Notify a blocked reader/writer about closing.
		if (sock->reader != NULL && sock->reader != mm_running_task) {
			mm_sched_run(sock->reader);
			mm_sched_yield();
		}
		if (sock->writer != NULL && sock->writer != mm_running_task) {
			mm_sched_run(sock->writer);
			mm_sched_yield();
		}

		// Remove the socket from the event loop.
		mm_event_unregister_fd(sock->fd);

		/* TODO: Take care of work items and tasks that might still
		   refer to this socket. */
	}

	LEAVE();
}
