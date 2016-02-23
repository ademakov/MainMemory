/*
 * memcache/state.h - MainMemory memcache connection state.
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

#ifndef MEMCACHE_STATE_H
#define MEMCACHE_STATE_H

#include "memcache/command.h"
#include "memcache/binary.h"

#include "base/log/debug.h"
#include "net/netbuf.h"

typedef enum {
	MC_PROTOCOL_INIT = 0,
	MC_PROTOCOL_ASCII = 1,
	MC_PROTOCOL_BINARY = 2,
} mc_protocol_t;

struct mc_state
{
	/* The client socket. */
	struct mm_netbuf_socket sock;

	/* Currently constructed command. */
	struct mc_command *command;

	/* Command processing queue. */
	struct mc_command *command_head;
	struct mc_command *command_tail;

	/* Memcache protocol. */
	uint8_t protocol;

	/* Flags. */
	bool error;
	bool trash;
};

/**********************************************************************
 * Net protocol routines.
 **********************************************************************/

struct mm_net_socket *
mc_state_create(void);

void NONNULL(1)
mc_state_reclaim(struct mm_net_socket *sock);

void NONNULL(1)
mc_state_destroy(struct mm_net_socket *sock);

bool NONNULL(1)
mc_state_detach(struct mm_net_socket *sock);

/**********************************************************************
 * Command support.
 **********************************************************************/

static inline mc_protocol_t NONNULL(1)
mc_getprotocol(struct mc_state *state)
{
	mc_protocol_t protocol = state->protocol;
	if (protocol == MC_PROTOCOL_INIT) {
		ASSERT(mm_netbuf_rptr(&state->sock) < mm_netbuf_rend(&state->sock));
		uint8_t *p = (uint8_t *) mm_netbuf_rptr(&state->sock);
		if (*p == MC_BINARY_REQUEST) {
			DEBUG("binary protocol detected");
			protocol = MC_PROTOCOL_BINARY;
		} else {
			DEBUG("ASCII protocol detected");
			protocol = MC_PROTOCOL_ASCII;
		}
		state->protocol = protocol;
	}
	return protocol;
}

static inline void NONNULL(1, 2, 3)
mc_queue_command(struct mc_state *state,
		 struct mc_command *first,
		 struct mc_command *last)
{
	if (state->command_head == NULL) {
		state->command_head = first;
	} else {
		state->command_tail->next = first;
	}
	state->command_tail = last;
}

#endif /* MEMCACHE_STATE_H */
