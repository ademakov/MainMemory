/*
 * memcache/state.c - MainMemory memcache connection state.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

#include "memcache/state.h"
#include "memcache/memcache.h"

#include "base/event/event.h"

extern struct mm_memcache_config mc_config;

struct mm_net_socket *
mc_state_create(void)
{
	ENTER();

	struct mc_state *state = mm_memory_xalloc(sizeof(struct mc_state));

	state->command_first = NULL;
	state->command_last = NULL;
	state->protocol = MC_PROTOCOL_INIT;
	state->error = false;
	state->trash = false;

	mm_netbuf_prepare(&state->sock, mc_config.rx_chunk_size, mc_config.tx_chunk_size);

	LEAVE();
	return &state->sock.sock;
}

void NONNULL(1)
mc_state_destroy(struct mm_event_fd *sink)
{
	ENTER();

	struct mc_state *state = containerof(sink, struct mc_state, sock.sock.event);

	mm_netbuf_cleanup(&state->sock);
	mm_memory_free(state);

	LEAVE();
}
