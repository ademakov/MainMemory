/*
 * memcache/memcache.c - MainMemory memcached protocol support.
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

#include "memcache/memcache.h"

#include "memcache/command.h"
#include "memcache/entry.h"
#include "memcache/parser.h"
#include "memcache/state.h"
#include "memcache/table.h"

#include "alloc.h"
#include "bitops.h"
#include "chunk.h"
#include "core.h"
#include "future.h"
#include "list.h"
#include "log.h"
#include "pool.h"
#include "trace.h"

#include <stdio.h>
#include <stdlib.h>

#define MC_VERSION	"VERSION " PACKAGE_STRING "\r\n"

struct mm_memcache_config mc_config;

/**********************************************************************
 * Transmitting command results.
 **********************************************************************/

static void
mc_transmit_unref(uintptr_t data)
{
	ENTER();

	struct mc_entry *entry = (struct mc_entry *) data;
	mc_table_unref_entry(mc_table_part(entry->hash), entry);

	LEAVE();
}

static void
mc_transmit(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	switch (mc_command_result(command)) {

#define SL(x) x, (sizeof (x) - 1)

	case MC_RESULT_BLANK:
		break;

	case MC_RESULT_OK:
		mm_netbuf_append(&state->sock, SL("OK\r\n"));
		break;

	case MC_RESULT_END:
		mm_netbuf_append(&state->sock, SL("END\r\n"));
		break;

	case MC_RESULT_ERROR:
		mm_netbuf_append(&state->sock, SL("ERROR\r\n"));
		break;

	case MC_RESULT_EXISTS:
		mm_netbuf_append(&state->sock, SL("EXISTS\r\n"));
		break;

	case MC_RESULT_STORED:
		mm_netbuf_append(&state->sock, SL("STORED\r\n"));
		break;

	case MC_RESULT_DELETED:
		mm_netbuf_append(&state->sock, SL("DELETED\r\n"));
		break;

	case MC_RESULT_TOUCHED:
		mm_netbuf_append(&state->sock, SL("TOUCHED\r\n"));
		break;

	case MC_RESULT_NOT_FOUND:
		mm_netbuf_append(&state->sock, SL("NOT_FOUND\r\n"));
		break;

	case MC_RESULT_NOT_STORED:
		mm_netbuf_append(&state->sock, SL("NOT_STORED\r\n"));
		break;

	case MC_RESULT_INC_DEC_NON_NUM:
		mm_netbuf_append(&state->sock, SL("CLIENT_ERROR cannot increment or decrement non-numeric value\r\n"));
		break;

	case MC_RESULT_NOT_IMPLEMENTED:
		mm_netbuf_append(&state->sock, SL("SERVER_ERROR not implemented\r\n"));
		break;

	case MC_RESULT_CANCELED:
		mm_netbuf_append(&state->sock, SL("SERVER_ERROR command canceled\r\n"));
		break;

	case MC_RESULT_VERSION:
		mm_netbuf_append(&state->sock, SL(MC_VERSION));
		break;

#undef SL

	case MC_RESULT_ENTRY:
	case MC_RESULT_ENTRY_CAS: {
		struct mc_entry *entry = command->entry;
		const char *key = mc_entry_getkey(entry);
		char *value = mc_entry_getvalue(entry);
		uint8_t key_len = entry->key_len;
		uint32_t value_len = entry->value_len;

		if (command->result == MC_RESULT_ENTRY) {
			mm_netbuf_printf(
				&state->sock,
				"VALUE %.*s %u %u\r\n",
				key_len, key,
				entry->flags, value_len);
		} else {
			mm_netbuf_printf(
				&state->sock,
				"VALUE %.*s %u %u %llu\r\n",
				key_len, key,
				entry->flags, value_len,
				(unsigned long long) entry->cas);
		}

		mm_netbuf_splice(&state->sock, value, value_len,
				 mc_transmit_unref, (uintptr_t) entry);

		// Prevent extra entry unref on command destruction.
		command->result = MC_RESULT_BLANK;

		if (command->params.last)
			mm_netbuf_append(&state->sock, "\r\nEND\r\n", 7);
		else
			mm_netbuf_append(&state->sock, "\r\n", 2);
		break;
	}

	case MC_RESULT_VALUE: {
		struct mc_entry *entry = command->entry;
		char *value = mc_entry_getvalue(entry);
		uint32_t value_len = entry->value_len;

		mm_netbuf_splice(&state->sock, value, value_len,
				 mc_transmit_unref, (uintptr_t) entry);

		// Prevent extra entry unref on command destruction.
		command->result = MC_RESULT_BLANK;

		mm_netbuf_append(&state->sock, "END\r\n", 5);
		break;
	}

	case MC_RESULT_QUIT:
		mm_netbuf_close(&state->sock);
		break;

	default:
		ABORT();
	}

	LEAVE();
}

static void
mc_transmit_flush(struct mc_state *state)
{
	ENTER();

	ssize_t n = mm_netbuf_write(&state->sock);
	if (n > 0)
		mm_netbuf_write_reset(&state->sock);

	LEAVE();
}

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		10000

static mm_value_t
mc_process_command(struct mc_state *state, struct mc_command *first)
{
	ENTER();
 
	struct mc_command *last = first;
	if (likely(first->type != NULL)) {
		DEBUG("command %s", mc_command_name(first->type->tag));
		for (;;) {
			mc_command_execute(last);
			if (last->next == NULL)
				break;
			last = last->next;
		}
	}

	mc_queue_command(state, first, last);
	mm_net_spawn_writer(&state->sock.sock);

	LEAVE();
	return 0;
}

static void
mc_release_buffers(struct mc_state *state, char *ptr)
{
	ENTER();

	size_t size = 0;

	struct mm_buffer_cursor cur;
	bool rc = mm_netbuf_read_first(&state->sock, &cur);
	while (rc) {
		if (ptr >= cur.ptr && ptr <= cur.end) {
			// The buffer is (might be) still in use.
			if (ptr == cur.end && state->start_ptr == cur.end)
				state->start_ptr = NULL;
			size += ptr - cur.ptr;
			break;
		}

		size += cur.end - cur.ptr;
		rc = mm_netbuf_read_next(&state->sock, &cur);
	}

	if (size > 0)
		mm_netbuf_reduce(&state->sock, size);

	LEAVE();
}

static void
mc_reader_routine(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	// Reset the buffer state.
	if (mm_netbuf_read_empty(&state->sock)) {
		mm_netbuf_read_reset(&state->sock);
		state->start_ptr = NULL;
	}

	// Try to get some input w/o blocking.
	mm_net_set_read_timeout(&state->sock.sock, 0);
	mm_netbuf_demand(&state->sock, 1);
	ssize_t n = mm_netbuf_read(&state->sock);
	mm_net_set_read_timeout(&state->sock.sock, MC_READ_TIMEOUT);

retry:
	// Get out of here if there is no more input available.
	if (n <= 0) {
		if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
			state->error = true;

		// If the socket is closed queue a quit command.
		if (state->error && !mm_net_is_reader_shutdown(sock)) {
			struct mc_command *command = mc_command_create(sock->core);
			command->type = &mc_desc_quit;
			command->params.sock = sock;
			command->end_ptr = state->start_ptr;
			mc_process_command(state, command);
		}
		goto leave;
	}

	// Initialize the parser.
	struct mc_parser parser;
	mc_parser_start(&parser, state);

parse:
	// Try to parse the received input.
	if (!mc_parser_parse(&parser)) {
		if (parser.command != NULL) {
			mc_command_destroy(sock->core, parser.command);
			parser.command = NULL;
		}
		if (state->trash) {
			mm_netbuf_close(&state->sock);
			goto leave;
		}

		// The input is incomplete, try to get some more.
		mm_netbuf_demand(&state->sock, 1);
		n = mm_netbuf_read(&state->sock);
		goto retry;
	}

	// Mark the parsed input as consumed.
	state->start_ptr = parser.cursor.ptr;

	// Process the parsed command.
	mc_process_command(state, parser.command);

	// If there is more input in the buffer then try to parse the next
	// command.
	if (!mm_netbuf_read_end(&state->sock, &parser.cursor))
		goto parse;

leave:
	LEAVE();
}

static void
mc_writer_routine(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	// Check to see if there at least one ready result.
	struct mc_command *command = state->command_head;
	if (unlikely(command == NULL))
		goto leave;

	// Put the results into the transmit buffer.
	for (;;) {
		mc_transmit(state, command);

		struct mc_command *next = command->next;
		if (next == NULL)
			break;

		command = next;
	}

	// Transmit buffered results.
	mc_transmit_flush(state);

	// Free the receive buffers.
	mc_release_buffers(state, command->end_ptr);

	// Release the command data
	for (;;) {
		struct mc_command *head = state->command_head;
		state->command_head = head->next;
		if (state->command_head == NULL)
			state->command_tail = NULL;

		mc_command_destroy(sock->core, head);

		if (head == command) {
			break;
		}
	}

leave:
	LEAVE();
}

/**********************************************************************
 * Module Entry Points.
 **********************************************************************/

// TCP memcache server.
static struct mm_net_server *mc_tcp_server;

static void
mc_memcache_start(void)
{
	ENTER();

	mc_table_init(&mc_config);
	mc_command_start();
	mm_net_start_server(mc_tcp_server);

	LEAVE();
}

static void
mc_memcache_stop(void)
{
	ENTER();

	mm_net_stop_server(mc_tcp_server);
	mc_command_stop();
	mc_table_term();

	LEAVE();
}

void
mm_memcache_init(const struct mm_memcache_config *config)
{
	ENTER();

	static struct mm_net_proto proto = {
		.flags = MM_NET_INBOUND,
		.alloc = mc_state_alloc,
		.free = mc_state_free,
		.prepare = mc_state_prepare,
		.cleanup = mc_state_cleanup,
		.reader = mc_reader_routine,
		.writer = mc_writer_routine,
	};

	mc_tcp_server = mm_net_create_inet_server("memcache", &proto,
						  "127.0.0.1", 11211);

	mm_core_hook_start(mc_memcache_start);
	mm_core_hook_stop(mc_memcache_stop);

	// Determine the maximal data size in memcache table.
	if (config != NULL && config->volume)
		mc_config.volume = config->volume;
	else
		mc_config.volume = MC_TABLE_VOLUME_DEFAULT;

	// Determine the required memcache table partitions.
#if ENABLE_MEMCACHE_LOCKS
	if (config != NULL && config->nparts)
		mc_config.nparts = config->nparts;
	else
		mc_config.nparts = 1;
#else
	mm_bitset_prepare(&mc_config.affinity, &mm_alloc_global, mm_core_getnum());
	if (config != NULL)
		mm_bitset_or(&mc_config.affinity, &config->affinity);
	if (!mm_bitset_any(&mc_config.affinity))
		mm_bitset_set(&mc_config.affinity, 0);
#endif

	LEAVE();
}
