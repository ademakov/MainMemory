/*
 * memcache/state.c - MainMemory memcache connection state.
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

#include "memcache/state.h"

#include "event/event.h"

struct mm_net_socket *
mc_state_alloc(void)
{
	ENTER();

	struct mc_state *state = mm_regular_alloc(sizeof(struct mc_state));

	LEAVE();
	return &state->sock.sock;
}

void
mc_state_free(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	mm_regular_free(state);

	LEAVE();
}

void
mc_state_prepare(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);
	state->command_head = NULL;
	state->command_tail = NULL;
	state->protocol = MC_PROTOCOL_INIT;
	state->error = false;
	state->trash = false;

	//mm_net_set_async_read(sock, true);
	//mm_net_set_async_write(sock, true);

	LEAVE();
}

void
mc_state_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	mm_core_t core = mm_event_target(&sock->event);
	while (state->command_head != NULL) {
		struct mc_command *command = state->command_head;
		state->command_head = command->next;
		mc_command_destroy(core, command);
	}

	if (mm_event_attached(&state->sock.sock.event)) {
		mm_netbuf_cleanup(&state->sock);
	}

	LEAVE();
}

bool
mc_state_finish(struct mm_net_socket *sock)
{
	struct mc_state *state = containerof(sock, struct mc_state, sock);
	return state->command_head == NULL;
}

void
mc_state_attach(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);
	mm_netbuf_prepare(&state->sock);

	LEAVE();
}

void
mc_state_detach(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);
	mm_netbuf_cleanup(&state->sock);

	LEAVE();
}
