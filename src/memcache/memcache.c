/*
 * memcache.c - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#include "memcache.h"

#include "../list.h"
#include "../net.h"
#include "../util.h"


#define MC_DEFAULT_BUFFER_SIZE	2000


struct mc_buffer
{
	struct mm_list buffers;
	size_t size;
	size_t used;
	char data[];
};

struct mc_state
{
	struct mm_list read_buffers;
	struct mm_list write_buffers;
};


static struct mm_net_server *mc_server;


static struct mc_state *
mc_create(void)
{
	ENTER();

	struct mc_state *state = mm_alloc(sizeof(struct mc_state));
	mm_list_init(&state->read_buffers);
	mm_list_init(&state->write_buffers);

	LEAVE();
	return state;
}

static void
mc_destroy(struct mc_state *state)
{
	ENTER();

	while(!mm_list_empty(&state->read_buffers)) {
		struct mm_list *link = mm_list_head(&state->read_buffers);
		struct mc_buffer *buffer = containerof(link, struct mc_buffer, buffers);
		mm_list_delete(link);
		mm_free(buffer);
	}

	while(!mm_list_empty(&state->write_buffers)) {
		struct mm_list *link = mm_list_head(&state->write_buffers);
		struct mc_buffer *buffer = containerof(link, struct mc_buffer, buffers);
		mm_list_delete(link);
		mm_free(buffer);
	}

	mm_free(state);

	LEAVE();
}

static struct mc_buffer *
mc_create_buffer(struct mc_state *state, size_t size)
{
	size_t total_size = size + sizeof(struct mc_buffer);
	struct mc_buffer *buffer = mm_alloc(total_size);
	buffer->size = size;
	buffer->used = 0;
	return buffer;
}

static void
mc_add_read_buffer(struct mc_state *state, size_t size)
{
	ENTER();

	struct mc_buffer *buffer = mc_create_buffer(state, size);
	mm_list_append(&state->read_buffers, &buffer->buffers);

	LEAVE();
}

static void
mc_add_write_buffer(struct mc_state *state, size_t size)
{
	ENTER();

	struct mc_buffer *buffer = mc_create_buffer(state, size);
	mm_list_append(&state->write_buffers, &buffer->buffers);

	LEAVE();
}

static ssize_t
mc_read(struct mm_net_socket *sock, struct mc_state *state)
{
	struct mm_list *link = mm_list_tail(&state->read_buffers);
	struct mc_buffer *buffer = containerof(link, struct mc_buffer, buffers);

	size_t nbytes = buffer->size - buffer->used;
	ASSERT(nbytes > 0);

	ssize_t n = mm_net_read(sock, buffer->data + buffer->used, nbytes);
	return n;
}

static bool
mc_receive_command(struct mm_net_socket *sock, struct mc_state *state)
{
}

static void
mc_execute_command(struct mm_net_socket *sock, struct mc_state *state)
{
}

static void
mc_prepare(struct mm_net_socket *sock)
{
	ENTER();

	sock->proto_data = 0;

	LEAVE();
}

static void
mc_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	if (sock->proto_data) {
		mc_destroy((struct mc_state *) sock->proto_data);
		sock->proto_data = 0;
	}

	LEAVE();
}

static void
mc_process(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state;
	if (sock->proto_data) {
		state = (struct mc_state *) sock->proto_data;
	} else {
		state = mc_create();
		sock->proto_data = (intptr_t) state;
		mc_add_read_buffer(state, MC_DEFAULT_BUFFER_SIZE);
	}

	if (mc_receive_command(sock, state))
		mc_execute_command(sock, state);

	LEAVE();
}

void
mm_memcache_init(void)
{
	static struct mm_net_proto proto = {
		.prepare = mc_prepare,
		.cleanup = mc_cleanup,
		.reader_routine = mc_process,
		.writer_routine = NULL,
	};

	mc_server = mm_net_create_inet_server("memcache", "127.0.0.1", 11211);
	mm_net_start_server(mc_server, &proto);
}

void
mm_memcache_term(void)
{
	mm_net_stop_server(mc_server);
}
