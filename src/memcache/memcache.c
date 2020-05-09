/*
 * memcache/memcache.c - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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

#include "base/bitops.h"
#include "base/list.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/fiber/fiber.h"
#include "base/fiber/future.h"

struct mm_memcache_config mc_config;

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		10000

static void
mc_process_command(struct mc_state *state, struct mc_command_base *command)
{
	ENTER();

	do {
		struct mc_command_base *next = command->next;
		mc_command_execute(state, command);
		mc_command_cleanup(command);
		command = next;

	} while (command != NULL);

	LEAVE();
}

static mm_value_t
mc_reader_routine(mm_value_t arg)
{
	ENTER();

	struct mm_net_socket *const sock = mm_net_arg_to_socket(arg);
	struct mc_state *const state = containerof(sock, struct mc_state, sock.sock);
	state->command_first = NULL;
	state->command_last = NULL;

	if (mm_netbuf_empty(&state->sock)) {
		// Try to get some input w/o blocking.
		mm_net_set_read_timeout(sock, 0);
		ssize_t n = mm_netbuf_fill(&state->sock, 1);
		// Get out of here if the connection is broken or there is no input yet.
		if (n <= 0) {
			if (n == 0 || errno != EAGAIN)
				mm_netbuf_close(&state->sock);
			goto leave;
		}
		mm_net_set_read_timeout(sock, MC_READ_TIMEOUT);
	}

	// Prepare for parsing.
	struct mm_buffer_reader safepoint;
	mm_netbuf_capture_read_pos(&state->sock, &safepoint);
	const mc_protocol_t proto = mc_getprotocol(state);
	size_t batch_size = mc_config.batch_size;
	struct mc_command_base *command_last;

	// Try to parse the received input.
parse:
	command_last = state->command_last;
	if (!(proto == MC_PROTOCOL_BINARY ? mc_binary_parse(state) : mc_parser_parse(state))) {
		// If the parser created a new command then clean it up.
		if (state->command_last != command_last) {
			mc_command_cleanup(state->command_last);
			if (command_last != NULL) {
				command_last->next = NULL;
				state->command_last = command_last;
			} else {
				state->command_first = NULL;
				state->command_last = NULL;
			}
		}

		// If the input is beyond repair then silently drop the connection.
		if (state->trash) {
			mm_netbuf_reset(&state->sock);
			mm_warning(0, "disconnect an odd client");
			goto leave;
		}

		// The input is incomplete, try to get some more.
		if (!state->error) {
			mm_netbuf_restore_read_pos(&state->sock, &safepoint);
			ssize_t n = mm_netbuf_fill(&state->sock, 1);
			if (n == 0 || (n < 0 && errno != ETIMEDOUT)) {
				state->error = true;
			} else {
				goto parse;
			}
		}
	}

	if (state->error) {
		// The last parsed command was incomplete because of the broken connection.
		// But there may be already a few good ones parsed before. So add a quiet
		// quit command now. Here there is no difference between ascii and binary
		// protocols.
		mc_command_create_simple(state, &mc_command_ascii_quit);
	} else if (!mm_netbuf_empty(&state->sock)) {
		// If there is more input in the buffer then try to parse the next command.
		if (--batch_size) {
			// Update the safe consumed input position.
			mm_netbuf_capture_read_pos(&state->sock, &safepoint);
			goto parse;
		} else {
			// Set up to resume after other queued tasks are handled.
			mm_net_submit_input(sock);
		}
	}

	// Process the parsed commands.
	mc_process_command(state, state->command_first);

	// Transmit buffered results and compact the output buffer storage.
	mm_netbuf_flush(&state->sock);
	mm_netbuf_compact_write_buf(&state->sock);

	// Compact the input buffer storage.
	mm_netbuf_compact_read_buf(&state->sock);

leave:
	LEAVE();
	return 0;
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

	mc_table_start(&mc_config);

	LEAVE();
}

static void
mc_memcache_stop(void)
{
	ENTER();

	mc_table_stop();

	LEAVE();
}

void
mm_memcache_init(const struct mm_memcache_config *config)
{
	ENTER();

	static struct mm_net_proto proto = {
		.options = MM_NET_NODELAY | MM_NET_KEEPALIVE,
		.create = mc_state_create,
		.destroy = mc_state_destroy,
		.reader = mc_reader_routine,
	};

	const char *addr = "127.0.0.1";
	if (config != NULL && config->addr != NULL)
		addr = config->addr;

	uint16_t port = 11211;
	if (config != NULL && config->port != 0)
		port = config->port;

	mc_tcp_server = mm_net_create_inet_server("memcache", &proto, addr, port);
	mm_net_setup_server(mc_tcp_server);

	mm_regular_start_hook_0(mc_memcache_start);
	mm_regular_stop_hook_0(mc_memcache_stop);

	// Determine the maximal data size in memcache table.
	if (config != NULL && config->volume)
		mc_config.volume = config->volume;
	else
		mc_config.volume = MC_TABLE_VOLUME_DEFAULT;

	if (config != NULL)
		mc_config.batch_size = config->batch_size;

	uint32_t rx_chunk_size = 0, tx_chunk_size = 0;
	if (config != NULL) {
		rx_chunk_size = config->rx_chunk_size;
		tx_chunk_size = config->tx_chunk_size;
	}
	// The ascii parser wants 1024 bytes of look-ahead space for each
	// command. Make the initial size more than that to parse a series
	// of pipelined short commands without buffer reallocation.
	if (rx_chunk_size < 2000)
		rx_chunk_size = 2000;
	mc_config.rx_chunk_size = rx_chunk_size;
	mc_config.tx_chunk_size = tx_chunk_size;

	// Determine the required memcache table partitions.
#if ENABLE_MEMCACHE_DELEGATE
	size_t nbits = 1;
	if (config != NULL) {
		nbits = mm_bitset_size(&config->affinity);
		if (nbits == 0)
			nbits = 1;
	}
	mm_bitset_prepare(&mc_config.affinity, &mm_common_space.xarena, nbits);
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
