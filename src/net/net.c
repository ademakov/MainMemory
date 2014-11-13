/*
 * net/net.c - MainMemory networking.
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

#include "net/net.h"

#include "core/core.h"
#include "core/pool.h"
#include "core/task.h"
#include "core/timer.h"

#include "event/event.h"
#include "event/nonblock.h"

#include "base/log/error.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/alloc.h"
#include "base/util/exit.h"
#include "base/util/format.h"

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

static void
mm_net_init_server_table(void)
{
	ENTER();

	mm_srv_table_size = 4;
	mm_srv_table = mm_global_alloc(mm_srv_table_size * sizeof(struct mm_net_server));
	mm_srv_count = 0;

	LEAVE();
}

static void
mm_net_free_server_table(void)
{
	ENTER();

	for (uint32_t i = 0; i < mm_srv_count; i++) {
		struct mm_net_server *srv = &mm_srv_table[i];
		mm_bitset_cleanup(&srv->affinity, &mm_global_arena);
		mm_global_free(srv->name);
	}

	mm_global_free(mm_srv_table);

	LEAVE();
}

static struct mm_net_server *
mm_net_alloc_server(void)
{
	ENTER();

	if (mm_srv_table_size == mm_srv_count) {
		mm_srv_table_size += 4;
		mm_srv_table = mm_global_realloc(
			mm_srv_table,
			mm_srv_table_size * sizeof(struct mm_net_server));
	}

	struct mm_net_server *srv = &mm_srv_table[mm_srv_count++];
	srv->event.fd = -1;
	srv->flags = 0;
	srv->client_count = 0;

	srv->core = MM_CORE_NONE;
	srv->core_num = 0;
	srv->per_core = NULL;

	mm_bitset_prepare(&srv->affinity, &mm_global_arena, mm_core_getnum());

	LEAVE();
	return srv;
}

/**********************************************************************
 * Socket initialization and cleanup.
 **********************************************************************/

static void
mm_net_prepare_socket(struct mm_net_socket *sock, int fd, struct mm_net_server *srv)
{
	// Prepare the event data.
	bool input_oneshot = !(srv->proto->flags & MM_NET_INBOUND);
	bool output_oneshot = !(srv->proto->flags & MM_NET_OUTBOUND);
	mm_event_prepare_fd(&sock->event, fd,
			     srv->input_handler, input_oneshot,
			     srv->output_handler, output_oneshot,
			     srv->control_handler);

	sock->flags = 0;
	sock->close_flags = 0;
#if !MM_NET_LOCAL_EVENTS
	sock->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
#endif
	sock->read_stamp = 0;
	sock->write_stamp = 0;
	mm_waitset_prepare(&sock->read_waitset);
	mm_waitset_prepare(&sock->write_waitset);
	sock->read_timeout = MM_TIMEOUT_INFINITE;
	sock->write_timeout = MM_TIMEOUT_INFINITE;
	sock->core = MM_CORE_NONE;
	sock->core_server_index = MM_CORE_NONE;
	sock->reader = NULL;
	sock->writer = NULL;
	sock->server = srv;
}

static void
mm_net_cleanup_socket(struct mm_net_socket *sock)
{
	mm_waitset_cleanup(&sock->read_waitset);
	mm_waitset_cleanup(&sock->write_waitset);
	mm_list_delete(&sock->clients);
}

/**********************************************************************
 * Socket pool.
 **********************************************************************/

static struct mm_pool mm_socket_pool;

static void
mm_net_init_socket_table(void)
{
	ENTER();

	mm_pool_prepare_shared(&mm_socket_pool, "net-socket", sizeof(struct mm_net_socket));

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
mm_net_create_socket(struct mm_net_server *srv, int fd)
{
	ENTER();

	// Allocate the socket.
	struct mm_net_socket *sock;
	if (srv->proto->alloc != NULL)
		sock = (srv->proto->alloc)();
	else
		sock = mm_pool_alloc(&mm_socket_pool);

	// Initialize the fields.
	if (likely(sock != NULL))
		mm_net_prepare_socket(sock, fd, srv);

	LEAVE();
	return sock;
}

static void
mm_net_destroy_socket(struct mm_net_socket *sock)
{
	ENTER();

	mm_net_cleanup_socket(sock);

	if (sock->server->proto->free != NULL)
		(sock->server->proto->free)(sock);
	else
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
	fd = accept(srv->event.fd, (struct sockaddr *) &sa, &salen);
	if (unlikely(fd < 0)) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			mm_error(errno, "%s: accept()", srv->name);
		else
			rc = false;
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
	struct mm_net_socket *sock = mm_net_create_socket(srv, fd);
	if (sock == NULL) {
		mm_error(0, "%s: failed to allocate socket data", srv->name);
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
	mm_core_t index = (srv->client_count++ / 4) % srv->core_num;

	// Register with the server.
	sock->core_server_index = index;
	sock->core = srv->per_core[index].core;
	mm_waitset_pin(&sock->read_waitset, sock->core);
	mm_waitset_pin(&sock->write_waitset, sock->core);
	mm_list_append(&srv->per_core[index].clients, &sock->clients);
	mm_verbose("bind socket %d to core %d", fd, sock->core);

	// Request required I/O tasks.
	if ((srv->proto->flags & MM_NET_INBOUND) != 0)
		sock->flags |= MM_NET_READER_PENDING;
	if ((srv->proto->flags & MM_NET_OUTBOUND) != 0)
		sock->flags |= MM_NET_WRITER_PENDING;

	// Request protocol handler routine.
	mm_core_post(sock->core, mm_net_prepare, (mm_value_t) sock);

leave:
	LEAVE();
	return rc;
}

static mm_value_t
mm_net_acceptor(mm_value_t arg)
{
	ENTER();

	// Find the pertinent server.
	struct mm_net_server *srv = (struct mm_net_server *) arg;

	// Accept incoming connections.
	while (mm_net_accept(srv)) {
		mm_task_yield();
	}

	LEAVE();
	return 0;
}

static void
mm_net_accept_handler(mm_event_t event __attribute__((unused)), void *data)
{
	ENTER();

	struct mm_net_server *srv
		= containerof(data, struct mm_net_server, event);
	mm_core_post(MM_CORE_SELF, mm_net_acceptor, (mm_value_t) srv);

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

static inline void
mm_net_socket_lock(struct mm_net_socket *sock)
{
#if !MM_NET_LOCAL_EVENTS
	mm_task_lock(&sock->lock);
#else
	(void) sock;
#endif
}

static inline void
mm_net_socket_unlock(struct mm_net_socket *sock)
{
#if !MM_NET_LOCAL_EVENTS
	mm_task_unlock(&sock->lock);
#else
	(void) sock;
#endif
}

static inline void
mm_net_socket_notify(struct mm_net_socket *sock, struct mm_waitset *waitset)
{
#if !MM_NET_LOCAL_EVENTS
	mm_waitset_broadcast(waitset, &sock->lock);
#else
	(void) sock;
	mm_waitset_local_broadcast(waitset);
#endif
}

static inline void
mm_net_socket_wait(struct mm_net_socket *sock, struct mm_waitset *waitset)
{
#if !MM_NET_LOCAL_EVENTS
	mm_waitset_wait(waitset, &sock->lock);
#else
	(void) sock;
	mm_waitset_local_wait(waitset);
#endif
}

static inline void
mm_net_socket_timedwait(struct mm_net_socket *sock,
			struct mm_waitset *waitset,
			mm_timeout_t timeout)
{
#if !MM_NET_LOCAL_EVENTS
	mm_waitset_timedwait(waitset, &sock->lock, timeout);
#else
	(void) sock;
	mm_waitset_local_timedwait(waitset, timeout);
#endif
}

static void
mm_net_set_read_ready(struct mm_net_socket *sock, uint8_t flags)
{
	ENTER();

	// Update the read readiness stamp.
	uint32_t stamp = sock->read_stamp + 1;
	mm_memory_store(sock->read_stamp, stamp);

	mm_net_socket_lock(sock);

	// Update the read readiness flags.
	sock->flags |= flags;

	// Check to see if a new reader should be spawned.
	flags = sock->flags & (MM_NET_READER_SPAWNED | MM_NET_READER_PENDING);
	if (flags == MM_NET_READER_PENDING) {
		if ((sock->server->proto->flags & MM_NET_INBOUND) == 0)
			sock->flags &= ~MM_NET_READER_PENDING;

		sock->flags |= MM_NET_READER_SPAWNED;
	}

	mm_net_socket_notify(sock, &sock->read_waitset);

	// Submit a reader work if needed.
	if (flags == MM_NET_READER_PENDING)
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);

	LEAVE();
}

static void
mm_net_set_write_ready(struct mm_net_socket *sock, uint8_t flags)
{
	ENTER();

	// Update the write readiness stamp.
	uint32_t stamp = sock->write_stamp + 1;
	mm_memory_store(sock->write_stamp, stamp);

	mm_net_socket_lock(sock);

	// Update the write readiness flags.
	sock->flags |= flags;

	// Check to see if a new writer should be spawned.
	flags = sock->flags & (MM_NET_WRITER_SPAWNED | MM_NET_WRITER_PENDING);
	if (flags == MM_NET_WRITER_PENDING) {
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) == 0)
			sock->flags &= ~MM_NET_WRITER_PENDING;

		sock->flags |= MM_NET_WRITER_SPAWNED;
	}

	mm_net_socket_notify(sock, &sock->write_waitset);

	// Submit a writer work if needed.
	if (flags == MM_NET_WRITER_PENDING)
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);

	LEAVE();
}

static void
mm_net_reset_read_ready(struct mm_net_socket *sock, uint32_t stamp)
{
	ENTER();

	mm_net_socket_lock(sock);
	if (sock->read_stamp != stamp) {
		mm_net_socket_unlock(sock);
	} else {
		sock->flags &= ~MM_NET_READ_READY;
		mm_net_socket_unlock(sock);
#if MM_ONESHOT_HANDLERS
		bool oneshot = !(sock->server->proto->flags & MM_NET_INBOUND);
		if (oneshot) {
			struct mm_core *core = mm_core_getptr(sock->core);
			mm_event_trigger_input(core->events, &sock->event);
		}
#endif
	}

	LEAVE();
}

static void
mm_net_reset_write_ready(struct mm_net_socket *sock, uint32_t stamp)
{
	ENTER();

	mm_net_socket_lock(sock);
	if (sock->write_stamp != stamp) {
		mm_net_socket_unlock(sock);
	} else {
		sock->flags &= ~MM_NET_WRITE_READY;
		mm_net_socket_unlock(sock);
#if MM_ONESHOT_HANDLERS
		bool oneshot = !(sock->server->proto->flags & MM_NET_OUTBOUND);
		if (oneshot) {
			struct mm_core *core = mm_core_getptr(sock->core);
			mm_event_trigger_output(core->events, &sock->event);
		}
#endif
	}

	LEAVE();
}

/**********************************************************************
 * Socket I/O event handlers.
 **********************************************************************/

static void
mm_net_input_handler(mm_event_t event __attribute__((unused)), void *data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = containerof(data, struct mm_net_socket, event);

	// Mark the socket as read ready.
	mm_net_set_read_ready(sock, MM_NET_READ_READY);

	LEAVE();
}

static void
mm_net_output_handler(mm_event_t event __attribute__((unused)), void *data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = containerof(data, struct mm_net_socket, event);

	// Mark the socket as write ready.
	mm_net_set_write_ready(sock, MM_NET_WRITE_READY);

	LEAVE();
}

static void
mm_net_control_handler(mm_event_t event, void *data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = containerof(data, struct mm_net_socket, event);

	switch (event) {
	case MM_EVENT_REGISTER:
#if 0
		if ((sock->flags & MM_NET_READER_PENDING) != 0)
			mm_net_spawn_reader(sock);
		if ((sock->flags & MM_NET_WRITER_PENDING) != 0)
			mm_net_spawn_writer(sock);
#endif
		break;

	case MM_EVENT_UNREGISTER:
		// At this time there are no and will not be any I/O messages
		// related to this socket in the event processing pipeline.
		// But there still may be active reader/writer tasks or pending
		// work items for this socket. So relying on the FIFO order of
		// the work queue submit a work item that might safely cleanup
		// the socket being the last one that refers to it.
		mm_core_post(sock->core, mm_net_cleanup, (mm_value_t) sock);
		break;

	case MM_EVENT_INPUT_ERROR:
		// Mark the socket as having a read error.
		mm_net_set_read_ready(sock, MM_NET_READ_ERROR);
		break;

	case MM_EVENT_OUTPUT_ERROR:
		// Mark the socket as having a write error.
		mm_net_set_write_ready(sock, MM_NET_WRITE_ERROR);
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

	if (mm_net_is_reader_shutdown(sock))
		goto leave;

	mm_net_socket_lock(sock);
	if ((sock->flags & MM_NET_READER_SPAWNED) != 0) {
		// If a reader is already active then request to start another
		// one as it yields.
		sock->flags |= MM_NET_READER_PENDING;
		mm_net_socket_unlock(sock);
	} else {
		// Starting a reader.
		sock->flags |= MM_NET_READER_SPAWNED;
		mm_net_socket_unlock(sock);

		// Submit a reader work.
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);

		// Let it start immediately.
#if ENABLE_SMP
		if (sock->core == mm_core_selfid())
#endif
			mm_task_yield();
	}

leave:
	LEAVE();
}

void
mm_net_spawn_writer(struct mm_net_socket *sock)
{
	ENTER();

	if (mm_net_is_writer_shutdown(sock))
		goto leave;

	mm_net_socket_lock(sock);
	if ((sock->flags & MM_NET_WRITER_SPAWNED) != 0) {
		// If a writer is already active then request to start another
		// one as it yields.
		sock->flags |= MM_NET_WRITER_PENDING;
		mm_net_socket_unlock(sock);
	} else {
		// Starting a writer.
		sock->flags |= MM_NET_WRITER_SPAWNED;
		mm_net_socket_unlock(sock);

		// Submit a writer work.
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);

		// Let it start immediately.
#if ENABLE_SMP
		if (sock->core == mm_core_selfid())
#endif
			mm_task_yield();
	}

leave:
	LEAVE();
}

void
mm_net_yield_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core_selfid());

	struct mm_task *task = sock->reader;
	if (unlikely(task == NULL))
		goto leave;
	if (unlikely((task->flags & MM_TASK_READING) == 0))
		goto leave;

	// Unbind the current task from the socket.
	ASSERT(sock->reader == mm_task_self());
	task->flags &= ~MM_TASK_READING;
	sock->reader = NULL;

	mm_net_socket_lock(sock);

	ASSERT((sock->flags & MM_NET_READER_SPAWNED) != 0);
	if (mm_net_is_reader_shutdown(sock)) {
		sock->flags &= ~MM_NET_READER_SPAWNED;
		mm_net_socket_unlock(sock);
		goto leave;
	}

	// Check to see if a new reader should be spawned.
	uint8_t fd_flags = sock->flags & (MM_NET_READ_READY | MM_NET_READ_ERROR);
	uint8_t task_flags = sock->flags & MM_NET_READER_PENDING;
	if (task_flags != 0 && fd_flags != 0) {
		// Starting a reader.
		if ((sock->server->proto->flags & MM_NET_INBOUND) == 0)
			sock->flags &= ~MM_NET_READER_PENDING;
		mm_net_socket_unlock(sock);

		// Submit a reader work.
		mm_core_post(sock->core, mm_net_reader, (mm_value_t) sock);
	} else {
		sock->flags &= ~MM_NET_READER_SPAWNED;
		mm_net_socket_unlock(sock);

		if ((fd_flags & MM_NET_READ_ERROR) != 0) {
			mm_core_post(sock->core, mm_net_closer, (mm_value_t) sock);
		}
	}

leave:
	LEAVE();
}

void
mm_net_yield_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core_selfid());

	struct mm_task *task = sock->writer;
	if (unlikely(task == NULL))
		goto leave;
	if (unlikely((task->flags & MM_TASK_WRITING) == 0))
		goto leave;

	// Unbind the current task from the socket.
	ASSERT(sock->writer == mm_task_self());
	task->flags &= ~MM_TASK_WRITING;
	sock->writer = NULL;

	mm_net_socket_lock(sock);

	ASSERT((sock->flags & MM_NET_WRITER_SPAWNED) != 0);
	if (mm_net_is_writer_shutdown(sock)) {
		sock->flags &= ~MM_NET_WRITER_SPAWNED;
		mm_net_socket_unlock(sock);
		goto leave;
	}

	// Check to see if a new writer should be spawned.
	uint8_t fd_flags = sock->flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	uint8_t task_flags = sock->flags & MM_NET_WRITER_PENDING;
	if (task_flags != 0 && fd_flags != 0) {
		// Starting a writer.
		if ((sock->server->proto->flags & MM_NET_OUTBOUND) == 0)
			sock->flags &= ~MM_NET_WRITER_PENDING;
		mm_net_socket_unlock(sock);

		// Submit a writer work.
		mm_core_post(sock->core, mm_net_writer, (mm_value_t) sock);
	} else {
		sock->flags &= ~MM_NET_WRITER_SPAWNED;
		mm_net_socket_unlock(sock);

		if ((fd_flags & MM_NET_WRITE_ERROR) != 0) {
			mm_core_post(sock->core, mm_net_closer, (mm_value_t) sock);
		}
	}

leave:
	LEAVE();
}

static mm_value_t
mm_net_prepare(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core_selfid());
	ASSERT(!mm_net_is_closed(sock));

	// Let the protocol layer prepare the socket data if needed.
	if (sock->server->proto->prepare != NULL)
		(sock->server->proto->prepare)(sock);

	// Register the socket with the event loop.
	mm_event_register_fd(mm_core->events, &sock->event);

	LEAVE();
	return 0;
}

static mm_value_t
mm_net_destroy(mm_value_t arg)
{
	ENTER();

	// At this time there are no and will not be any reader/writer tasks
	// bound to this socket.

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	//ASSERT(sock->core == mm_core_self());
	ASSERT(mm_net_is_closed(sock));

	// Close the socket.
	// TODO: set linger off and/or close concurrently to avoid stalls.
	close(sock->event.fd);
	sock->event.fd = -1;

	// Remove the socket from the server lists.
	mm_net_destroy_socket(sock);

	LEAVE();
	return 0;
}

static mm_value_t
mm_net_cleanup(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core_selfid());

	// Notify a reader/writer about closing.
	// TODO: don't block here, have a queue of closed socks
	while (sock->reader != NULL || sock->writer != NULL) {
		struct mm_task *task = mm_task_self();
		mm_priority_t priority = MM_PRIO_UPPER(task->priority, 1);
		if (sock->reader != NULL)
			mm_task_hoist(sock->reader, priority);
 		if (sock->writer != NULL)
			mm_task_hoist(sock->writer, priority);
		mm_task_yield();
	}

	// Run the protocol handler routine.
	if (sock->server->proto->cleanup != NULL)
		(sock->server->proto->cleanup)(sock);

	mm_core_post(0, mm_net_destroy, (mm_value_t) sock);

	LEAVE();
	return 0;
}

static mm_value_t
mm_net_reader(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *sock = (struct mm_net_socket *) arg;
	ASSERT(sock->core == mm_core_selfid());
	if (unlikely(mm_net_is_reader_shutdown(sock)))
		goto leave;

	// Register the reader task.
	struct mm_task *task = mm_task_self();
	task->flags |= MM_TASK_READING;
	sock->reader = task;

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
	ASSERT(sock->core == mm_core_selfid());
	if (unlikely(mm_net_is_writer_shutdown(sock)))
		goto leave;

	// Register the writer task.
	struct mm_task *task = mm_task_self();
	task->flags |= MM_TASK_WRITING;
	sock->writer = task;

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
	ASSERT(sock->core == mm_core_selfid());

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
		if (srv->event.fd >= 0) {
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

	mm_core_hook_start(mm_net_init_socket_table);
	mm_core_hook_stop(mm_net_free_socket_table);

	mm_net_init_server_table();
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
		if (srv->event.fd >= 0) {
			mm_net_close_server_socket(&srv->addr, srv->event.fd);
		}

		// TODO: close client sockets
	}

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
	srv->name = mm_format(&mm_global_arena, "%s (%s)", name, path);
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
	srv->name = mm_format(&mm_global_arena, "%s (%s:%d)", name, addrstr, port);
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
	srv->name = mm_format(&mm_global_arena, "%s (%s:%d)", name, addrstr, port);
	srv->proto = proto;

	if (mm_net_set_in6_addr(&srv->addr, addrstr, port) < 0)
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'",
			 name, addrstr, port);

	LEAVE();
	return srv;
}

void
mm_net_set_server_affinity(struct mm_net_server *srv, struct mm_bitset *mask)
{
	ENTER();

	mm_bitset_clear_all(&srv->affinity);
	mm_bitset_or(&srv->affinity, mask);

	LEAVE();
}

void
mm_net_start_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->event.fd == -1);

	mm_brief("start server '%s'", srv->name);

	// Find the cores to run the server on.
	if (mm_bitset_any(&srv->affinity))
		mm_bitset_and(&srv->affinity, mm_core_get_event_affinity());
	else
		mm_bitset_or(&srv->affinity, mm_core_get_event_affinity());
	srv->core_num = mm_bitset_count(&srv->affinity);
	if (srv->core_num == 0)
		mm_fatal(0, "the server cannot be bound to any core");

	// Bind the server to the specified cores.
	mm_core_t c = 0;
	srv->per_core = mm_global_calloc(srv->core_num, sizeof(struct mm_net_server_per_core));
	for (mm_core_t i = 0; i < mm_core_getnum(); i++) {
		if (mm_bitset_test(&srv->affinity, i)) {
			ASSERT(c < srv->core_num);
			srv->per_core[c].core = i;

			/* Initialize the client list. */
			mm_list_init(&srv->per_core[c].clients);

			if (c == 0)
				srv->core = i;
			c++;
		}
	}
	ASSERT(c == srv->core_num);

	// Create the server socket.
	int fd = mm_net_open_server_socket(&srv->addr, 0);
	mm_verbose("bind server '%s' to socket %d", srv->name, fd);

	// Allocate an event handler IDs.
	srv->input_handler = mm_event_register_handler(mm_net_input_handler);
	srv->output_handler = mm_event_register_handler(mm_net_output_handler);
	srv->control_handler = mm_event_register_handler(mm_net_control_handler);

	// Register the server socket with the event loop.
	mm_event_prepare_fd(&srv->event, fd, mm_net_accept_hid, false, 0, false, 0);
	struct mm_core *core = mm_core_getptr(srv->core);
	mm_event_register_fd(core->events, &srv->event);

	LEAVE();
}

void
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->event.fd != -1);

	mm_brief("stop server: %s", srv->name);

	// Unregister the socket.
	struct mm_core *core = mm_core_getptr(srv->core);
	mm_event_unregister_fd(core->events, &srv->event);

	// Close the socket.
	mm_net_close_server_socket(&srv->addr, srv->event.fd);
	srv->event.fd = -1;

	LEAVE();
}

/**********************************************************************
 * Network sockets.
 **********************************************************************/

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
	uint8_t flags = mm_memory_load(sock->flags);
	flags &= (MM_NET_READ_READY | MM_NET_READ_ERROR);
	if (flags != 0) {
		goto leave;
	}

	mm_net_socket_lock(sock);

	// Check to see if the socket is read ready again.
	flags = sock->flags & (MM_NET_READ_READY | MM_NET_READ_ERROR);
	if (flags != 0) {
		mm_net_socket_unlock(sock);
		goto leave;
	}

	// Block the task waiting for the socket to become read ready.
	if (sock->read_timeout == MM_TIMEOUT_INFINITE) {
		mm_net_socket_wait(sock, &sock->read_waitset);
		rc = 0;
	} else if (mm_core->time_manager.time < deadline) {
		mm_timeout_t timeout = deadline - mm_core->time_manager.time;
		mm_net_socket_timedwait(sock, &sock->read_waitset, timeout);
		rc = 0;
	} else {
		mm_net_socket_unlock(sock);
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
	uint8_t flags = mm_memory_load(sock->flags);
	flags &= (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	if (flags != 0) {
		goto leave;
	}

	mm_net_socket_lock(sock);

	// Check to see if the socket is write ready again.
	flags = sock->flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	if (flags != 0) {
		mm_net_socket_unlock(sock);
		goto leave;
	}

	// Block the task waiting for the socket to become write ready.
	if (sock->write_timeout == MM_TIMEOUT_INFINITE) {
		mm_net_socket_wait(sock, &sock->write_waitset);
		rc = 0;
	} else  if (mm_core->time_manager.time < deadline) {
		mm_timeout_t timeout = deadline - mm_core->time_manager.time;
		mm_net_socket_timedwait(sock, &sock->write_waitset, timeout);
		rc = 0;
	} else {
		mm_net_socket_unlock(sock);
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
	ASSERT(sock->core == mm_core_selfid());
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_manager.time + sock->read_timeout;

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
	n = read(sock->event.fd, buffer, nbytes);
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
	ASSERT(sock->core == mm_core_selfid());
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_manager.time + sock->write_timeout;

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
	n = write(sock->event.fd, buffer, nbytes);
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
	ASSERT(sock->core == mm_core_selfid());
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_manager.time + sock->read_timeout;

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
	n = readv(sock->event.fd, iov, iovcnt);
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
	ASSERT(sock->core == mm_core_selfid());
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_core->time_manager.time + sock->write_timeout;

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
	n = writev(sock->event.fd, iov, iovcnt);
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

void
mm_net_close(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core_selfid());

	if (mm_net_is_closed(sock))
		goto leave;

	// Mark the socket as closed.
	sock->close_flags = MM_NET_CLOSED;

	// Remove the socket from the event loop.
	struct mm_core *core = mm_core_getptr(sock->core);
	mm_event_unregister_fd(core->events, &sock->event);

leave:
	LEAVE();
}

void
mm_net_shutdown_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core_selfid());

	if (mm_net_is_reader_shutdown(sock))
		goto leave;

	// Mark the socket as having the reader part closed.
	sock->close_flags |= MM_NET_READER_SHUTDOWN;

	// TODO: async this
	if (shutdown(sock->event.fd, SHUT_RD) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}

void
mm_net_shutdown_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->core == mm_core_selfid());

	if (mm_net_is_writer_shutdown(sock))
		goto leave;

	// Mark the socket as having the writer part closed.
	sock->close_flags |= MM_NET_WRITER_SHUTDOWN;

	// TODO: async this
	if (shutdown(sock->event.fd, SHUT_WR) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}
