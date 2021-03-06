/*
 * base/net/net.c - MainMemory networking.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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

#include "base/async.h"
#include "base/context.h"
#include "base/exit.h"
#include "base/format.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/stdcall.h"
#include "base/event/nonblock.h"
#include "base/fiber/fiber.h"
#include "base/memory/alloc.h"
#include "base/memory/arena.h"
#include "base/memory/pool.h"

#include <netinet/tcp.h>
#include <unistd.h>

/**********************************************************************
 * Socket helper routines.
 **********************************************************************/

static int NONNULL(1)
mm_net_open_server_socket(struct mm_net_addr *addr, int backlog)
{
	// Create the socket.
	int sock = mm_socket(addr->addr.sa_family, SOCK_STREAM, 0);
	if (sock < 0)
		mm_fatal(errno, "socket()");

	// Set socket options.
	int val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) < 0)
		mm_fatal(errno, "setsockopt(..., SO_REUSEADDR, ...)");
	if (addr->addr.sa_family == AF_INET6
	    && setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) < 0)
		mm_fatal(errno, "setsockopt(..., IPV6_V6ONLY, ...)");

	// Bind the socket to the given address.
	socklen_t salen = mm_net_sockaddr_len(addr->addr.sa_family);
	if (mm_bind(sock, &addr->addr, salen) < 0)
		mm_fatal(errno, "bind()");

	// Make the socket ready to accept connections.
	if (mm_listen(sock, backlog > 0 ? backlog : SOMAXCONN) < 0)
		mm_fatal(errno, "listen()");

	// Make the socket non-blocking.
	mm_set_nonblocking(sock);

	return sock;
}

static void
mm_net_set_socket_options(int fd, uint32_t options)
{
	// Set the socket options.
	int val = 1;
	struct linger lin = { .l_onoff = 0, .l_linger = 0 };
	if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin) < 0)
		mm_error(errno, "setsockopt(..., SO_LINGER, ...)");
	if ((options & MM_NET_KEEPALIVE) != 0 && setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if ((options & MM_NET_NODELAY) != 0 && setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., TCP_NODELAY, ...)");

	// Make the socket non-blocking.
	mm_set_nonblocking(fd);
}

static void NONNULL(1)
mm_net_remove_unix_socket(struct mm_net_addr *addr)
{
	if (addr->addr.sa_family == AF_UNIX) {
		mm_brief("removing %s", addr->un_addr.sun_path);
		if (unlink(addr->un_addr.sun_path) < 0) {
			mm_error(errno, "unlink(\"%s\")", addr->un_addr.sun_path);
		}
	}
}

static void NONNULL(1)
mm_net_close_server_socket(struct mm_net_addr *addr, int sock)
{
	TRACE("sock: %d", sock);

	// Close the socket.
	mm_close(sock);

	// Remove the Unix-domain socket file.
	mm_net_remove_unix_socket(addr);
}

/**********************************************************************
 * Socket create and destroy routines.
 **********************************************************************/

static struct mm_net_socket *
mm_net_socket_alloc(void)
{
	return mm_memory_xalloc(sizeof(struct mm_net_socket));
}

static void
mm_net_socket_free(struct mm_event_fd *sink)
{
	mm_memory_free(containerof(sink, struct mm_net_socket, event));
}

static struct mm_net_socket *
mm_net_create_accepted(struct mm_net_proto *proto)
{
	return proto->create != NULL ? (proto->create)() : mm_net_socket_alloc();
}

/**********************************************************************
 * Socket initialization.
 **********************************************************************/

static void NONNULL(1)
mm_net_prepare(struct mm_net_socket *sock)
{
	sock->read_timeout = MM_TIMEOUT_INFINITE;
	sock->write_timeout = MM_TIMEOUT_INFINITE;
}

static void
mm_net_prepare_accepted(struct mm_net_socket *sock, int fd, struct mm_net_server *srv)
{
	uint32_t options = srv->proto->options;
	if (srv->proto->reader == NULL && srv->proto->writer != NULL)
		options |= MM_NET_EGRESS;

	// Assume that an accepted socket is ready for output right away.
	uint32_t flags = MM_EVENT_OUTPUT_READY;
	if ((options & MM_NET_EGRESS) == 0) {
		VERIFY(srv->proto->reader != NULL);
		flags |= MM_EVENT_REGULAR_INPUT;
	} else {
		VERIFY(srv->proto->writer != NULL);
		flags |= MM_EVENT_REGULAR_OUTPUT;
	}
	if ((options & MM_NET_BOUND) != 0)
		flags |= MM_EVENT_FIXED_POLLER;

	// Initialize the event sink.
	mm_event_prepare_fd(&sock->event, fd, flags, &srv->tasks, srv->proto->destroy != NULL ? srv->proto->destroy : mm_net_socket_free);
	// Initialize common socket fields.
	mm_net_prepare(sock);
}

/**********************************************************************
 * Server connection acceptor.
 **********************************************************************/

static void
mm_net_register_sock_req(struct mm_context *const context, uintptr_t *arguments)
{
	// Fetch the arguments.
	struct mm_net_socket *const sock = (struct mm_net_socket *) arguments[0];

	// Register the socket for event dispatch.
	mm_event_register_fd(&sock->event, context);
}

static bool
mm_net_accept(struct mm_net_server *srv, struct mm_context *const context)
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
	fd = mm_accept(srv->event.fd, (struct sockaddr *) &sa, &salen);
	if (unlikely(fd < 0)) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_error(errno, "%s: accept()", srv->name);
		} else {
			srv->event.flags &= ~MM_EVENT_INPUT_READY;
			rc = false;
		}
		goto leave;
	}

	// Set common socket options.
	mm_net_set_socket_options(fd, srv->proto->options);

	// Allocate a new socket structure.
	struct mm_net_socket *sock = mm_net_create_accepted(srv->proto);
	if (unlikely(sock == NULL)) {
		mm_error(0, "%s: failed to allocate a socket", srv->name);
		mm_close(fd);
		goto leave;
	}

	// Initialize the socket structure.
	mm_net_prepare_accepted(sock, fd, srv);
	if (sa.ss_family == AF_INET)
		memcpy(&sock->peer.in_addr, &sa, sizeof(sock->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&sock->peer.in6_addr, &sa, sizeof(sock->peer.in6_addr));
	else
		sock->peer.addr.sa_family = sa.ss_family;

	// Choose a target context.
	struct mm_context *const target_context = mm_thread_ident_to_context(srv->assignment_target);
	if (++(srv->assignment_counter) >= 2) {
		size_t next = mm_bitset_find(&srv->affinity, srv->assignment_target + 1);
		if (next == MM_BITSET_NONE) {
			next = mm_bitset_find(&srv->affinity, 0);
			if (next == MM_BITSET_NONE)
				next = 0;
		}
		srv->assignment_target = next;
		srv->assignment_counter = 0;
	}

	// Register the socket for event dispatch.
	if (target_context == context) {
		mm_event_register_fd(&sock->event, context);
	} else {
		mm_async_call_1(target_context, mm_net_register_sock_req, (uintptr_t) sock);
	}

leave:
	LEAVE();
	return rc;
}

static mm_value_t
mm_net_acceptor(mm_value_t arg)
{
	ENTER();

	// Find the pertinent server.
	struct mm_net_server *const server = (struct mm_net_server *) arg;
	struct mm_context *const context = mm_net_get_server_context(server);

	// Accept incoming connections.
	while (mm_net_accept(server, context))
		mm_fiber_yield(context);

	LEAVE();
	return 0;
}

/**********************************************************************
 * Network servers.
 **********************************************************************/

/* Global server list. */
static struct mm_list MM_LIST_INIT(mm_net_server_list);
/* Acceptor I/O tasks. */
static struct mm_event_io mm_net_acceptor_tasks;

static void
mm_net_exit_cleanup(void)
{
	ENTER();

	// Go through the the global server list and remove files
	// associated with unix-domain sockets.
	struct mm_link *link = mm_list_head(&mm_net_server_list);
	while (!mm_list_is_tail(&mm_net_server_list, link)) {
		struct mm_net_server *srv = containerof(link, struct mm_net_server, link);
		if (srv->event.fd >= 0)
			mm_net_remove_unix_socket(&srv->addr);
		link = link->next;
	}

	LEAVE();
}

static void
mm_net_shutdown_server(struct mm_net_server *srv)
{
	ENTER();

	// Remove a server from the global list.
	mm_list_delete(&srv->link);

	// Close the server socket if it's open.
	if (srv->event.fd >= 0)
		mm_net_close_server_socket(&srv->addr, srv->event.fd);

	// Free all the server data.
	mm_bitset_cleanup(&srv->affinity, &mm_memory_xarena);
	mm_memory_free(srv->name);
	mm_memory_free(srv);

	LEAVE();
}

static mm_value_t
mm_net_register_server(mm_value_t arg)
{
	ENTER();

	// Register the server socket with the event loop.
	struct mm_net_server *srv = (struct mm_net_server *) arg;
	ASSERT(srv->event.fd >= 0);
	mm_event_register_fd(&srv->event, mm_context_selfptr());

	LEAVE();
	return 0;
}

static struct mm_net_server *
mm_net_alloc_server(struct mm_net_proto *proto)
{
	ENTER();

	// On the very first server do global initialization.
	if (mm_list_empty(&mm_net_server_list)) {
		// Register the server cleanup routine.
		mm_atexit(mm_net_exit_cleanup);
		// Prepare acceptor I/O tasks.
		mm_event_prepare_io(&mm_net_acceptor_tasks, mm_net_acceptor, NULL);
	}

	// Allocate a server.
	struct mm_net_server *srv = mm_memory_xalloc(sizeof(struct mm_net_server));

	// Initialize its data.
	srv->proto = proto;
	srv->event.fd = -1;
	srv->event.flags = MM_EVENT_REGULAR_INPUT;
	srv->name = NULL;
	mm_event_prepare_io(&srv->tasks, proto->reader, proto->writer);
	mm_bitset_prepare(&srv->affinity, &mm_memory_xarena, 0);

	// Register the server stop hook.
	mm_common_stop_hook_1((void (*)(void *)) mm_net_shutdown_server, srv);

	// Link it to the global server list.
	mm_list_append(&mm_net_server_list, &srv->link);

	LEAVE();
	return srv;
}

static void
mm_net_destroy_server(struct mm_event_fd *sink UNUSED)
{
	// TODO: This is a stub that never gets called as servers are never
	// unregistered from event listeners. As servers are created before
	// event loops are started so logically servers should be destroyed
	// after event loops are finished. But that never happens too.
}

static void NONNULL(1)
mm_net_start_server(struct mm_net_server *srv)
{
	ENTER();

	mm_brief("start server '%s'", srv->name);
	ASSERT(srv->event.fd == -1);

	// Find the thread to run the server on.
	const size_t nthreads = mm_number_of_regular_threads();
	if (mm_bitset_size(&srv->affinity) == 0) {
		mm_bitset_cleanup(&srv->affinity, &mm_memory_xarena);
		mm_bitset_prepare(&srv->affinity, &mm_memory_xarena, nthreads);
		mm_bitset_set_all(&srv->affinity);
	} else if (!mm_bitset_any(&srv->affinity)) {
		mm_bitset_cleanup(&srv->affinity, &mm_memory_xarena);
		mm_bitset_prepare(&srv->affinity, &mm_memory_xarena, 1);
		mm_bitset_set_all(&srv->affinity);
	} else if (mm_bitset_size(&srv->affinity) > nthreads) {
		struct mm_bitset tmp;
		mm_bitset_prepare(&tmp, &mm_memory_xarena, nthreads);
		mm_bitset_or(&tmp, &srv->affinity);
		mm_bitset_cleanup(&srv->affinity, &mm_memory_xarena);
		srv->affinity = tmp;
	}
	srv->assignment_target = mm_bitset_find(&srv->affinity, 0);
	srv->assignment_counter = 0;

	// Create the server socket.
	int fd = mm_net_open_server_socket(&srv->addr, 0);
	mm_verbose("bind server '%s' to socket %d", srv->name, fd);

	// Register the server socket with the event loop.
	mm_event_prepare_fd(&srv->event, fd, MM_EVENT_REGULAR_INPUT, &mm_net_acceptor_tasks, mm_net_destroy_server);

	MM_TASK(register_task, mm_net_register_server, mm_task_complete_noop, mm_task_reassign_off);
	struct mm_context *context = mm_thread_ident_to_context(srv->assignment_target);
	mm_context_send_task(context, &register_task, (mm_value_t) srv);

	LEAVE();
}

static void NONNULL(1)
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->event.fd != -1);
	ASSERT(mm_net_get_server_context(srv) == mm_context_selfptr());

	mm_brief("stop server: %s", srv->name);

	// Unregister the socket.
	mm_event_close_fd(&srv->event);

	// Close the socket.
	mm_net_close_server_socket(&srv->addr, srv->event.fd);
	srv->event.fd = -1;

	LEAVE();
}

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_unix_server(const char *name, struct mm_net_proto *proto,
			  const char *path)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server(proto);
	srv->name = mm_format(&mm_memory_xarena, "%s (%s)", name, path);
	if (!mm_net_set_unix_addr(&srv->addr, path))
		mm_fatal(0, "failed to create '%s' server with path '%s'", name, path);

	LEAVE();
	return srv;
}

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_inet_server(const char *name, struct mm_net_proto *proto,
			  const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server(proto);
	srv->name = mm_format(&mm_memory_xarena, "%s (%s:%d)", name, addrstr, port);
	if (!mm_net_set_inet_addr(&srv->addr, addrstr, port))
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'", name, addrstr, port);

	LEAVE();
	return srv;
}

struct mm_net_server * NONNULL(1, 2, 3)
mm_net_create_inet6_server(const char *name, struct mm_net_proto *proto,
			   const char *addrstr, uint16_t port)
{
	ENTER();

	struct mm_net_server *srv = mm_net_alloc_server(proto);
	srv->name = mm_format(&mm_memory_xarena, "%s (%s:%d)", name, addrstr, port);
	if (!mm_net_set_inet6_addr(&srv->addr, addrstr, port))
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'", name, addrstr, port);

	LEAVE();
	return srv;
}

void NONNULL(1, 2)
mm_net_set_server_affinity(struct mm_net_server *srv, struct mm_bitset *mask)
{
	ENTER();

	// Reset the old affinity mask value.
	size_t size = mm_bitset_size(mask);
	if (mm_bitset_size(&srv->affinity) == size) {
		mm_bitset_clear_all(&srv->affinity);
	} else {
		mm_bitset_cleanup(&srv->affinity, &mm_memory_xarena);
		mm_bitset_prepare(&srv->affinity, &mm_memory_xarena, size);
	}

	// Assign the new affinity mask value.
	mm_bitset_or(&srv->affinity, mask);

	LEAVE();
}

void NONNULL(1)
mm_net_setup_server(struct mm_net_server *srv)
{
	ENTER();

	// Register the server start hook.
	mm_regular_start_hook_1((void (*)(void *)) mm_net_start_server, srv);

	// Register the server stop hook.
	mm_regular_stop_hook_1((void (*)(void *)) mm_net_stop_server, srv);

	LEAVE();
}

/**********************************************************************
 * Network client connection sockets.
 **********************************************************************/

void NONNULL(1, 2)
mm_net_prepare_for_connect(struct mm_net_socket *sock, void (*destroy)(struct mm_event_fd *))
{
	// Initialize the event sink.
	mm_event_prepare_fd(&sock->event, -1, MM_EVENT_FIXED_POLLER | MM_EVENT_OUTPUT_READY, mm_event_instant_io(), destroy);
	// Initialize common socket fields.
	mm_net_prepare(sock);
}

struct mm_net_socket *
mm_net_create(void)
{
	ENTER();

	// Create the socket.
	struct mm_net_socket *sock = mm_net_socket_alloc();
	// Initialize the socket basic fields.
	mm_net_prepare_for_connect(sock, mm_net_socket_free);

	LEAVE();
	return sock;
}

void NONNULL(1)
mm_net_destroy(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(sock->event.fd < 0);

	(sock->event.destroy)(&sock->event);

	LEAVE();
}

int NONNULL(1, 2)
mm_net_connect(struct mm_net_socket *sock, const struct mm_net_addr *addr)
{
	ENTER();
	int rc = -1;

	// Create the socket.
	const int fd = mm_socket(addr->addr.sa_family, SOCK_STREAM, 0);
	if (fd < 0) {
		int saved_errno = errno;
		mm_error(saved_errno, "socket()");
		errno = saved_errno;
		goto leave;
	}
	sock->event.fd = fd;

	// Set common socket options.
	mm_net_set_socket_options(fd, 0);

	// Initiate the connection.
	socklen_t salen = mm_net_sockaddr_len(addr->addr.sa_family);
retry:
	rc = mm_connect(fd, &addr->addr, salen);
	if (rc < 0) {
		if (errno == EINTR)
			goto retry;
		if (errno != EINPROGRESS) {
			int saved_errno = errno;
			mm_close(fd);
			mm_error(saved_errno, "connect()");
			errno = saved_errno;
			goto leave;
		}
	}

	// Register the socket in the event loop.
	struct mm_context *const context = mm_context_selfptr();
	mm_event_register_fd(&sock->event, context);

	// Handle the EINPROGRESS case.
	if (rc < 0) {
		mm_event_trigger_output(&sock->event, context);

		// Block the fiber waiting for connection completion.
		sock->event.output_fiber = context->fiber;
		while ((sock->event.flags & (MM_EVENT_OUTPUT_READY | MM_EVENT_OUTPUT_ERROR)) == 0) {
			mm_fiber_block(context);
			// TODO: mm_fiber_testcancel();
		}
		sock->event.output_fiber = NULL;

		// Check the connection outcome.
		int conn_errno = 0;
		socklen_t len = sizeof(conn_errno);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &conn_errno, &len) < 0)
			mm_fatal(errno, "getsockopt(..., SO_ERROR, ...)");
		if (conn_errno == 0) {
			rc = 0;
		} else {
			mm_event_close_broken_fd(&sock->event);
			sock->event.fd = -1;
			mm_close(fd);
			errno = conn_errno;
		}
	}

leave:
	LEAVE();
	return rc;
}

int NONNULL(1, 2)
mm_net_connect_inet(struct mm_net_socket *sock, const char *addrstr, uint16_t port)
{
	ENTER();
	int rc;

	struct mm_net_addr addr;
	if (mm_net_parse_in_addr(&addr.in_addr, addrstr, port)) {
		rc = mm_net_connect(sock, &addr);
	} else {
		errno = EINVAL;
		rc = -1;
	}

	LEAVE();
	return rc;
}

int NONNULL(1, 2)
mm_net_connect_inet6(struct mm_net_socket *sock, const char *addrstr, uint16_t port)
{
	ENTER();
	int rc;

	struct mm_net_addr addr;
	if (mm_net_parse_in6_addr(&addr.in6_addr, addrstr, port)) {
		rc = mm_net_connect(sock, &addr);
	} else {
		errno = EINVAL;
		rc = -1;
	}

	LEAVE();
	return rc;
}

/**********************************************************************
 * Network socket I/O.
 **********************************************************************/

static void
mm_net_error(struct mm_net_socket *sock, const char *where)
{
	const int saved_errno = errno;
	mm_error(saved_errno, "%s(%d, ...)", where, sock->event.fd);
	errno = saved_errno;
}

static ssize_t
mm_net_input_closed(struct mm_net_socket *sock)
{
	if (mm_event_input_closed(&sock->event)) {
		errno = EBADF;
		return -1;
	}
	return 0;
}

static ssize_t
mm_net_output_closed(struct mm_net_socket *sock)
{
	if (mm_event_output_closed(&sock->event)) {
		errno = EBADF;
		return -1;
	}
	return 0;
}

// Block the fiber waiting for the socket to become read ready.
static ssize_t NONNULL(1, 2)
mm_net_input_wait(struct mm_net_socket *const sock, struct mm_context *const context, const mm_timeval_t deadline)
{
	ENTER();
	int rc = 0;

	do {
		if (deadline == MM_TIMEVAL_MAX) {
			sock->event.input_fiber = context->fiber;
			mm_fiber_block(context);
			sock->event.input_fiber = NULL;
		} else {
			mm_timeval_t time = mm_context_gettime(context);
			DEBUG("now: %lu, deadline: %lu", time, deadline);
			if (time < deadline) {
				sock->event.input_fiber = context->fiber;
				mm_fiber_pause(context, deadline - time);
				sock->event.input_fiber = NULL;
			} else {
				if (sock->read_timeout != 0)
					errno = ETIMEDOUT;
				else
					errno = EAGAIN;
				rc = -1;
				break;
			}
		}

		// Check if the fiber is canceled.
		mm_fiber_testcancel();

		// Check if the socket is closed for input.
		if (mm_event_input_closed(&sock->event)) {
			errno = EBADF;
			rc = -1;
			break;
		}

	} while (!mm_event_input_ready(&sock->event));

	LEAVE();
	return rc;
}

// Block the fiber waiting for the socket to become write ready.
static ssize_t NONNULL(1, 2)
mm_net_output_wait(struct mm_net_socket *const sock, struct mm_context *const context, const mm_timeval_t deadline)
{
	ENTER();
	int rc = 0;

	do {
		if (deadline == MM_TIMEVAL_MAX) {
			sock->event.output_fiber = context->fiber;
			mm_fiber_block(context);
			sock->event.output_fiber = NULL;
		} else {
			mm_timeval_t time = mm_context_gettime(context);
			DEBUG("now: %lu, deadline: %lu", time, deadline);
			if (time < deadline) {
				sock->event.output_fiber = context->fiber;
				mm_fiber_pause(context, deadline - time);
				sock->event.output_fiber = NULL;
			} else {
				if (sock->write_timeout != 0)
					errno = ETIMEDOUT;
				else
					errno = EAGAIN;
				rc = -1;
				break;
			}
		}

		// Check if the fiber is canceled.
		mm_fiber_testcancel();

		// Check if the socket is closed for output.
		if (mm_event_output_closed(&sock->event)) {
			errno = EBADF;
			rc = -1;
			break;
		}

	} while (!mm_event_output_ready(&sock->event));

	LEAVE();
	return rc;
}

ssize_t NONNULL(1, 2)
mm_net_read(struct mm_net_socket *sock, void *buffer, const size_t nbytes)
{
	ENTER();
	DEBUG("nbytes: %zu", nbytes);
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	// Check if the socket is closed.
	ssize_t n = mm_net_input_closed(sock);
	if (n < 0)
		goto leave;

	// Try to read fast (nonblocking).
	if (mm_event_input_ready(&sock->event)) {
retry:
		if ((n = mm_read(sock->event.fd, buffer, nbytes)) >= 0)
			goto check;
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "read");
			goto leave;
		}
	}

	// Remember the wait time.
	struct mm_context *const context = mm_net_get_socket_context(sock);
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_context_gettime(context) + sock->read_timeout;

	for (;;) {
		// Turn on the input event notification if needed.
		mm_event_trigger_input(&sock->event, context);

		// Try to read again (nonblocking).
		if ((n = mm_read(sock->event.fd, buffer, nbytes)) >= 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "read");
			goto leave;
		}

		// Wait for input readiness.
		if ((n = mm_net_input_wait(sock, context, deadline)) < 0)
			goto leave;
	}

check:
	// Check for incomplete read. But if n is equal to zero then it's closed for reading.
	if (n != 0 && (size_t) n < nbytes) {
		DEBUG("reset input ready flag");
		mm_event_reset_input_ready(&sock->event);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_write(struct mm_net_socket *sock, const void *buffer, const size_t nbytes)
{
	ENTER();
	DEBUG("nbytes: %zu", nbytes);
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	// Check if the socket is closed.
	ssize_t n = mm_net_output_closed(sock);
	if (n < 0)
		goto leave;

	// Try to write fast (nonblocking).
	if (mm_event_output_ready(&sock->event)) {
retry:
		if ((n = mm_write(sock->event.fd, buffer, nbytes)) >= 0)
			goto check;
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "write");
			goto leave;
		}
	}

	// Remember the wait time.
	struct mm_context *const context = mm_net_get_socket_context(sock);
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_context_gettime(context) + sock->write_timeout;

	for (;;) {
		// Turn on the output event notification if needed.
		mm_event_trigger_output(&sock->event, context);

		// Try to write again (nonblocking).
		if ((n = mm_write(sock->event.fd, buffer, nbytes)) >= 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "write");
			goto leave;
		}

		// Wait for output readiness.
		if ((n = mm_net_output_wait(sock, context, deadline)) < 0)
			goto leave;
	}

check:
	// Check for incomplete write.
	if ((size_t) n < nbytes) {
		DEBUG("reset output ready flag");
		mm_event_reset_output_ready(&sock->event);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, const int iovcnt, const size_t nbytes)
{
	ENTER();
	DEBUG("nbytes: %zu", nbytes);
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	// Check if the socket is closed.
	ssize_t n = mm_net_input_closed(sock);
	if (n < 0)
		goto leave;

	// Try to read fast (nonblocking).
	if (mm_event_input_ready(&sock->event)) {
retry:
		if ((n = mm_readv(sock->event.fd, iov, iovcnt)) >= 0)
			goto check;
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "readv");
			goto leave;
		}
	}

	// Remember the start time.
	struct mm_context *const context = mm_net_get_socket_context(sock);
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_context_gettime(context) + sock->read_timeout;

	for (;;) {
		// Turn on the input event notification if needed.
		mm_event_trigger_input(&sock->event, context);

		// Try to read again (nonblocking).
		n = mm_readv(sock->event.fd, iov, iovcnt);
		if (n >= 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "readv");
			goto leave;
		}

		// Wait for input readiness.
		if ((n = mm_net_input_wait(sock, context, deadline)) < 0)
			goto leave;
	}

check:
	// Check for incomplete read. But if n is equal to zero then it's closed for reading.
	if (n != 0 && (size_t) n < nbytes) {
		DEBUG("reset input ready flag");
		mm_event_reset_input_ready(&sock->event);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, const int iovcnt, const size_t nbytes)
{
	ENTER();
	DEBUG("nbytes: %zu", nbytes);
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	// Check if the socket is closed.
	ssize_t n = mm_net_output_closed(sock);
	if (n < 0)
		goto leave;

	// Try to write fast (nonblocking).
	if (mm_event_output_ready(&sock->event)) {
retry:
		if ((n = mm_writev(sock->event.fd, iov, iovcnt)) >= 0)
			goto check;
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "writev");
			goto leave;
		}
	}

	// Remember the start time.
	struct mm_context *const context = mm_net_get_socket_context(sock);
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE)
		deadline = mm_context_gettime(context) + sock->write_timeout;

	for (;;) {
		// Turn on the output event notification if needed.
		mm_event_trigger_output(&sock->event, context);

		// Try to write again (nonblocking).
		n = mm_writev(sock->event.fd, iov, iovcnt);
		if (n >= 0)
			break;
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			mm_net_error(sock, "writev");
			goto leave;
		}

		// Wait for output readiness.
		if ((n = mm_net_output_wait(sock, context, deadline)) < 0)
			goto leave;
	}

check:
	// Check for incomplete write.
	if ((size_t) n < nbytes) {
		DEBUG("reset output ready flag");
		mm_event_reset_output_ready(&sock->event);
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

void NONNULL(1)
mm_net_close(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	if (mm_net_is_closed(sock))
		goto leave;

	// Remove the socket from the event loop.
	mm_event_close_fd(&sock->event);

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_reset(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	if (mm_net_is_closed(sock))
		goto leave;

	// Disable the time-wait connection state.
	struct linger lin = { .l_onoff = 1, .l_linger = 0 };
	if (setsockopt(sock->event.fd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin) < 0)
		mm_error(errno, "setsockopt(..., SO_LINGER, ...)");

	// Remove the socket from the event loop.
	mm_event_close_fd(&sock->event);

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_shutdown_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	if (mm_net_is_reader_shutdown(sock))
		goto leave;

	// Mark the socket as having the reader part closed.
	mm_event_set_input_closed(&sock->event);

	// Ask the system to close the reader part.
	if (mm_shutdown(sock->event.fd, SHUT_RD) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_shutdown_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_net_get_socket_context(sock) == mm_context_selfptr());

	if (mm_net_is_writer_shutdown(sock))
		goto leave;

	// Mark the socket as having the writer part closed.
	mm_event_set_output_closed(&sock->event);

	// Ask the system to close the writer part.
	if (mm_shutdown(sock->event.fd, SHUT_WR) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}
