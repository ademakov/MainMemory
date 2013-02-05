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

#include "event.h"
#include "pool.h"
#include "port.h"
#include "task.h"
#include "sched.h"
#include "util.h"
#include "work.h"

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

	// Initialize client lists.
	mm_list_init(&srv->clients);
	mm_list_init(&srv->read_queue);
	mm_list_init(&srv->write_queue);

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

	mm_pool_init(&mm_socket_pool, "net-socket", sizeof (struct mm_net_socket));

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

	// Allocate the socket.
	struct mm_net_socket *sock = mm_pool_alloc(&mm_socket_pool);

	// Initialize the fields.
	sock->fd = fd;
	sock->proto_data = 0;
	sock->reader = NULL;
	sock->writer = NULL;
	sock->srv = srv;

	// Set the socket flags.
	sock->flags = 0;
	if (srv->proto->reader_routine != NULL)
		sock->flags |= MM_NET_READ_SPAWN;
	if (srv->proto->writer_routine != NULL)
		sock->flags |= MM_NET_WRITE_SPAWN;

	// Register with the server.
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
 * Socket I/O tasks.
 **********************************************************************/

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

static inline void
mm_net_bind_reader(struct mm_net_socket *sock)
{
	sock->flags &= ~MM_NET_READ_SPAWN;
}

static inline void
mm_net_bind_writer(struct mm_net_socket *sock)
{
	sock->flags &= ~MM_NET_WRITE_SPAWN;
}

void
mm_net_unbind_reader(struct mm_net_socket *sock)
{
	ENTER();

	// Enable creating new reader tasks on the socket if it was so far
	// bound to this one.
	if ((mm_running_task->flags & MM_TASK_READING) != 0) {
		// TODO: check that the socket is bound to mm_running_task?
		mm_running_task->flags &= ~MM_TASK_READING;
		sock->flags |= MM_NET_READ_SPAWN;
	}

	LEAVE();
}

void
mm_net_unbind_writer(struct mm_net_socket *sock)
{
	ENTER();

	// Enable creating new writer tasks on the socket if it was so far
	// bound to this one.
	if ((mm_running_task->flags & MM_TASK_WRITING) != 0) {
		// TODO: check that the socket is bound to mm_running_task?
		mm_running_task->flags &= ~MM_TASK_WRITING;
		sock->flags |= MM_NET_WRITE_SPAWN;
	}

	LEAVE();
}

static void
mm_net_reader(uintptr_t arg)
{
	struct mm_net_socket *sock = (struct mm_net_socket *) arg;

	/* Make sure the socket will be unbound from the task. */
	mm_task_cleanup_push(mm_net_unbind_reader, sock);

	// Run the protocol handler routine.
	(sock->srv->proto->reader_routine)(sock);

	mm_task_cleanup_pop(true);
}

static void
mm_net_writer(uintptr_t arg)
{
	struct mm_net_socket *sock = (struct mm_net_socket *) arg;

	/* Make sure the socket will be unbound from the task. */
	mm_task_cleanup_push(mm_net_unbind_writer, sock);

	// Run the protocol handler routine.
	(sock->srv->proto->writer_routine)(sock);

	mm_task_cleanup_pop(true);
}

/**********************************************************************
 * Net I/O routines.
 **********************************************************************/

#define MM_ACCEPT_COUNT		10
#define MM_IO_COUNT		10

/* The accept event handler task. */
static struct mm_task *mm_net_accept_task;

/* The accept event handler port. */
static struct mm_port *mm_net_accept_port;

// I/O event handler cookies.
static mm_event_handler_t mm_net_accept_handler;

// The list of servers ready to accept connections.
static struct mm_list mm_accept_queue;

static void
mm_net_add_accept_ready(uint32_t index)
{
	ENTER();
	ASSERT(index < mm_srv_count);

	// Find the pertinent server and add it to the accept queue if needed.
	struct mm_net_server *srv = &mm_srv_table[index];
	if (likely((srv->flags & MM_NET_ACCEPT_QUEUE) == 0)) {
		mm_list_append(&mm_accept_queue, &srv->accept_queue);
		srv->flags |= MM_NET_ACCEPT_QUEUE;
	}

	LEAVE();
}

static void
mm_net_set_read_ready(struct mm_net_socket *sock)
{
	ENTER();

	if (sock->reader != NULL) {
		sock->flags |= MM_NET_READ_READY;
		mm_sched_run(sock->reader);
	} else if ((sock->flags & MM_NET_READ_SPAWN) != 0) {
		if (likely((sock->flags & MM_NET_READ_QUEUE) == 0))
			mm_list_append(&sock->srv->read_queue, &sock->read_queue);
		sock->flags |= (MM_NET_READ_READY | MM_NET_READ_QUEUE);
	} else {
		sock->flags |= MM_NET_READ_READY;
	}

	LEAVE();
}

static void
mm_net_reset_read_ready(struct mm_net_socket *sock)
{
	ENTER();

	if ((sock->flags & MM_NET_READ_QUEUE) != 0)
		mm_list_delete(&sock->read_queue);
	sock->flags &= ~(MM_NET_READ_READY | MM_NET_READ_QUEUE);

	LEAVE();
}

static void
mm_net_set_write_ready(struct mm_net_socket *sock)
{
	ENTER();

	if (sock->writer != NULL) {
		sock->flags |= MM_NET_WRITE_READY;
		mm_sched_run(sock->writer);
	} else if (sock->srv->proto->writer_routine != NULL) {
		if (likely((sock->flags & MM_NET_WRITE_QUEUE) == 0))
			mm_list_append(&sock->srv->write_queue, &sock->write_queue);
		sock->flags |= (MM_NET_WRITE_READY | MM_NET_WRITE_QUEUE);
	} else {
		sock->flags |= MM_NET_WRITE_READY;
	}

	LEAVE();
}

static void
mm_net_reset_write_ready(struct mm_net_socket *sock)
{
	ENTER();

	if ((sock->flags & MM_NET_WRITE_QUEUE) != 0)
		mm_list_delete(&sock->write_queue);
	sock->flags &= ~(MM_NET_WRITE_READY | MM_NET_WRITE_QUEUE);

	LEAVE();
}

static void
mm_net_accept(struct mm_net_server *srv)
{
	ENTER();
	ASSERT((srv->flags & MM_NET_ACCEPT_QUEUE) != 0);

	// Client socket.
	int fd;
	socklen_t salen;
	struct sockaddr_storage sa;

retry:
	// Accept a client socket.
	salen = sizeof sa;
	fd = accept(srv->fd, (struct sockaddr *) &sa, &salen);
	if (unlikely(fd < 0)) {
		if (errno == EINTR)
			goto retry;

		mm_list_delete(&srv->accept_queue);
		srv->flags &= ~MM_NET_ACCEPT_QUEUE;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			mm_error(errno, "%s: accept()", srv->name);
		goto done;
	}
	if (unlikely(mm_event_verify_fd(fd) != MM_FD_VALID)) {
		mm_error(0, "%s: socket no is too high: %d", srv->name, fd);
		close(fd);
		goto done;
	}

	// Set the socket options.
	int val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., TCP_NODELAY, ...)");

	// Make the socket non-blocking.
	mm_net_set_nonblocking(fd);

	// Allocate a new socket structure.
	struct mm_net_socket *sock = mm_net_create_socket(fd, srv);
	if (sock == NULL) {
		mm_error(0, "%s: socket table overflow", srv->name);
		close(fd);
		goto done;
	}

	// Initialize the socket structure.
	if (sa.ss_family == AF_INET)
		memcpy(&sock->peer.in_addr, &sa, sizeof(sock->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&sock->peer.in6_addr, &sa, sizeof(sock->peer.in6_addr));
	else
		sock->peer.addr.sa_family = sa.ss_family;

	// Register the socket with the event loop.
	uint32_t sock_index = mm_pool_ptr2idx(&mm_socket_pool, sock);
	mm_event_register_fd(sock->fd, srv->io_handler, sock_index);

done:
	LEAVE();
}

static void
mm_net_accept_loop(uintptr_t dummy __attribute__((unused)))
{
	ENTER();

	// Accept incoming connections.
	int accept_count = 0;
	for (;;) {

		// Accept a connection.
		if (!mm_list_empty(&mm_accept_queue)) {

			// Get the pertinent server.
			struct mm_list *link = mm_list_head(&mm_accept_queue);
			struct mm_net_server *srv = containerof(link, struct mm_net_server, accept_queue);

			// Move the last server to the queue tail.
			mm_list_delete(link);
			mm_list_append(&mm_accept_queue, &srv->accept_queue);

			// Accept a single connection on it.
			mm_net_accept(srv);

			if (++accept_count == MM_ACCEPT_COUNT) {
				// Prevent starvation of other tasks. Give them
				// a chance to run.
				mm_sched_yield();
				accept_count = 0;
			}
		} else {
			uint32_t msg[2];

			// If there are no ready servers yet then block
			// waiting for an accept event.
			mm_port_receive_blocking(mm_net_accept_port, msg, 2);
			mm_net_add_accept_ready(msg[1]);
			accept_count = 0;

			// Drain all pending accept events without blocking.
			while (mm_port_receive(mm_net_accept_port, msg, 2) == 0) {
				mm_net_add_accept_ready(msg[1]);
			}
		}
	}

	LEAVE();
}

static void
mm_net_add_read_write_ready(mm_net_msg_t msg, uint32_t index)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, index);

	switch (msg) {
	case MM_NET_MSG_ERROR:
		mm_net_close(sock);
		break;

	case MM_NET_MSG_READ_READY:
		// Mark the socket as read ready.
		if ((sock->flags & MM_NET_CLOSED) == 0)
			mm_net_set_read_ready(sock);
		break;

	case MM_NET_MSG_WRITE_READY:
		// Mark the socket as write ready.
		if ((sock->flags & MM_NET_CLOSED) == 0)
			mm_net_set_write_ready(sock);
		break;

	case MM_NET_MSG_READ_SPAWN:
		break;

	case MM_NET_MSG_WRITE_SPAWN:
		break;

	case MM_NET_MSG_REGISTER:
		// Let the protocol layer prepare the socket data.
		if (sock->srv->proto->prepare != NULL)
			(sock->srv->proto->prepare)(sock);
		break;

	case MM_NET_MSG_UNREGISTER:
		ASSERT((sock->flags & MM_NET_CLOSED) != 0);

		// Let the protocol layer cleanup the socket data.
		if (sock->srv->proto->cleanup != NULL)
			(sock->srv->proto->cleanup)(sock);

		// Close the socket.
		// TODO: set linger off and/or close concurrently to avoid stalls.
		close(sock->fd);

		// Remove the socket from the server lists.
		mm_net_destroy_socket(sock);
		break;

	default:
		mm_print("%x %x", msg, index);
		ABORT();
	}

	LEAVE();
}

static void
mm_net_io_loop(uintptr_t arg)
{
	ENTER();

	/* Find the pertinent server. */
	struct mm_net_server *srv = (struct mm_net_server *) arg;

	// Handle I/O events.
	int io_count = 0;
	for (;;) {
		bool no_events = true;

		// Handle a read event.
		if (!mm_list_empty(&srv->read_queue)) {

			// Get the pertinent socket.
			struct mm_list *link = mm_list_head(&srv->read_queue);
			struct mm_net_socket *sock = containerof(link, struct mm_net_socket, read_queue);

			// Remove it from the queue.
			mm_list_delete(link);
			sock->flags &= ~MM_NET_READ_QUEUE;

			if ((sock->flags & MM_NET_READ_SPAWN) != 0) {
				// Disable new tasks until this one is done.
				mm_net_bind_reader(sock);

				// Create a new work item that will execute
				// the protocol read routine.
				mm_work_add(MM_TASK_READING, mm_net_reader, (intptr_t) sock);

				++io_count;
				no_events = false;
			}
		}

		// Handle a write event.
		if (!mm_list_empty(&srv->write_queue)) {

			// Get the pertinent socket.
			struct mm_list *link = mm_list_head(&srv->write_queue);
			struct mm_net_socket *sock = containerof(link, struct mm_net_socket, write_queue);

			// Remove it from the queue.
			mm_list_delete(link);
			sock->flags &= ~MM_NET_WRITE_QUEUE;

			if ((sock->flags & MM_NET_WRITE_SPAWN) != 0) {
				// Disable new tasks until this one is done.
				mm_net_bind_writer(sock);

				// Create a new work item that will execute
				// the protocol write routine.
				mm_work_add(MM_TASK_WRITING, mm_net_writer, (intptr_t) sock);

				++io_count;
				no_events = false;
			}
		}

		if (no_events) {
			uint32_t msg[2];

			// If there are no ready sockets then block waiting
			// for an event.
			mm_port_receive_blocking(srv->io_port, msg, 2);
			mm_net_add_read_write_ready(msg[0], msg[1]);
			io_count = 0;

			// Drain all pending read events without blocking.
			while (mm_port_receive(srv->io_port, msg, 2) == 0) {
				mm_net_add_read_write_ready(msg[0], msg[1]);
			}

		} else if (io_count >= MM_IO_COUNT) {
			// Prevent starvation of other tasks. Give them
			// a chance to run.
			mm_sched_yield();
			io_count = 0;
		}
	}

	LEAVE();
}

static void
mm_net_init_tasks(void)
{
	ENTER();

	/* Initialize the accept queue. */
	mm_list_init(&mm_accept_queue);

	/* Create the event handler task. */
	mm_net_accept_task = mm_task_create("net-accept", 0, mm_net_accept_loop, 0);

	/* Make the task priority higher. */
	mm_net_accept_task->priority /= 2;

	/* Create the event handler port. */
	mm_net_accept_port = mm_port_create(mm_net_accept_task);

	// Register I/O handlers.
	mm_net_accept_handler = mm_event_add_io_handler(
		MM_EVENT_NET_READ, mm_net_accept_port);

	LEAVE();
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
	mm_net_init_tasks();

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

void
mm_net_start_server(struct mm_net_server *srv, struct mm_net_proto *proto)
{
	ENTER();
	ASSERT(srv->fd == -1);

	mm_print("start server '%s'", srv->name);

	/* Store the protocol handlers. */
	srv->proto = proto;

	/* Create the server socket. */
	srv->fd = mm_net_open_server_socket(&srv->addr, 0);
	if (mm_event_verify_fd(srv->fd)) {
		mm_fatal(0, "%s: server socket no is too high: %d", srv->name, srv->fd);
	}

	/* Create the event handler task. */
	srv->io_task = mm_task_create("net-io", 0, mm_net_io_loop, (intptr_t) srv);

	/* Make the task priority higher. */
	srv->io_task->priority /= 2;

	/* Create the event handler port. */
	srv->io_port = mm_port_create(srv->io_task);

	/* Allocate an event handler ID for the port. */
	srv->io_handler = mm_event_add_io_handler(MM_EVENT_NET_READ_WRITE, srv->io_port);

	/* Register the server socket with the event loop. */
	mm_event_register_fd(srv->fd, mm_net_accept_handler, (uint32_t) mm_net_server_index(srv));

	LEAVE();
}

void
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->fd != -1);

	mm_print("stop server: %s", srv->name);

	// Unregister the socket.
	mm_event_unregister_fd(srv->fd);

	// Close the socket.
	mm_net_close_server_socket(&srv->addr, srv->fd);
	srv->fd = -1;

	LEAVE();
}

/**********************************************************************
 * Network sockets.
 **********************************************************************/

ssize_t
mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes)
{
	ENTER();
	ssize_t n;

	// Register the current task as reader.
	mm_net_attach_reader(sock);

retry:
	// Check to see if the socket is closed.
	if (unlikely((sock->flags & MM_NET_CLOSED) != 0)) {
		n = -1;
		errno = EBADF;
		goto done;
	}
	// Check to see if the socket is ready for reading.
	if ((sock->flags & MM_NET_READ_READY) == 0) {
		if ((sock->flags & MM_NET_NONBLOCK) == 0) {
			// Block waiting to become read ready.
			mm_sched_block();
			goto retry;
		}

		n = -1;
		errno = EAGAIN;
		goto done;
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
	} else {
		mm_net_close(sock);
	}

done:
	// Unregister the current task as reader.
	mm_net_detach_reader(sock);

	LEAVE();
	return n;
}

ssize_t
mm_net_write(struct mm_net_socket *sock, void *buffer, size_t nbytes)
{
	ENTER();
	ssize_t n;

	// Register the current task as writer.
	mm_net_attach_writer(sock);

retry:
	// Check to see if the socket is closed.
	if (unlikely((sock->flags & MM_NET_CLOSED) != 0)) {
		n = -1;
		errno = EBADF;
		goto done;
	}
	// Check to see if the socket is ready for writing.
	if ((sock->flags & MM_NET_WRITE_READY) == 0) {
		if ((sock->flags & MM_NET_NONBLOCK) == 0) {
			// Block waiting to become write ready.
			mm_sched_block();
			goto retry;
		}
		n = -1;
		errno = EAGAIN;
		goto done;
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
	// Unregister the current task as writer.
	mm_net_detach_writer(sock);

	LEAVE();
	return n;
}

ssize_t
mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt)
{
	ENTER();
	ssize_t n;

	// Register the current task as reader.
	mm_net_attach_reader(sock);

retry:
	// Check to see if the socket is closed.
	if (unlikely((sock->flags & MM_NET_CLOSED) != 0)) {
		n = -1;
		errno = EBADF;
		goto done;
	}
	// Check to see if the socket is ready for reading.
	if ((sock->flags & MM_NET_READ_READY) == 0) {
		if ((sock->flags & MM_NET_NONBLOCK) == 0) {
			// Block waiting to become read ready.
			mm_sched_block();
			goto retry;
		}

		n = -1;
		errno = EAGAIN;
		goto done;
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
	} else {
		mm_net_close(sock);
	}

done:
	// Unregister the current task as reader.
	mm_net_detach_reader(sock);

	LEAVE();
	return n;
}

ssize_t
mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt)
{
	ENTER();
	ssize_t n;

	// Register the current task as writer.
	mm_net_attach_writer(sock);

retry:
	// Check to see if the socket is closed.
	if (unlikely((sock->flags & MM_NET_CLOSED) != 0)) {
		n = -1;
		errno = EBADF;
		goto done;
	}
	// Check to see if the socket is ready for writing.
	if ((sock->flags & MM_NET_WRITE_READY) == 0) {
		if ((sock->flags & MM_NET_NONBLOCK) == 0) {
			// Block waiting to become write ready.
			mm_sched_block();
			goto retry;
		}
		n = -1;
		errno = EAGAIN;
		goto done;
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
	// Unregister the current task as writer.
	mm_net_detach_writer(sock);

	LEAVE();
	return n;
}

void
mm_net_close(struct mm_net_socket *sock)
{
	ENTER();

	if ((sock->flags & MM_NET_CLOSED) != 0)
		goto done;

	// Remove the socket from I/O queues and mark as closed.
	if ((sock->flags & MM_NET_READ_QUEUE) != 0)
		mm_list_delete(&sock->read_queue);
	if ((sock->flags & MM_NET_WRITE_QUEUE) != 0)
		mm_list_delete(&sock->write_queue);
	sock->flags = MM_NET_CLOSED;

	// Remove the socket from the event loop.
	mm_event_unregister_fd(sock->fd);

done:
	LEAVE();
}
