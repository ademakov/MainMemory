/*
 * memcache/memcache.c - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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
#include "memcache/binary.h"
#include "memcache/command.h"
#include "memcache/entry.h"
#include "memcache/parser.h"
#include "memcache/state.h"
#include "memcache/table.h"

#include "core/core.h"
#include "core/future.h"

#include "base/bitops.h"
#include "base/list.h"
#include "base/log/error.h"
#include "base/log/trace.h"
#include "base/memory/chunk.h"
#include "base/memory/pool.h"

struct mm_memcache_config mc_config;

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		10000

static void
mc_process_command(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	mm_thread_t thread = mm_netbuf_thread(&state->sock);

	do {
		// Handle the command if it has associated
		// execution routine.
		mc_command_execute(state, command);

		// Release the command data
		struct mc_command *next = command->next;
		mc_command_destroy(thread, command);
		command = next;

	} while (command != NULL);

	// Transmit buffered results.
	ssize_t n = mm_netbuf_flush(&state->sock);
	if (n > 0)
		mm_netbuf_write_reset(&state->sock);

	LEAVE();
}

static void
mc_reader_routine(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	// Try to get some input w/o blocking.
	mm_net_set_read_timeout(&state->sock.sock, 0);
	mm_netbuf_demand(&state->sock, 1);
	ssize_t n = mm_netbuf_fill(&state->sock);
	mm_net_set_read_timeout(&state->sock.sock, MC_READ_TIMEOUT);

retry:
	// Get out of here if there is no more input available.
	if (n <= 0) {
		if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
			state->error = true;

		// If the socket is closed queue a quit command.
		if (state->error && !mm_net_is_reader_shutdown(sock)) {
			mm_core_t core = mm_event_target(&sock->event);
			struct mc_command *command = mc_command_create(core);
			command->type = &mc_command_ascii_quit;
			mc_process_command(state, command);
		}
		goto leave;
	}

	// Initialize the parser.
	struct mc_parser parser;
	mc_parser_start(&parser, state);

	// Try to parse the received input.
	bool rc;
parse:
	if (state->protocol == MC_PROTOCOL_BINARY)
		rc = mc_binary_parse(&parser);
	else
		rc = mc_parser_parse(&parser);

	if (!rc) {
		if (parser.command != NULL) {
			mm_core_t core = mm_event_target(&sock->event);
			mc_command_destroy(core, parser.command);
			parser.command = NULL;
		}
		if (state->trash) {
			mm_netbuf_close(&state->sock);
			mm_warning(0, "disconnect odd client");
			goto leave;
		}

		// The input is incomplete, try to get some more.
		mm_netbuf_demand(&state->sock, 1);
		n = mm_netbuf_fill(&state->sock);
		goto retry;
	}

	// Process the parsed command.
	mc_process_command(state, parser.command);

	// Mark the parsed input as consumed.
	mm_slider_flush_used(&parser.cursor);

	// If there is more input in the buffer then try to parse
	// the next command.
	if (!mm_slider_empty(&parser.cursor)) {
		parser.command = NULL;
		goto parse;
	}

	// Reset the buffer state.
	mm_netbuf_read_reset(&state->sock);

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
	mc_action_start();
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
	mc_action_stop();
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
		.attach = mc_state_attach,
		.detach = mc_state_detach,
		.reader = mc_reader_routine,
	};

	mc_tcp_server = mm_net_create_inet_server("memcache", &proto,
						  "0.0.0.0", 11211);

	mm_core_hook_start(mc_memcache_start);
	mm_core_hook_stop(mc_memcache_stop);

	// Determine the maximal data size in memcache table.
	if (config != NULL && config->volume)
		mc_config.volume = config->volume;
	else
		mc_config.volume = MC_TABLE_VOLUME_DEFAULT;

	// Determine the required memcache table partitions.
#if ENABLE_MEMCACHE_DELEGATE
	mm_bitset_prepare(&mc_config.affinity, &mm_common_space.arena, mm_core_getnum());
	if (config != NULL)
		mm_bitset_or(&mc_config.affinity, &config->affinity);
	if (!mm_bitset_any(&mc_config.affinity))
		mm_bitset_set(&mc_config.affinity, 0);
#else
	if (config != NULL && config->nparts)
		mc_config.nparts = config->nparts;
	else
		mc_config.nparts = 1;
#endif

	LEAVE();
}
