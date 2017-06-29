/*
 * net/net.c - MainMemory networking.
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

#include "net/net.h"

#include "base/exit.h"
#include "base/format.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/stdcall.h"
#include "base/event/event.h"
#include "base/event/nonblock.h"
#include "base/fiber/fiber.h"
#include "base/fiber/timer.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
#include "base/memory/pool.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

/**********************************************************************
 * Network address manipulation routines.
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

static bool NONNULL(1)
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

static bool NONNULL(1)
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

bool NONNULL(1)
mm_net_set_inet_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	return mm_net_parse_in_addr(&addr->in_addr, addrstr, port);
}

bool NONNULL(1)
mm_net_set_inet6_addr(struct mm_net_addr *addr, const char *addrstr, uint16_t port)
{
	return mm_net_parse_in6_addr(&addr->in6_addr, addrstr, port);
}

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
mm_net_set_socket_options(int fd, uint32_t flags)
{
	// Set the socket options.
	int val = 1;
	struct linger lin = { .l_onoff = 0, .l_linger = 0 };
	if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin) < 0)
		mm_error(errno, "setsockopt(..., SO_LINGER, ...)");
	if ((flags & MM_NET_KEEPALIVE) != 0 && setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof val) < 0)
		mm_error(errno, "setsockopt(..., SO_KEEPALIVE, ...)");
	if ((flags & MM_NET_NODELAY) != 0 && setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val) < 0)
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
	return mm_regular_alloc(sizeof(struct mm_net_socket));
}

static void
mm_net_socket_free(struct mm_net_socket *sock)
{
	mm_regular_free(sock);
}

static struct mm_net_socket *
mm_net_socket_create(struct mm_net_proto *proto)
{
	ENTER();

	struct mm_net_socket *sock;
	if (proto->create != NULL)
		sock = (proto->create)();
	else
		sock = mm_net_socket_alloc();

	LEAVE();
	return sock;
}

static void
mm_net_socket_destroy(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT((sock->flags & MM_NET_CLIENT) == 0);

	if (sock->proto->destroy != NULL)
		(sock->proto->destroy)(sock);
	else
		mm_net_socket_free(sock);

	LEAVE();
}

static mm_value_t
mm_net_reclaim_routine(struct mm_work *work)
{
	ENTER();

	struct mm_net_socket *sock = containerof(work, struct mm_net_socket, reclaim_work);
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	// Notify a reader/writer about closing.
	// TODO: don't block here, have a queue of closed socks
	while (sock->reader != NULL || sock->writer != NULL) {
		struct mm_fiber *fiber = mm_fiber_selfptr();
		mm_priority_t priority = MM_PRIO_UPPER(fiber->priority, 1);
		if (sock->reader != NULL)
			mm_fiber_hoist(sock->reader, priority);
 		if (sock->writer != NULL)
			mm_fiber_hoist(sock->writer, priority);
		mm_fiber_yield();
	}

	// Destroy the socket.
	ASSERT(mm_net_is_closed(sock));
	if ((sock->flags & MM_NET_CLIENT) != 0) {
		(sock->destroy)(sock);
	} else {
		mm_net_socket_destroy(sock);
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Socket initialization and cleanup.
 **********************************************************************/

static void mm_net_socket_handler(mm_event_t event, void *data);
static mm_value_t mm_net_reader_routine(struct mm_work *work);
static mm_value_t mm_net_writer_routine(struct mm_work *work);

static void
mm_net_reader_complete(struct mm_work *work, mm_value_t value UNUSED)
{
	mm_net_yield_reader(containerof(work, struct mm_net_socket, read_work));
}

static void
mm_net_writer_complete(struct mm_work *work, mm_value_t value UNUSED)
{
	mm_net_yield_writer(containerof(work, struct mm_net_socket, write_work));
}

MM_WORK_VTABLE_2(mm_net_read_vtable, mm_net_reader_routine, mm_net_reader_complete);
MM_WORK_VTABLE_2(mm_net_write_vtable, mm_net_writer_routine, mm_net_writer_complete);
MM_WORK_VTABLE_1(mm_net_reclaim_vtable, mm_net_reclaim_routine);

static void
mm_net_socket_prepare_basic(struct mm_net_socket *sock, struct mm_net_proto *proto, uint32_t flags)
{
	// Invalidate the event sink.
	sock->event.fd = -1;
	// Initialize common socket fields.
	sock->proto = proto;
	sock->flags = flags;
	sock->read_timeout = MM_TIMEOUT_INFINITE;
	sock->write_timeout = MM_TIMEOUT_INFINITE;
	sock->reader = NULL;
	sock->writer = NULL;
}

static void
mm_net_socket_prepare_event(struct mm_net_socket *sock, int fd)
{
	uint32_t flags = sock->flags;
	mm_event_sequence_t input = (flags & MM_NET_INBOUND) != 0 ? MM_EVENT_REGULAR : MM_EVENT_ONESHOT;
	mm_event_sequence_t output = (flags & MM_NET_OUTBOUND) != 0 ? MM_EVENT_REGULAR : MM_EVENT_ONESHOT;
	mm_event_affinity_t affinity = (flags & MM_NET_BOUND_EVENTS) != 0 ? MM_EVENT_BOUND : MM_EVENT_LOOSE;
	mm_event_prepare_fd(&sock->event, fd, mm_net_socket_handler, input, output, affinity);
}

static void
mm_net_socket_prepare(struct mm_net_socket *sock, struct mm_net_proto *proto, int fd)
{
	// Figure out the required flags.
	uint32_t flags = proto->flags & (MM_NET_INBOUND | MM_NET_OUTBOUND);
	if (flags == 0) {
		if (proto->reader != NULL)
			flags |= MM_NET_INBOUND;
		if (proto->writer != NULL)
			flags |= MM_NET_OUTBOUND;
	} else {
		if (proto->reader == NULL)
			flags &= ~MM_NET_INBOUND;
		if (proto->writer == NULL)
			flags &= ~MM_NET_OUTBOUND;
	}
	if ((flags & MM_NET_INBOUND) != 0)
		flags |= MM_NET_READER_PENDING;
	if ((flags & MM_NET_OUTBOUND) != 0)
		flags |= MM_NET_WRITER_PENDING;

	// Initialize basic fields.
	mm_net_socket_prepare_basic(sock, proto, flags);
	// Initialize the event sink.
	mm_net_socket_prepare_event(sock, fd);

	// Initialize the required work items.
	mm_work_prepare(&sock->read_work, &mm_net_read_vtable);
	mm_work_prepare(&sock->write_work, &mm_net_write_vtable);
	mm_work_prepare(&sock->reclaim_work, &mm_net_reclaim_vtable);
}

/**********************************************************************
 * Server connection acceptor.
 **********************************************************************/

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
	fd = mm_accept(srv->event.fd, (struct sockaddr *) &sa, &salen);
	if (unlikely(fd < 0)) {
		if (errno == EINTR)
			goto retry;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			mm_error(errno, "%s: accept()", srv->name);
		else
			rc = false;
		goto leave;
	}

	// Set common socket options.
	mm_net_set_socket_options(fd, srv->proto->flags);

	// Allocate a new socket structure.
	struct mm_net_socket *sock = mm_net_socket_create(srv->proto);
	if (unlikely(sock == NULL)) {
		mm_error(0, "%s: failed to allocate a socket", srv->name);
		mm_close(fd);
		goto leave;
	}

	// Initialize the socket structure.
	mm_net_socket_prepare(sock, srv->proto, fd);
	if (sa.ss_family == AF_INET)
		memcpy(&sock->peer.in_addr, &sa, sizeof(sock->peer.in_addr));
	else if (sa.ss_family == AF_INET6)
		memcpy(&sock->peer.in6_addr, &sa, sizeof(sock->peer.in6_addr));
	else
		sock->peer.addr.sa_family = sa.ss_family;

	// Register the socket for event dispatch.
	mm_event_register_fd(&sock->event);

leave:
	LEAVE();
	return rc;
}

static mm_value_t
mm_net_acceptor(struct mm_work *work)
{
	ENTER();

	// Find the pertinent server.
	struct mm_net_server *srv = containerof(work, struct mm_net_server, acceptor_work);

	// Accept incoming connections.
	while (mm_net_accept(srv))
		mm_fiber_yield();

	LEAVE();
	return 0;
}

static void
mm_net_acceptor_complete(struct mm_work *work, mm_value_t result UNUSED)
{
	// Find the pertinent server.
	struct mm_net_server *srv = containerof(work, struct mm_net_server, acceptor_work);

	// Indicate that the acceptor work is done.
	srv->acceptor_active = false;
}

static void
mm_net_accept_handler(mm_event_t event, void *data)
{
	ENTER();

	struct mm_net_server *srv = containerof(data, struct mm_net_server, event);

	if (likely(event == MM_EVENT_INPUT) && !srv->acceptor_active) {
		// Indicate that the acceptor work is in progress.
		srv->acceptor_active = true;
		// Really queue the acceptor work for running.
		mm_thread_t thread = mm_event_target(&srv->event);
		mm_strand_post_work(thread, &srv->acceptor_work);
	}

	LEAVE();
}

/**********************************************************************
 * Socket I/O state.
 **********************************************************************/

static void
mm_net_event_complete(struct mm_net_socket *sock)
{
	ENTER();

	if ((sock->flags & (MM_NET_READER_SPAWNED | MM_NET_WRITER_SPAWNED)) != 0)
		/* Do nothing. @suppress("Suspicious semicolon") */;
	else if ((sock->flags & (MM_NET_READ_ERROR | MM_NET_WRITE_ERROR)) != 0)
		mm_net_close(sock);
#if ENABLE_SMP
	else if (sock->proto->detach == NULL || (sock->proto->detach)(sock))
		mm_event_handle_complete(&sock->event);
#endif

	LEAVE();
}

static void
mm_net_set_read_ready(struct mm_net_socket *sock, uint32_t flags)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	// Update the read readiness flags.
	sock->flags |= flags;

	if (sock->reader != NULL) {
		// Run the reader fiber presumably blocked on the socket.
		mm_fiber_run(sock->reader);
	} else {
		// Check to see if a new reader should be spawned.
		flags = sock->flags & (MM_NET_READER_SPAWNED | MM_NET_READER_PENDING);
		if (flags == MM_NET_READER_PENDING) {
			if ((sock->flags & MM_NET_INBOUND) == 0)
				sock->flags &= ~MM_NET_READER_PENDING;
			// Remember a reader has been started.
			sock->flags |= MM_NET_READER_SPAWNED;
			// Submit a reader work.
			mm_thread_t target = mm_event_target(&sock->event);
			mm_strand_post_work(target, &sock->read_work);
		} else if (flags == 0) {
			mm_net_event_complete(sock);
		}
	}

	LEAVE();
}

static void
mm_net_set_write_ready(struct mm_net_socket *sock, uint32_t flags)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	// Update the write readiness flags.
	sock->flags |= flags;

	if (sock->writer != NULL) {
		// Run the writer fiber presumably blocked on the socket.
		mm_fiber_run(sock->writer);
	} else {
		// Check to see if a new writer should be spawned.
		flags = sock->flags & (MM_NET_WRITER_SPAWNED | MM_NET_WRITER_PENDING);
		if (flags == MM_NET_WRITER_PENDING) {
			if ((sock->flags & MM_NET_OUTBOUND) == 0)
				sock->flags &= ~MM_NET_WRITER_PENDING;
			// Remember a writer has been started.
			sock->flags |= MM_NET_WRITER_SPAWNED;
			// Submit a writer work.
			mm_thread_t target = mm_event_target(&sock->event);
			mm_strand_post_work(target, &sock->write_work);
		} else if (flags == 0) {
			mm_net_event_complete(sock);
		}
	}

	LEAVE();
}

static void
mm_net_reset_read_ready(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	sock->flags &= ~MM_NET_READ_READY;
	if ((sock->flags & MM_NET_INBOUND) == 0)
		mm_event_trigger_input(&sock->event);

	LEAVE();
}

static void
mm_net_reset_write_ready(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	sock->flags &= ~MM_NET_WRITE_READY;
	if ((sock->flags & MM_NET_OUTBOUND) == 0)
		mm_event_trigger_output(&sock->event);

	LEAVE();
}

/**********************************************************************
 * Socket I/O event handler.
 **********************************************************************/

static void
mm_net_socket_handler(mm_event_t event, void *data)
{
	ENTER();

	// Find the pertinent socket.
	struct mm_net_socket *sock = containerof(data, struct mm_net_socket, event);

	switch (event) {
	case MM_EVENT_INPUT:
		// Mark the socket as read ready.
		mm_net_set_read_ready(sock, MM_NET_READ_READY);
		break;

	case MM_EVENT_OUTPUT:
		// Mark the socket as write ready.
		mm_net_set_write_ready(sock, MM_NET_WRITE_READY);
		break;

	case MM_EVENT_INPUT_ERROR:
		// Mark the socket as having a read error.
		mm_net_set_read_ready(sock, MM_NET_READ_ERROR);
		break;

	case MM_EVENT_OUTPUT_ERROR:
		// Mark the socket as having a write error.
		mm_net_set_write_ready(sock, MM_NET_WRITE_ERROR);
		break;

	case MM_EVENT_RETIRE:
		// Close the socket.
		ASSERT(sock->event.fd >= 0);
		mm_close(sock->event.fd);
		sock->event.fd = -1;
		break;

	case MM_EVENT_RECLAIM:
		// At this time there are no and will not be any I/O messages
		// related to this socket in the event processing pipeline.
		// But there still may be active reader/writer fibers or pending
		// work items for this socket. So relying on the FIFO order of
		// the work queue submit a work item that might safely cleanup
		// the socket being the last one that refers to it.
		mm_strand_post_work(mm_event_target(&sock->event), &sock->reclaim_work);
		break;

	default:
		break;
	}

	LEAVE();
}

/**********************************************************************
 * Network I/O tasks for server sockets.
 **********************************************************************/

void
mm_net_spawn_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_reader_shutdown(sock))
		goto leave;
	if (sock->proto->reader == NULL)
		goto leave;

	if ((sock->flags & MM_NET_READER_SPAWNED) != 0) {
		// If a reader is already active then remember to start another
		// one when it ends.
		sock->flags |= MM_NET_READER_PENDING;
	} else {
		// Remember a reader has been started.
		sock->flags |= MM_NET_READER_SPAWNED;
		// Submit a reader work.
		mm_thread_t target = mm_event_target(&sock->event);
		mm_strand_post_work(target, &sock->read_work);

		// Let it start immediately.
		mm_fiber_yield();
	}

leave:
	LEAVE();
}

void
mm_net_spawn_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_writer_shutdown(sock))
		goto leave;
	if (sock->proto->writer == NULL)
		goto leave;

	if ((sock->flags & MM_NET_WRITER_SPAWNED) != 0) {
		// If a writer is already active then remember to start another
		// one when it ends.
		sock->flags |= MM_NET_WRITER_PENDING;
	} else {
		// Remember a writer has been started.
		sock->flags |= MM_NET_WRITER_SPAWNED;
		// Submit a writer work.
		mm_thread_t target = mm_event_target(&sock->event);
		mm_strand_post_work(target, &sock->write_work);

		// Let it start immediately.
		mm_fiber_yield();
	}

leave:
	LEAVE();
}

void
mm_net_yield_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

#if ENABLE_FIBER_IO_FLAGS
	struct mm_fiber *fiber = mm_fiber_selfptr();
	if (unlikely((fiber->flags & MM_FIBER_READING) == 0))
		goto leave;

	// Unbind the current fiber from the socket.
	fiber->flags &= ~MM_FIBER_READING;
#endif

	// Bail out if the socket is shutdown.
	ASSERT((sock->flags & MM_NET_READER_SPAWNED) != 0);
	if (mm_net_is_reader_shutdown(sock)) {
		sock->flags &= ~MM_NET_READER_SPAWNED;
		mm_net_event_complete(sock);
		goto leave;
	}

	// Check to see if a new reader should be spawned.
	uint32_t fd_flags = sock->flags & (MM_NET_READ_READY | MM_NET_READ_ERROR);
	if ((sock->flags & MM_NET_READER_PENDING) != 0 && fd_flags != 0) {
		if ((sock->flags & MM_NET_INBOUND) == 0)
			sock->flags &= ~MM_NET_READER_PENDING;
		// Submit a reader work.
		mm_thread_t target = mm_event_target(&sock->event);
		mm_strand_post_work(target, &sock->read_work);
	} else {
		sock->flags &= ~MM_NET_READER_SPAWNED;
		mm_net_event_complete(sock);
	}

leave:
	LEAVE();
}

void
mm_net_yield_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

#if ENABLE_FIBER_IO_FLAGS
	struct mm_fiber *fiber = mm_fiber_selfptr();
	if (unlikely((fiber->flags & MM_FIBER_WRITING) == 0))
		goto leave;

	// Unbind the current fiber from the socket.
	fiber->flags &= ~MM_FIBER_WRITING;
#endif

	// Bail out if the socket is shutdown.
	ASSERT((sock->flags & MM_NET_WRITER_SPAWNED) != 0);
	if (mm_net_is_writer_shutdown(sock)) {
		sock->flags &= ~MM_NET_WRITER_SPAWNED;
		mm_net_event_complete(sock);
		goto leave;
	}

	// Check to see if a new writer should be spawned.
	uint32_t fd_flags = sock->flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR);
	if ((sock->flags & MM_NET_WRITER_PENDING) != 0 && fd_flags != 0) {
		if ((sock->flags & MM_NET_OUTBOUND) == 0)
			sock->flags &= ~MM_NET_WRITER_PENDING;
		// Submit a writer work.
		mm_thread_t target = mm_event_target(&sock->event);
		mm_strand_post_work(target, &sock->write_work);
	} else {
		sock->flags &= ~MM_NET_WRITER_SPAWNED;
		mm_net_event_complete(sock);
	}

leave:
	LEAVE();
}

static mm_value_t
mm_net_reader_routine(struct mm_work *work)
{
	ENTER();

	struct mm_net_socket *sock = containerof(work, struct mm_net_socket, read_work);
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	if (unlikely(mm_net_is_reader_shutdown(sock)))
		goto leave;

#if ENABLE_FIBER_IO_FLAGS
	// Register the reader fiber.
	struct mm_fiber *fiber = mm_fiber_selfptr();
	fiber->flags |= MM_FIBER_READING;
#endif

	// Run the protocol handler routine.
	(sock->proto->reader)(sock);

leave:
	LEAVE();
	return 0;
}

static mm_value_t
mm_net_writer_routine(struct mm_work *work)
{
	ENTER();

	struct mm_net_socket *sock = containerof(work, struct mm_net_socket, write_work);
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	if (unlikely(mm_net_is_writer_shutdown(sock)))
		goto leave;

#if ENABLE_FIBER_IO_FLAGS
	// Register the writer fiber.
	struct mm_fiber *fiber = mm_fiber_selfptr();
	fiber->flags |= MM_FIBER_WRITING;
#endif

	// Run the protocol handler routine.
	(sock->proto->writer)(sock);

leave:
	LEAVE();
	return 0;
}

/**********************************************************************
 * Network servers.
 **********************************************************************/

/* Global server list. */
static struct mm_list MM_LIST_INIT(mm_server_list);

static void
mm_net_exit_cleanup(void)
{
	ENTER();

	// Go through the the global server list and remove files
	// associated with unix-domain sockets.
	struct mm_link *link = mm_list_head(&mm_server_list);
	while (!mm_list_is_tail(&mm_server_list, link)) {
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
	mm_bitset_cleanup(&srv->affinity, &mm_global_arena);
	mm_global_free(srv->name);
	mm_global_free(srv);

	LEAVE();
}

static mm_value_t
mm_net_register_server(struct mm_work *work)
{
	ENTER();

	// Register a server for events.
	struct mm_net_server *srv = containerof(work, struct mm_net_server, register_work);
	ASSERT(srv->event.fd >= 0);
	mm_event_register_fd(&srv->event);

	LEAVE();
	return 0;
}

static struct mm_net_server *
mm_net_alloc_server(struct mm_net_proto *proto)
{
	ENTER();

	// Allocate a server.
	struct mm_net_server *srv = mm_global_alloc(sizeof(struct mm_net_server));

	// Initialize its data.
	srv->event.fd = -1;
	srv->proto = proto;
	srv->acceptor_active = false;
	srv->name = NULL;
	MM_WORK_VTABLE_2(acceptor_vtable, mm_net_acceptor, mm_net_acceptor_complete);
	mm_work_prepare(&srv->acceptor_work, &acceptor_vtable);
	MM_WORK_VTABLE_1(register_vtable, mm_net_register_server);
	mm_work_prepare(&srv->register_work, &register_vtable);
	mm_bitset_prepare(&srv->affinity, &mm_global_arena, mm_regular_nthreads);

	// On the very first server register the server cleanup routine.
	if (mm_list_empty(&mm_server_list))
		mm_atexit(mm_net_exit_cleanup);

	// Register the server stop hook.
	mm_common_stop_hook_1((void (*)(void *)) mm_net_shutdown_server, srv);

	// Link it to the global server list.
	mm_list_append(&mm_server_list, &srv->link);

	LEAVE();
	return srv;
}

static void NONNULL(1)
mm_net_start_server(struct mm_net_server *srv)
{
	ENTER();

	mm_brief("start server '%s'", srv->name);
	ASSERT(srv->event.fd == -1);

	// Find the thread to run the server on.
	size_t target = mm_bitset_find(&srv->affinity, 0);
	if (target == MM_BITSET_NONE)
		target = 0;

	// Create the server socket.
	int fd = mm_net_open_server_socket(&srv->addr, 0);
	mm_verbose("bind server '%s' to socket %d", srv->name, fd);

	// Register the server socket with the event loop.
	mm_event_prepare_fd(&srv->event, fd, mm_net_accept_handler,
			    MM_EVENT_REGULAR, MM_EVENT_IGNORED, MM_EVENT_BOUND);
	mm_strand_post_work(target, &srv->register_work);

	LEAVE();
}

static void NONNULL(1)
mm_net_stop_server(struct mm_net_server *srv)
{
	ENTER();
	ASSERT(srv->event.fd != -1);
	ASSERT(mm_event_target(&srv->event) == mm_thread_self());

	mm_brief("stop server: %s", srv->name);

	// Unregister the socket.
	mm_event_unregister_fd(&srv->event);

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
	srv->name = mm_format(&mm_global_arena, "%s (%s)", name, path);
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
	srv->name = mm_format(&mm_global_arena, "%s (%s:%d)", name, addrstr, port);
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
	srv->name = mm_format(&mm_global_arena, "%s (%s:%d)", name, addrstr, port);
	if (!mm_net_set_inet6_addr(&srv->addr, addrstr, port))
		mm_fatal(0, "failed to create '%s' server with address '%s:%d'", name, addrstr, port);

	LEAVE();
	return srv;
}

void NONNULL(1, 2)
mm_net_set_server_affinity(struct mm_net_server *srv, struct mm_bitset *mask)
{
	ENTER();

	mm_bitset_clear_all(&srv->affinity);
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

// Zero protocol handler for client sockets.
static struct mm_net_proto mm_net_dummy_proto;

void NONNULL(1, 2)
mm_net_prepare(struct mm_net_socket *sock, void (*destroy)(struct mm_net_socket *))
{
	ENTER();

	// Initialize common fields.
	mm_net_socket_prepare_basic(sock, &mm_net_dummy_proto, MM_NET_CLIENT);
	// Initialize the destruction routine.
	sock->destroy = destroy;

	// Initialize the required work items.
	mm_work_prepare(&sock->reclaim_work, &mm_net_reclaim_vtable);

	LEAVE();
}

struct mm_net_socket *
mm_net_create(void)
{
	ENTER();

	// Create the socket.
	struct mm_net_socket *sock = mm_net_socket_alloc();
	// Initialize the socket basic fields.
	mm_net_prepare(sock, mm_net_socket_free);

	LEAVE();
	return sock;
}

void NONNULL(1)
mm_net_destroy(struct mm_net_socket *sock)
{
	ENTER();

	if (unlikely((sock->flags & MM_NET_CLIENT) == 0))
		ABORT();
	if (unlikely(sock->event.fd >= 0))
		ABORT();

	(sock->destroy)(sock);

	LEAVE();
}

int NONNULL(1, 2)
mm_net_connect(struct mm_net_socket *sock, const struct mm_net_addr *addr)
{
	ENTER();
	int rc = -1;

	// Create the socket.
	int fd = mm_socket(addr->addr.sa_family, SOCK_STREAM, 0);
	if (fd < 0) {
		int saved_errno = errno;
		mm_error(saved_errno, "socket()");
		errno = saved_errno;
		goto leave;
	}

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

	// Indicate that the socket connection is in progress.
	sock->flags |= MM_NET_CONNECTING;

	// Register the socket in the event loop.
	mm_event_prepare_fd(&sock->event, fd, mm_net_socket_handler,
			    MM_EVENT_ONESHOT, MM_EVENT_ONESHOT, MM_EVENT_BOUND);
	mm_event_register_fd(&sock->event);

	// Block the fiber waiting for connection completion.
	sock->writer = mm_fiber_selfptr();
	while ((sock->flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR)) == 0) {
		mm_fiber_block();
		// TODO: mm_fiber_testcancel();
	}
	sock->writer = NULL;

	// Check for EINPROGRESS connection outcome.
	if (rc == -1) {
		int conn_errno = 0;
		socklen_t len = sizeof(conn_errno);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &conn_errno, &len) < 0)
			mm_fatal(errno, "getsockopt(..., SO_ERROR, ...)");
		if (conn_errno == 0) {
			rc = 0;
		} else {
			mm_event_unregister_invalid_fd(&sock->event);
			sock->event.fd = -1;
			mm_close(fd);
			errno = conn_errno;
		}
	}

	// Indicate that the socket connection has completed.
	sock->flags &= ~MM_NET_CONNECTING;

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

static int
mm_net_wait_readable(struct mm_net_socket *sock, mm_timeval_t deadline)
{
	ENTER();
	int rc;

	// Check to see if the socket is closed.
	if (mm_net_is_reader_shutdown(sock)) {
		errno = EBADF;
		rc = -1;
		goto leave;
	}

	// Check to see if the socket is read ready.
	if ((sock->flags & (MM_NET_READ_READY | MM_NET_READ_ERROR)) != 0) {
		rc = 1;
		goto leave;
	}

	// Block the fiber waiting for the socket to become read ready.
	struct mm_strand *strand = mm_strand_selfptr();
	if (deadline == MM_TIMEVAL_MAX) {
		sock->reader = mm_fiber_selfptr();
		mm_fiber_block();
		sock->reader = NULL;
		rc = 0;
	} else if (mm_strand_gettime(strand) < deadline) {
		mm_timeout_t timeout = deadline - mm_strand_gettime(strand);
		sock->reader = mm_fiber_selfptr();
		mm_timer_block(timeout);
		sock->reader = NULL;
		rc = 0;
	} else {
		if (sock->read_timeout != 0)
			errno = ETIMEDOUT;
		else
			errno = EAGAIN;
		rc = -1;
		goto leave;
	}

	// Check if the fiber is canceled.
	mm_fiber_testcancel();

leave:
	LEAVE();
	return rc;
}

static int
mm_net_wait_writable(struct mm_net_socket *sock, mm_timeval_t deadline)
{
	ENTER();
	int rc;

	// Check to see if the socket is closed.
	if (mm_net_is_writer_shutdown(sock)) {
		errno = EBADF;
		rc = -1;
		goto leave;
	}

	// Check to see if the socket is write ready.
	if ((sock->flags & (MM_NET_WRITE_READY | MM_NET_WRITE_ERROR)) != 0) {
		rc = 1;
		goto leave;
	}

	// Block the fiber waiting for the socket to become write ready.
	struct mm_strand *strand = mm_strand_selfptr();
	if (deadline == MM_TIMEVAL_MAX) {
		sock->writer = mm_fiber_selfptr();
		mm_fiber_block();
		sock->writer = NULL;
		rc = 0;
	} else if (mm_strand_gettime(strand) < deadline) {
		mm_timeout_t timeout = deadline - mm_strand_gettime(strand);
		sock->writer = mm_fiber_selfptr();
		mm_timer_block(timeout);
		sock->writer = NULL;
		rc = 0;
	} else {
		if (sock->write_timeout != 0)
			errno = ETIMEDOUT;
		else
			errno = EAGAIN;
		rc = -1;
		goto leave;
	}

	// Check if the fiber is canceled.
	mm_fiber_testcancel();

leave:
	LEAVE();
	return rc;
}

ssize_t NONNULL(1, 2)
mm_net_read(struct mm_net_socket *sock, void *buffer, size_t nbytes)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE) {
		struct mm_strand *strand = mm_strand_selfptr();
		deadline = mm_strand_gettime(strand) + sock->read_timeout;
	}

retry:
	// Check to see if the socket is ready for reading.
	n = mm_net_wait_readable(sock, deadline);
	if (n <= 0) {
		if (n < 0)
			goto leave;
		else
			goto retry;
	}

	// Try to read (nonblocking).
	n = mm_read(sock->event.fd, buffer, nbytes);
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
			mm_error(saved_errno, "read()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_write(struct mm_net_socket *sock, const void *buffer, size_t nbytes)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	ssize_t n;

	// Remember the wait time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE) {
		struct mm_strand *strand = mm_strand_selfptr();
		deadline = mm_strand_gettime(strand) + sock->write_timeout;
	}

retry:
	// Check to see if the socket is ready for writing.
	n = mm_net_wait_writable(sock, deadline);
	if (n <= 0) {
		if (n < 0)
			goto leave;
		else
			goto retry;
	}

	// Try to write (nonblocking).
	n = mm_write(sock->event.fd, buffer, nbytes);
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
			mm_error(saved_errno, "write()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_readv(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt, ssize_t nbytes)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->read_timeout != MM_TIMEOUT_INFINITE) {
		struct mm_strand *strand = mm_strand_selfptr();
		deadline = mm_strand_gettime(strand) + sock->read_timeout;
	}

retry:
	// Check to see if the socket is ready for reading.
	n = mm_net_wait_readable(sock, deadline);
	if (n <= 0) {
		if (n < 0)
			goto leave;
		else
			goto retry;
	}

	// Try to read (nonblocking).
	n = mm_readv(sock->event.fd, iov, iovcnt);
	if (n > 0) {
		if (n < nbytes) {
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
			mm_error(saved_errno, "readv()");
			errno = saved_errno;
		}
	}

leave:
	DEBUG("n: %ld", (long) n);
	LEAVE();
	return n;
}

ssize_t NONNULL(1, 2)
mm_net_writev(struct mm_net_socket *sock, const struct iovec *iov, int iovcnt, ssize_t nbytes)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());
	ssize_t n;

	// Remember the start time.
	mm_timeval_t deadline = MM_TIMEVAL_MAX;
	if (sock->write_timeout != MM_TIMEOUT_INFINITE) {
		struct mm_strand *strand = mm_strand_selfptr();
		deadline = mm_strand_gettime(strand) + sock->write_timeout;
	}

retry:
	// Check to see if the socket is ready for writing.
	n = mm_net_wait_writable(sock, deadline);
	if (n <= 0) {
		if (n < 0)
			goto leave;
		else
			goto retry;
	}

	// Try to write (nonblocking).
	n = mm_writev(sock->event.fd, iov, iovcnt);
	if (n > 0) {
		if (n < nbytes) {
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
			mm_error(saved_errno, "writev()");
			errno = saved_errno;
		}
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
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_closed(sock))
		goto leave;

	// Mark the socket as closed.
	sock->flags |= MM_NET_CLOSED;

	// Remove the socket from the event loop.
	mm_event_unregister_fd(&sock->event);

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_reset(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_closed(sock))
		goto leave;

	// Mark the socket as closed.
	sock->flags |= MM_NET_CLOSED;

	struct linger lin = { .l_onoff = 1, .l_linger = 0 };
	if (setsockopt(sock->event.fd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin) < 0)
		mm_error(errno, "setsockopt(..., SO_LINGER, ...)");

	// Remove the socket from the event loop.
	mm_event_unregister_fd(&sock->event);

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_shutdown_reader(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_reader_shutdown(sock))
		goto leave;

	// Mark the socket as having the reader part closed.
	sock->flags |= MM_NET_READER_SHUTDOWN;

	if (mm_shutdown(sock->event.fd, SHUT_RD) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}

void NONNULL(1)
mm_net_shutdown_writer(struct mm_net_socket *sock)
{
	ENTER();
	ASSERT(mm_event_target(&sock->event) == mm_thread_self());

	if (mm_net_is_writer_shutdown(sock))
		goto leave;

	// Mark the socket as having the writer part closed.
	sock->flags |= MM_NET_WRITER_SHUTDOWN;

	if (mm_shutdown(sock->event.fd, SHUT_WR) < 0)
		mm_warning(errno, "shutdown");

leave:
	LEAVE();
}
