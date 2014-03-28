/*
 * net.c - MainMemory networking.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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
#include "buffer.h"
#include "core.h"
#include "event.h"
#include "exit.h"
#include "log.h"
#include "pool.h"
#include "port.h"
#include "task.h"
#include "timer.h"
#include "trace.h"
#include "util.h"

#include <string.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

static mm_value_t mm_net_prepare(mm_value_t arg);
static mm_value_t mm_net_cleanup(mm_value_t arg);
static mm_value_t mm_net_reader(mm_value_t arg);
static mm_value_t mm_net_writer(mm_value_t arg);
static mm_value_t mm_net_closer(mm_value_t arg);

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
			goto leave;
		}
	} else {
		addr->in_addr.sin_addr = (struct in_addr) { INADDR_ANY };
	}
	addr->in_addr.sin_family = AF_INET;
	addr->in_addr.sin_port = htons(port);
	memset(addr->in_addr.sin_zero, 0, sizeof addr->in_addr.sin_zero);

leave:
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
			goto leave;
		}
	} else {
		addr->in6_addr.sin6_addr = (struct in6_addr) IN6ADDR_ANY_INIT;
	}
	addr->in6_addr.sin6_family = AF_INET6;
	addr->in6_addr.sin6_port = htons(port);
	addr->in6_addr.sin6_flowinfo = 0;
	addr->in6_addr.sin6_scope_id = 0;

leave:
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
	ENTER();

	mm_srv_table_size = 4;
	mm_srv_table = mm_alloc(mm_srv_table_size * sizeof(struct mm_net_server));
	mm_srv_count = 0;

	LEAVE();
}

static void
mm_net_free_server_table(void)
{
	ENTER();

	for (uint32_t i = 0; i < mm_srv_count; i++) {
		mm_free(mm_srv_table[i].name);
	}

	mm_free(mm_srv_table);

	LEAVE();
}

static struct mm_net_server *
mm_net_alloc_server(void)
{
	ENTER();

	if (mm_srv_table_size == mm_srv_count) {
		mm_srv_table_size += 4;
		mm_srv_table = mm_realloc(
			mm_srv_table,
			mm_srv_table_size * sizeof(struct mm_net_server));
	}

	struct mm_net_server *srv = &mm_srv_table[mm_srv_count++];
	srv->fd = -1;
	srv->flags = 0;
	srv->client_core = 0;

	/* Initialize the client list. */
	mm_list_init(&srv->clients);

	LEAVE();
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

	mm_pool_prepare(&mm_socket_pool, "net-socket",
			&mm_alloc_global, sizeof (struct mm_net_socket));

	LEAVE();
}

static void
mm_net_free_socket_table(void)
{
	ENTER();

	mm_pool_cleanup(&mm_socket_pool);

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
	sock->fd_flags = 0;
	sock->task_flags = 0;
	sock->close_flags = 0;
	sock->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	sock->read_stamp = 0;
	sock->write_stamp = 0;
	mm_waitset_prepare(&sock->read_waitset);
	mm_waitset_prepare(&sock->write_waitset);
	sock->read_timeout = MM_TIMEOUT_INFINITE;
	sock->write_timeout = MM_TIMEOUT_INFINITE;
	sock->data = 0;
	sock->core = MM_CORE_NONE;
	sock->reader = NULL;
	sock->writer = NULL;
	sock->server = srv;

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
 * Server connection acceptor.
 **********************************************************************/

/* Accept event handler cookie. */
static mm_event_hid_t mm_net_accept_hid;

static bool
mm_net_accept(struct mm_net_server *srv)
{
	ENTER();

	bool rc = true;

	// Client socket.
	int fd;
	socklen_t salen;
	struct sockaddr_storage sa;

retry:
	// Try to accept a connection.
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

	// Set the socket options.
	int val = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., TCP_NODELAY, ...)");

	// Make the socket non-blocking.
	mm_set_nonblocking(fd);

	// Allocate a new socket structure.
	struct mm_net_socket *sock = mm_net_create_socket(fd, srv);
	if (sock == NULL) {
		mm_error(0, "%s: socket table overflow", srv->name);
		close(fd);
		goto leave;
	}

	// Initialize the socket structure.
	if (sa.ss_family == AF_INET)
		memcpy(&sock->peer.in_addr, &sa, sizeof(sock->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&sock->peer.in6_addr, &sa, sizeof(sock->peer.in6_addr));
	else
		sock->peer.addr.sa_family = sa.ss_family;

	// Select a core for the client using round-robin discipline.
	sock->core = srv->client_core++;
	if (srv->client_core == mm_core_num)
		srv->client_core = 0;
	mm_verbose("bind connection to core %d", sock->core);

	// Request required I/O tasks.
	if ((sock->server->proto->flags & MM_NET_INBOUND) != 0)
		sock->task_flags |= MM_NET_READER_PENDING;
	if ((sock->server->proto->flags & MM_NET_OUTBOUND) != 0)
		sock->task_flags |= MM_NET_WRITER_PENDING;

	// Let the protocol layer prepare the socket data if needed.
	if (sock->server->proto->prepare != NULL) {
		// Delay starting I/O tasks until prepared.
		if ((sock->server->proto->flags & MM_NET_INBOUND) != 0)
			sock->task_flags |= MM_NET_READER_SPAWNED;
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) != 0)
			sock->task_flags |= MM_NET_WRITER_SPAWNED;

		// Request protocol handler routine.
		mm_core_post(sock->core, mm_net_prepare, (mm_value_t) sock);
	}

	// Register the socket with the event loop.
	uint32_t sock_index = mm_pool_ptr2idx(&mm_socket_pool, sock);
	bool input_oneshot = !(sock->server->proto->flags & MM_NET_INBOUND);
	bool output_oneshot = !(sock->server->proto->flags & MM_NET_OUTBOUND);
	mm_event_register_fd(sock->fd,
			     sock_index,
			     srv->input_handler, input_oneshot,
			     srv->output_handler, output_oneshot,
			     srv->control_handler);

leave:
	LEAVE();
	return rc;
}

static mm_value_t
mm_net_acceptor(mm_value_t arg)
{
	ENTER();

	// Find the pertinent server.
	struct mm_net_server *srv = &mm_srv_table[arg];

	// Accept incoming connections.
	while (mm_net_accept(srv)) {
		mm_task_yield();
	}

	LEAVE();
	return 0;
}

static void
mm_net_accept_handler(mm_event_t event __attribute__((unused)), uint32_t data)
{
	ENTER();

	mm_core_post(true, mm_net_acceptor, data);

	LEAVE();
}

static void
mm_net_init_acceptor(void)
{
	ENTER();

	// Register I/O handlers.
	mm_net_accept_hid = mm_event_register_handler(mm_net_accept_handler);

	LEAVE();
}

/**********************************************************************
 * Socket I/O state.
 **********************************************************************/

static void
mm_net_next_read_stamp(struct mm_net_socket *sock)
{
	uint32_t stamp = sock->read_stamp + 1;
	mm_memory_store(sock->read_stamp, stamp);
}

static void
mm_net_next_write_stamp(struct mm_net_socket *sock)
{
	uint32_t stamp = sock->write_stamp + 1;
	mm_memory_store(sock->write_stamp, stamp);
}

static void
mm_net_set_read_ready(struct mm_net_socket *sock)
{
	ENTER();

	mm_task_lock(&sock->lock);
	mm_net_next_read_stamp(sock);
	sock->fd_flags |= MM_NET_READ_READY;
	mm_waitset_broadcast(&sock->read_waitset, &sock->lock);

	LEAVE();
}

static void
mm_net_set_write_ready(struct mm_net_socket *sock)
{
	ENTER();

	mm_task_lock(&sock->lock);
	mm_net_next_write_stamp(sock);
	sock->fd_flags |= MM_NET_WRITE_READY;
	mm_waitset_broadcast(&sock->write_waitset, &sock->lock);

	LEAVE();
}

static void
mm_net_set_read_error(struct mm_net_socket *sock)
{
	ENTER();

	mm_task_lock(&sock->lock);
	mm_net_next_read_stamp(sock);
	sock->fd_flags |= MM_NET_READ_ERROR;
	mm_waitset_broadcast(&sock->read_waitset, &sock->lock);

	LEAVE();
}

static void
mm_net_set_write_error(struct mm_net_socket *sock)
{
	ENTER();

	mm_task_lock(&sock->lock);
	mm_net_next_write_stamp(sock);
	sock->fd_flags |= MM_NET_WRITE_ERROR;
	mm_waitset_broadcast(&sock->write_waitset, &sock->lock);

	LEAVE();
}

static void
mm_net_reset_read_ready(struct mm_net_socket *sock, uint32_t stamp)
{
	ENTER();

	mm_task_lock(&sock->lock);
	if (sock->read_stamp != stamp) {
		mm_task_unlock(&sock->lock);
	} else {
		sock->fd_flags &= ~MM_NET_READ_READY;
		mm_task_unlock(&sock->lock);
#if MM_ONESHOT_HANDLERS
		bool oneshot = !(sock->server->proto->flags & MM_NET_INBOUND);
		if (oneshot) {
			mm_event_trigger_input(sock->fd, sock->server->input_handler);
		}
#endif
	}

	LEAVE();
}

static void
mm_net_reset_write_ready(struct mm_net_socket *sock, uint32_t stamp)
{
	ENTER();

	mm_task_lock(&sock->lock);
	if (sock->write_stamp != stamp) {
		mm_task_unlock(&sock->lock);
	} else {
		sock->fd_flags &= ~MM_NET_WRITE_READY;
		mm_task_unlock(&sock->lock);
#if MM_ONESHOT_HANDLERS
		bool oneshot = !(sock->server->proto->flags & MM_NET_OUTBOUND);
		if (oneshot) {
			mm_event_trigger_output(sock->fd, sock->server->output_handler);
		}
#endif
	}

	LEAVE();
}

/**********************************************************************
 * Socket control loop.
 **********************************************************************/

/* Socket control codes. */
typedef enum {
	MM_NET_CHECK_READER,
	MM_NET_CHECK_WRITER,
	MM_NET_SPAWN_READER,
	MM_NET_SPAWN_WRITER,
	MM_NET_YIELD_READER,
	MM_NET_YIELD_WRITER,
	MM_NET_CLEANUP_SOCK,
	MM_NET_DESTROY_SOCK,
} mm_net_msg_t;

static void
mm_net_handle_check_reader(struct mm_net_socket *sock)
{
	ENTER();

	// Check if a reader should be spawned as soon as the socket becomes
	// read ready (otherwise a mm_net_spawn_reader() call is needed).
	uint8_t task_flags = sock->task_flags;
	task_flags &= MM_NET_READER_SPAWNED | MM_NET_READER_PENDING;
	if (task_flags == MM_NET_READER_PENDING) {
		// Submit a writer work.
		sock->task_flags |= MM_NET_READER_SPAWNED;
		if ((sock->server->proto->flags & MM_NET_INBOUND) == 0)
			sock->task_flags &= ~MM_NET_READER_PENDING;
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);
	}

	LEAVE();
}

static void
mm_net_handle_check_writer(struct mm_net_socket *sock)
{
	ENTER();

	// Check if a writer should be spawned as soon as the socket becomes
	// write ready (otherwise a mm_net_spawn_writer() call is needed).
	uint8_t task_flags = sock->task_flags;
	task_flags &= MM_NET_WRITER_SPAWNED | MM_NET_WRITER_PENDING;
	if (task_flags == MM_NET_WRITER_PENDING) {
		// Submit a writer work.
		sock->task_flags |= MM_NET_WRITER_SPAWNED;
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) == 0)
			sock->task_flags &= ~MM_NET_WRITER_PENDING;
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);
	}

	LEAVE();
}

static void
mm_net_handle_spawn_reader(struct mm_net_socket *sock)
{
	ENTER();

	// If a reader is already active defer another reader start.
	if ((sock->task_flags & MM_NET_READER_SPAWNED) != 0) {
		sock->task_flags |= MM_NET_READER_PENDING;
	} else {
		// Submit a reader work.
		sock->task_flags |= MM_NET_READER_SPAWNED;
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);
	}

	LEAVE();
}

static void
mm_net_handle_spawn_writer(struct mm_net_socket *sock)
{
	ENTER();

	// If a writer is already active defer another writer start.
	if ((sock->task_flags & MM_NET_WRITER_SPAWNED) != 0) {
		sock->task_flags |= MM_NET_WRITER_PENDING;
	} else {
		// Submit a writer work.
		sock->task_flags |= MM_NET_WRITER_SPAWNED;
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);
	}

	LEAVE();
}

static void
mm_net_handle_yield_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT((sock->task_flags & MM_NET_READER_SPAWNED) != 0);

	// Supposedly there is no active reader at this time so the read
	// readiness flags cannot change concurrently.
	uint8_t fd_flags = mm_memory_load(sock->fd_flags);
	fd_flags &= MM_NET_READ_READY | MM_NET_READ_ERROR;

	// Check if a reader should be spawned as soon as the socket becomes
	// read-ready (otherwise a mm_net_spawn_reader() call is needed).
	uint8_t task_flags = sock->task_flags;
	task_flags &= MM_NET_READER_PENDING;

	if (task_flags != 0 && fd_flags != 0) {
		// Submit a reader work.
		if ((sock->server->proto->flags & MM_NET_INBOUND) == 0)
			sock->task_flags &= ~MM_NET_READER_PENDING;
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);
	} else {
		sock->task_flags &= ~MM_NET_READER_SPAWNED;
		if ((fd_flags & MM_NET_READ_ERROR) != 0) {
			mm_core_post(sock->core, mm_net_closer, (mm_value_t) sock);
		}
	}

	LEAVE();
}

static void
mm_net_handle_yield_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT((sock->task_flags & MM_NET_WRITER_SPAWNED) != 0);

	// Supposedly there is no active writer at this time so the write
	// readiness flags cannot change concurrently.
	uint8_t fd_flags = mm_memory_load(sock->fd_flags);
	fd_flags &= MM_NET_WRITE_READY | MM_NET_WRITE_ERROR;

	// Check if a writer should be spawned as soon as the socket becomes
	// write-ready (otherwise a mm_net_spawn_writer() call is needed).
	uint8_t task_flags = sock->task_flags;
	task_flags &= MM_NET_WRITER_PENDING;

	if (task_flags != 0 && fd_flags != 0) {
		// Submit a writer work.
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) == 0)
			sock->task_flags &= ~MM_NET_WRITER_PENDING;
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);
	} else {
		sock->task_flags &= ~MM_NET_WRITER_SPAWNED;
		if ((sock->fd_flags & MM_NET_WRITE_ERROR) != 0) {
			mm_core_post(sock->core, mm_net_closer, (mm_value_t) sock);
		}
	}

	LEAVE();
}

static void
mm_net_handle_cleanup_sock(struct mm_net_socket *sock)
{
	ENTER();

	// At this time there are no and will not be any I/O control messages
	// related to this socket in the processing pipeline. But there still
	// may be active reader/writer tasks or pending work items for this
	// socket. So relying on the FIFO order of the work queue submit a
	// work item that will cleanup the socket being the last one that
	// refers to it.
	mm_core_post(sock->core, mm_net_cleanup, (mm_value_t) sock);

	LEAVE();
}

static void
mm_net_handle_destroy_sock(struct mm_net_socket *sock)
{
	ENTER();

	// At this time there are no and will not be any reader/writer tasks
	// bound to this socket.

	// Close the socket.
	// TODO: set linger off and/or close concurrently to avoid stalls.
	close(sock->fd);
	sock->fd = -1;

	// Remove the socket from the server lists.
	mm_net_destroy_socket(sock);

	LEAVE();
}

static mm_value_t
mm_net_sock_ctl_loop(mm_value_t arg)
{
	ENTER();

	// Find the pertinent server.
	struct mm_net_server *srv = (struct mm_net_server *) arg;

	// Handle I/O events.
	for (;;) {
		uint32_t msg[2];
		mm_port_receive_blocking(srv->io_port, msg, 2);

		// Find the pertinent socket.
		struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, msg[1]);

		// Handle the event.
		switch (msg[0]) {
		case MM_NET_CHECK_READER:
			mm_net_handle_check_reader(sock);
			break;

		case MM_NET_CHECK_WRITER:
			mm_net_handle_check_writer(sock);
			break;

		case MM_NET_SPAWN_READER:
			mm_net_handle_spawn_reader(sock);
			break;

		case MM_NET_SPAWN_WRITER:
			mm_net_handle_spawn_writer(sock);
			break;

		case MM_NET_YIELD_READER:
			mm_net_handle_yield_reader(sock);
			break;

		case MM_NET_YIELD_WRITER:
			mm_net_handle_yield_writer(sock);
			break;

		case MM_NET_CLEANUP_SOCK:
			mm_net_handle_cleanup_sock(sock);
			break;

		case MM_NET_DESTROY_SOCK:
			mm_net_handle_destroy_sock(sock);
			break;

		default:
			mm_brief("%x %x", msg[0], msg[1]);
			ABORT();
		}
	}

	LEAVE();
	return 0;
}

static void
mm_net_sock_ctl_low(struct mm_net_socket *sock, uint32_t idx, mm_net_msg_t msg)
{
	uint32_t buf[2] = { msg, idx };
	mm_port_send_blocking(sock->server->io_port, buf, 2);
}

static void
mm_net_sock_ctl(struct mm_net_socket *sock, mm_net_msg_t msg)
{
	uint32_t idx = mm_pool_ptr2idx(&mm_socket_pool, sock);
	mm_net_sock_ctl_low(sock, idx, msg);
}

/**********************************************************************
 * Socket I/O event handlers.
 **********************************************************************/

static void
mm_net_input_handler(mm_event_t event __attribute__((unused)), uint32_t data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, data);
	// Mark the socket as read ready.
	mm_net_set_read_ready(sock);
	// Spawn a reader task if needed.
	mm_net_sock_ctl_low(sock, data, MM_NET_CHECK_READER);

	LEAVE();
}

static void
mm_net_output_handler(mm_event_t event __attribute__((unused)), uint32_t data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, data);
	// Mark the socket as write ready.
	mm_net_set_write_ready(sock);
	// Spawn a writer task if needed.
	mm_net_sock_ctl_low(sock, data, MM_NET_CHECK_WRITER);

	LEAVE();
}

static void
mm_net_control_handler(mm_event_t event, uint32_t data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = mm_pool_idx2ptr(&mm_socket_pool, data);

	switch (event) {
	case MM_EVENT_REGISTER:
		break;

	case MM_EVENT_UNREGISTER:
		// Finish with the socket use. There still may be some
		// unprocessed I/O control messages in the pipeline, so
		// we should pipeline this one too.
		mm_net_sock_ctl_low(sock, data, MM_NET_CLEANUP_SOCK);
		break;

	case MM_EVENT_INPUT_ERROR:
		// Mark the socket as having a read error.
		mm_net_set_read_error(sock);
		// Spawn a reader task if needed.
		mm_net_sock_ctl_low(sock, data, MM_NET_CHECK_READER);
		break;

	case MM_EVENT_OUTPUT_ERROR:
		// Mark the socket as having a write error.
		mm_net_set_write_error(sock);
		// Spawn a writer task if needed.
		mm_net_sock_ctl_low(sock, data, MM_NET_CHECK_WRITER);
		break;

	default:
		mm_brief("%x", event);
		ABORT();
	}

	LEAVE();
}

/**********************************************************************
 * Socket I/O tasks.
 **********************************************************************/

void
mm_net_spawn_reader(struct mm_net_socket *sock)
{
	ENTER();

	if (!mm_net_is_reader_shutdown(sock)) {
		mm_net_sock_ctl(sock, MM_NET_SPAWN_READER);

#if 0
		// Let it start immediately.
		mm_task_yield();
#endif
	}

	LEAVE();
}

void
mm_net_spawn_writer(struct mm_net_socket *sock)
{
	ENTER();

	if (!mm_net_is_writer_shutdown(sock)) {
		mm_net_sock_ctl(sock, MM_NET_SPAWN_WRITER);

#if 0
		// Let it start immediately.
		mm_task_yield();
#endif
	}

	LEAVE();
}

void
mm_net_yield_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core);

	// Unbind the current task from the socket, enable spawning a new
	// reader task if needed.
	if ((mm_running_task->flags & MM_TASK_READING) != 0) {
		ASSERT(sock->reader == mm_running_task);
		mm_running_task->flags &= ~MM_TASK_READING;
		sock->reader = NULL;

		if (!mm_net_is_reader_shutdown(sock))
			mm_net_sock_ctl(sock, MM_NET_YIELD_READER);
	}

	LEAVE();
}

void
mm_net_yield_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core);

	// Unbind the current task from the socket, enable spawning a new
	// writer task if needed.
	if ((mm_running_task->flags & MM_TASK_WRITING) != 0) {
		ASSERT(sock->writer == mm_running_task);
		mm_running_task->flags &= ~MM_TASK_WRITING;
		sock->writer = NULL;

		if (!mm_net_is_writer_shutdown(sock))
			mm_net_sock_ctl(sock, MM_NET_YIELD_WRITER);
	}

	LEAVE();
}

static mm_value_t
mm_net_prepare(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(!mm_net_is_closed(sock));
	ASSERT(sock->core == mm_core);

	// Run the protocol handler routine.
	(sock->server->proto->prepare)(sock);

	// Let start I/O tasks.
	if (!mm_net_is_closed(sock)) {
		if ((sock->server->proto->flags & MM_NET_INBOUND) != 0)
			mm_net_sock_ctl(sock, MM_NET_YIELD_READER);
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) != 0)
			mm_net_sock_ctl(sock, MM_NET_YIELD_WRITER);
	}

	LEAVE();
	return 0;
}

static mm_value_t
mm_net_cleanup(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core);

	// Notify a reader/writer about closing.
	// TODO: don't block here, have a queue of closed socks
	while (sock->reader != NULL || sock->writer != NULL) {
		mm_priority_t priority = MM_PRIO_UPPER(mm_running_task->priority, 1);
		if (sock->reader != NULL)
			mm_task_hoist(sock->reader, priority);
 		if (sock->writer != NULL)
			mm_task_hoist(sock->writer, priority);
		mm_task_yield();
	}

	// Run the protocol handler routine.
	if (sock->server->proto->cleanup != NULL)
		(sock->server->proto->cleanup)(sock);

	mm_net_sock_ctl(sock, MM_NET_DESTROY_SOCK);

	LEAVE();
	return 0;
}

static mm_value_t
mm_net_reader(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core);
	if (unlikely(mm_net_is_reader_shutdown(sock)))
		goto leave;

	// Register the reader task.
	mm_running_task->flags |= MM_TASK_READING;
	sock->reader = mm_running_task;

	// Ensure the task yields socket on exit.
	mm_task_cleanup_push(mm_net_yield_reader, sock);

	// Run the protocol handler routine.
	(sock->server->proto->reader)(sock);

	// Yield the socket on return.
	mm_task_cleanup_pop(true);

leave:
	LEAVE();
	return 0;
}

static mm_value_t
mm_net_writer(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core);

	if (unlikely(mm_net_is_writer_shutdown(sock)))
		goto leave;

	// Register the writer task.
	mm_running_task->flags |= MM_TASK_WRITING;
	sock->writer = mm_running_task;

	// Ensure the task yields socket on exit.
	mm_task_cleanup_push(mm_net_yield_writer, sock);

	// Run the protocol handler routine.
	(sock->server->proto->writer)(sock);

	// Yield the socket on return.
	mm_task_cleanup_pop(true);

leave:
	LEAVE();
	return 0;
}

static mm_value_t
mm_net_closer(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core);

	// Close the socket.
	mm_net_close(sock);

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
		goto leave;

	for (uint32_t i = 0; i < mm_srv_count; i++) {
		struct mm_net_server *srv = &mm_srv_table[i];
		if (srv->fd >= 0) {
			mm_net_remove_unix_socket(&srv->addr);
		}
	}

leave:
	LEAVE();
}

void
mm_net_init(void)
{
	ENTER();

	mm_atexit(mm_net_exit_cleanup);

	mm_net_init_server_table();
	mm_net_init_socket_table();
	mm_net_init_acceptor();

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
mm_net_create_unix_server(const char *name,
			  struct mm_net_proto *proto,
			  const char *path)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	srv->name = mm_asprintf("%s (%s)", name, path);
	srv->proto = proto;

	if (mm_net_set_un_addr(&srv->addr, path) < 0)
		mm_fatal(0, "failed to create '%s' server with path '%s'",
		name, path);

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet_server(const char *name,
			  struct mm_net_proto *proto,
			  const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	srv->name = mm_asprintf("%s (%s:%d)", name, addrstr, port);
	srv->proto = proto;

	if (mm_net_set_in_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'",
			 name, addrstr, port);

	LEAVE();
	return srv;
}

struct mm_net_server *
mm_net_create_inet6_server(const char *name,
			   struct mm_net_proto *proto,
			   const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server();
	srv->name = mm_asprintf("%s (%s:%d)", name, addrstr, port);
	srv->proto = proto;

	if (mm_net_set_in6_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'",
			 name, addrstr, port);

	LEAVE();
	return srv;
}

void
mm_net_start_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->fd == -1);

	mm_brief("start server '%s'", srv->name);

	// Create the server socket.
	srv->fd = mm_net_open_server_socket(&srv->addr, 0);

	// Create the event handler task.
	struct mm_task_attr attr;
	mm_task_attr_init(&attr);
	mm_task_attr_setpriority(&attr, MM_PRIO_SYSTEM);
	mm_task_attr_setname(&attr, "net-io");
	srv->io_task = mm_task_create(&attr, mm_net_sock_ctl_loop, (mm_value_t) srv);

	// Create the event handler port.
	srv->io_port = mm_port_create(srv->io_task);

	// Allocate an event handler IDs.
	srv->input_handler = mm_event_register_handler(mm_net_input_handler);
	srv->output_handler = mm_event_register_handler(mm_net_output_handler);
	srv->control_handler = mm_event_register_handler(mm_net_control_handler);

	// Register the server socket with the event loop.
	mm_event_register_fd(srv->fd,
			     (uint32_t) mm_net_server_index(srv),
			     mm_net_accept_hid, false, 0, false, 0);

        LEAVE();
}

void
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
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

/**********************************************************************
 * Network sockets.
 **********************************************************************/

#define MM_NET_MAXIOV	64

static int
mm_net_wait_readable(struct mm_net_socket *sock, mm_timeval_t deadline)
{
	ENTER();
	int rc = 1;

	// Check to see if the socket is closed.
	if (mm_net_is_reader_shutdown(sock)) {
		errno = EBADF;
		rc = -1;
		goto leave;
	}

	// Check to see if the socket is read ready.
	uint8_t flags = mm_memory_load(sock->fd_flags);
	flags &= (MM_NET_READ_READY | MM_NET_READ_ERROR);
	if (flags != 0) {
		goto leave;
	}

	// Ensure atomic access to I/O state.
	mm_task_lock(&sock->lock);

	// Check to see if the socket is read ready again.
	flags = sock->fd_flags & (MM_NET_READ_READY | MM_NET_READ_ERROR);
	if (flags != 0) {
		mm_task_unlock(&sock->lock);
		goto leave;
	}

	// Block the task waiting for the socket to become read ready.
	if (sock->read_timeout == MM_TIMEOUT_INFINITE) {
		mm_waitset_wait(&sock->read_waitset, &sock->lock);
		rc = 0;
	} else if (mm_core->time_value < deadline) {
		mm_timeout_t timeout = deadline - mm_core->time_value;
		mm_waitset_timedwait(&sock->read_waitset, &sock->lock, timeout);
		rc = 0;
	} else {
		mm_task_unlock(&sock->lock);
		if (sock->read_timeout != 0)
			errno = ETIMEDOUT;
		else
			errno = EAGAIN;
		rc = -1;
		goto leave;
	}

	// Check if the task is canceled.
	mm_task_testcancel();

leave:
	LEAVE();
	return rc;
}

static int
mm_net_wait_writable(struct mm_net_socket *sock, mm_timeval_t deadline)
{
	ENTER();
	int rc = 1;

	// Check to see if the socket is closed.
	if (mm_net_is_writer_shutdown(sock)) {
		errno = EBADF;
		rc = -1;
		goto leave;
	}

	// Check to see if the socket is write ready.
	uint8_t flags = mm_memory_load(sock->fd_flags);
	flags &= (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	if (flags != 0) {
		goto leave;
	}

	// Ensure atomic access to I/O state.
	mm_task_lock(&sock->lock);

	// Check to see if the socket is write ready again.
	flags = sock->fd_flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	if (flags != 0) {
		mm_task_unlock(&sock->lock);
		goto leave;
	}

	// Block the task waiting for the socket to become write ready.
	if (sock->write_timeout == MM_TIMEOUT_INFINITE) {
		mm_waitset_wait(&sock->write_waitset, &sock->lock);
		rc = 0;
	} else  if (mm_core->time_value < deadline) {
		mm_timeout_t timeout = deadline - mm_core->time_value;
		mm_waitset_timedwait(&sock->write_waitset, &sock->lock, timeout);
		rc = 0;
	} else {
		mm_task_unlock(&sock->lock);
		if (sock->write_timeout != 0)
			errno = ETIMEDOUT;
		else
			errno = EAGAIN;
		rc = -1;
		goto leave;
	}

	// Check if the task is canceled.
	mm_task_testcancel();

leave:
	LEAVE();
	return rc;
}

ssize_t
mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_value + sock->read_timeout;

retry:
	// Check to see if the socket is ready for reading.
	n = mm_net_wait_readable(sock, deadline);
	if (n <= 0) {
		if (n < 0) {
			goto leave;
		} else {
			goto retry;
		}
	}

	// Save readiness stamp to detect a concurrent readiness update.
	uint32_t stamp = mm_memory_load(sock->read_stamp);

	// Try to read (nonblocking).
	n = read(sock->fd, buffer, nbytes);
	if (n > 0) {
		if ((size_t) n < nbytes) {
			mm_net_reset_read_ready(sock, stamp);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_read_ready(sock, stamp);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "read()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_write(struct mm_net_socket *sock, const void *buffer, size_t nbytes)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_value + sock->write_timeout;

retry:
	// Check to see if the socket is ready for writing.
	n = mm_net_wait_writable(sock, deadline);
	if (n <= 0) {
		if (n < 0) {
			goto leave;
		} else {
			goto retry;
		}
	}

	// Save readiness stamp to detect a concurrent readiness update.
	uint32_t stamp = mm_memory_load(sock->write_stamp);

	// Try to write (nonblocking).
	n = write(sock->fd, buffer, nbytes);
	if (n > 0) {
		if ((size_t) n < nbytes) {
			mm_net_reset_write_ready(sock, stamp);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_write_ready(sock, stamp);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "write()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_readv(struct mm_net_socket *sock,
	     const struct iovec *iov, int iovcnt,
	     ssize_t nbytes)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_value + sock->read_timeout;

retry:
	// Check to see if the socket is ready for reading.
	n = mm_net_wait_readable(sock, deadline);
	if (n <= 0) {
		if (n < 0) {
			goto leave;
		} else {
			goto retry;
		}
	}

	// Save readiness stamp to detect a concurrent readiness update.
	uint32_t stamp = mm_memory_load(sock->read_stamp);

	// Try to read (nonblocking).
	n = readv(sock->fd, iov, iovcnt);
	if (n > 0) {
		if (n < nbytes) {
			mm_net_reset_read_ready(sock, stamp);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_read_ready(sock, stamp);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "readv()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_writev(struct mm_net_socket *sock,
	      const struct iovec *iov, int iovcnt,
	      ssize_t nbytes)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_value + sock->write_timeout;

retry:
	// Check to see if the socket is ready for writing.
	n = mm_net_wait_writable(sock, deadline);
	if (n <= 0) {
		if (n < 0) {
			goto leave;
		} else {
			goto retry;
		}
	}

	// Save readiness stamp to detect a concurrent readiness update.
	uint32_t stamp = mm_memory_load(sock->write_stamp);

	// Try to write (nonblocking).
	n = writev(sock->fd, iov, iovcnt);
	if (n > 0) {
		if (n < nbytes) {
			mm_net_reset_write_ready(sock, stamp);
		}
	} else if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			mm_net_reset_write_ready(sock, stamp);
			goto retry;
		} else {
			int saved_errno = errno;
			if (errno != EINVAL && errno != EFAULT)
				mm_net_close(sock);
			mm_error(saved_errno, "writev()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_readbuf(struct mm_net_socket *sock, struct mm_buffer *buf)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n = 0;

	int iovcnt = 0;
	struct iovec iov[MM_NET_MAXIOV];

	struct mm_buffer_cursor cur;
	bool rc = mm_buffer_first_in(buf, &cur);
	while (rc) {
		size_t len = cur.end - cur.ptr;
		if (likely(len)) {
			n += len;

			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = cur.ptr;
			++iovcnt;

			if (unlikely(iovcnt == MM_NET_MAXIOV))
				break;
		}
		rc = mm_buffer_next_in(buf, &cur);
	}

	if (unlikely(n <= 0)) {
		n = -1;
		errno = EINVAL;
		goto leave;
	}

	if (iovcnt == 1) {
		n = mm_net_read(sock, iov[0].iov_base, iov[0].iov_len);
	} else {
		n = mm_net_readv(sock, iov, iovcnt, n);
	}
	if (n > 0) {
		mm_buffer_expand(buf, n);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t
mm_net_writebuf(struct mm_net_socket *sock, struct mm_buffer *buf)
{
	ENTER();
	ASSERT(sock->core == mm_core);
	ssize_t n = 0;

	int iovcnt = 0;
	struct iovec iov[MM_NET_MAXIOV];

	struct mm_buffer_cursor cur;
	bool rc = mm_buffer_first_out(buf, &cur);
	while (rc) {
		size_t len = cur.end - cur.ptr;
		if (likely(len)) {
			n += len;

			iov[iovcnt].iov_len = len;
			iov[iovcnt].iov_base = cur.ptr;
			++iovcnt;

			if (unlikely(iovcnt == MM_NET_MAXIOV))
				break;
		}
		rc = mm_buffer_next_out(buf, &cur);
	}

	if (unlikely(n <= 0)) {
		n = -1;
		errno = EINVAL;
		goto leave;
	}

	if (iovcnt == 1) {
		n = mm_net_write(sock, iov[0].iov_base, iov[0].iov_len);
	} else {
		n = mm_net_writev(sock, iov, iovcnt, n);
	}
	if (n > 0) {
		mm_buffer_reduce(buf, n);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

void
mm_net_close(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core);

	if (mm_net_is_closed(sock))
		goto leave;

	// Mark the socket as closed.
	sock->close_flags = MM_NET_CLOSED;

	// Remove the socket from the event loop.
	mm_event_unregister_fd(sock->fd);

leave:
	LEAVE();
}

void
mm_net_shutdown_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core);

	if (mm_net_is_reader_shutdown(sock))
		goto leave;

	// Mark the socket as having the reader part closed.
	sock->close_flags |= MM_NET_READER_SHUTDOWN;

	// TODO: async this
	if (shutdown(sock->fd, SHUT_RD) < 0)
		mm_error(errno, "shutdown");

leave:
	LEAVE();
}

void
mm_net_shutdown_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core);

	if (mm_net_is_writer_shutdown(sock))
		goto leave;

	// Mark the socket as having the writer part closed.
	sock->close_flags |= MM_NET_WRITER_SHUTDOWN;

	// TODO: async this
	if (shutdown(sock->fd, SHUT_WR) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}
