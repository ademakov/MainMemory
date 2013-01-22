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

/**********************************************************************
 * Protocol I/O Buffer.
 **********************************************************************/

#define MC_DEFAULT_BUFFER_SIZE	4000

struct mc_buffer
{
	struct mc_buffer *next;
	size_t size;
	size_t used;
	char data[];
};

static struct mc_buffer *
mc_buffer_create(size_t size)
{
	size_t total_size = sizeof(struct mc_buffer) + size;
	struct mc_buffer *buffer = mm_alloc(total_size);
	buffer->next = NULL;
	buffer->size = size;
	buffer->used = 0;
	return buffer;
}

static void
mc_buffer_destroy(struct mc_buffer *buffer)
{
	mm_free(buffer);
}

static inline bool
mc_buffer_contains(struct mc_buffer *buffer, char *ptr)
{
	return ptr >= buffer->data && ptr < (buffer->data + buffer->used);
}

/**********************************************************************
 * Whole Connection State.
 **********************************************************************/

struct mc_state
{
	struct mc_buffer *read_head;
	struct mc_buffer *read_tail;
	char *start_ptr;
};


static struct mc_state *
mc_create(void)
{
	ENTER();

	struct mc_state *state = mm_alloc(sizeof(struct mc_state));
	state->read_head = NULL;
	state->read_tail = NULL;
	state->start_ptr = NULL;

	LEAVE();
	return state;
}

static void
mc_destroy(struct mc_state *state)
{
	ENTER();

	while(state->read_head != NULL) {
		struct mc_buffer *buffer = state->read_head;
		state->read_head = buffer->next;
		mc_buffer_destroy(buffer);
	}

	mm_free(state);

	LEAVE();
}

static struct mc_buffer *
mc_add_read_buffer(struct mc_state *state, size_t size)
{
	ENTER();

	struct mc_buffer *buffer = mc_buffer_create(size);
	if (state->read_tail == NULL) {
		state->read_head = buffer;
		state->start_ptr = buffer->data;
	} else {
		state->read_tail->next = buffer;
	}
	state->read_tail = buffer;

	LEAVE();
	return buffer;
}

static void
mc_release_buffers(struct mc_state *state)
{
	while (state->read_head != NULL
	       && !mc_buffer_contains(state->read_head, state->start_ptr)) {
		struct mc_buffer *buffer = state->read_head;
		state->read_head = buffer->next;
		mc_buffer_destroy(buffer);
	}
}

/**********************************************************************
 * I/O routines.
 **********************************************************************/

static ssize_t
mc_read(struct mm_net_socket *sock, struct mc_state *state)
{
	ENTER();

	struct mc_buffer *buffer = state->read_tail;
	if (buffer == NULL || buffer->used == buffer->size)
		buffer = mc_add_read_buffer(state, MC_DEFAULT_BUFFER_SIZE);

	size_t nbytes = buffer->size - buffer->used;
	ssize_t n = mm_net_read(sock, buffer->data + buffer->used, nbytes);
	if (n > 0) {
		buffer->used += n;
	}

	LEAVE();
	return n;
}

/**********************************************************************
 * Command Processing.
 **********************************************************************/

static void
mc_process_command(struct mm_net_socket *sock, struct mc_state *state)
{
}

/**********************************************************************
 * Command Parsing.
 **********************************************************************/

#define MC_COMMAND_OK		0
#define MC_COMMAND_SHORT	(-1)
#define MC_COMMAND_ERROR	(-2)

#define MC_KEY_LEN_MAX		250

#define MC_BINARY_REQ		0x80
#define MC_BINARY_RES		0x81

static void
mc_carry_forward(struct mc_state *state, char *param, size_t param_len)
{
	struct mc_buffer *buffer = mc_add_read_buffer(state, MC_DEFAULT_BUFFER_SIZE);
	memcpy(buffer->data, param, param_len);
	memset(param, ' ', param_len);
	buffer->used = param_len;
	param = buffer->data;
}

static int
mc_parse_command(struct mc_state *state)
{
	ENTER();
	
	enum {
		PARSE_START,
		PARSE_CMD,
		PARSE_CMD_GE,
		PARSE_CMD_GET,
		PARSE_CMD_DE,
		PARSE_CMD_VE,
		PARSE_CMD_VER,
		PARSE_CMD_END,
		PARSE_PARAM,
		PARSE_WS,
		PARSE_CR,
		PARSE_ERROR,
	};

	int rc = MC_COMMAND_OK;
	int parse = PARSE_START;
	int start = -1;
	char *end = "";
	char *param;
	size_t param_len;

	struct mc_buffer *buffer = state->read_head;
	ASSERT(mc_buffer_contains(buffer, state->start_ptr));
	char *s = state->start_ptr;
	char *e = buffer->data + buffer->used;

	for (;;) {
		if (s == e) {
			if (buffer->next == NULL) {
				if (parse == PARSE_PARAM) {
					if (param_len > MC_KEY_LEN_MAX)
						goto error;
					mc_carry_forward(state, param, param_len);
				}

				rc = MC_COMMAND_SHORT;
				goto done;
			}

			buffer = buffer->next;
			s = buffer->data;
			e = buffer->data + buffer->used;
			continue;
		}

		char *p = s++;
		int c = *p;

		switch (parse) {
		case PARSE_START:
			if (c == ' ') {
				/* Skip spaces. */
				continue;
			} else if (unlikely(c == '\n')) {
				goto error;
			} else {
				start = c;
				parse = PARSE_CMD;
				continue;
			}

		case PARSE_CMD:
#define C(a, b) (((a) << 8) | (b))
			switch (C(start, c)) {
			case C('g', 'e'):
				parse = PARSE_CMD_GE;
				continue;
			case C('s', 'e'):
				parse = PARSE_CMD_END;
				end = "t";
				continue;
			case C('a', 'd'):
				parse = PARSE_CMD_END;
				end = "d";
				continue;
			case C('r', 'e'):
				parse = PARSE_CMD_END;
				end = "place";
				continue;
			case C('a', 'p'):
				parse = PARSE_CMD_END;
				end = "pend";
				continue;
			case C('p', 'r'):
				parse = PARSE_CMD_END;
				end = "epend";
				continue;
			case C('c', 'a'):
				parse = PARSE_CMD_END;
				end = "s";
				continue;
			case C('i', 'n'):
				parse = PARSE_CMD_END;
				end = "cr";
				continue;
			case C('d', 'e'):
				parse = PARSE_CMD_DE;
				continue;
			case C('t', 'o'):
				parse = PARSE_CMD_END;
				end = "uch";
				continue;
			case C('s', 'l'):
				parse = PARSE_CMD_END;
				end = "abs";
				continue;
			case C('s', 't'):
				parse = PARSE_CMD_END;
				end = "ats";
				continue;
			case C('f', 'l'):
				parse = PARSE_CMD_END;
				end = "ush_all";
				continue;
			case C('v', 'e'):
				parse = PARSE_CMD_DE;
				continue;
			case C('q', 'u'):
				parse = PARSE_CMD_END;
				end = "it";
				continue;
			}
#undef C
			break;

		case PARSE_CMD_GE:
			if (likely(c == 't')) {
				parse = PARSE_CMD_GET;
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_CMD_GET:
			if (likely(c == ' ')) {
				parse = PARSE_WS;
				continue;
			} else if (likely(c == 's')) {
				parse = PARSE_CMD_END;
				continue;
			} else if (unlikely(c == '\r')) {
				parse = PARSE_CR;
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_CMD_DE:
			if (likely(c == 'c')) {
				parse = PARSE_CMD_END;
				end = "r";
				continue;
			} else if (likely(c == 'l')) {
				parse = PARSE_CMD_END;
				end = "ete";
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_CMD_VE:
			if (likely(c == 'r')) {
				parse = PARSE_CMD_VER;
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_CMD_VER:
			if (likely(c == 's')) {
				parse = PARSE_CMD_END;
				end = "ion";
				continue;
			} else if (likely(c == 'b')) {
				parse = PARSE_CMD_END;
				end = "osity";
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_CMD_END:
			if (c == *end) {
				if (likely(c)) {
					/* So far so good. */
					end++;
				} else {
					/* Err, zero byte in the input. */
					parse = PARSE_ERROR;
				}
				continue;
			} else if (c == ' ') {
				/* End of the command. Verify it is complete. */
				parse = *end ? PARSE_ERROR : PARSE_WS;
				continue;
			} else if (c == '\r') {
				/* End of the command. Verify it is complete. */
				parse = *end ? PARSE_ERROR : PARSE_CR;
				continue;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_PARAM:
			if (c == ' ') {
				parse = PARSE_WS;
				continue;
			} else if (c == '\n') {
				/* Unexpected newline. */
				goto error;
			} else if (c == '\r') {
				parse = PARSE_CR;
				continue;
			} else {
				param_len++;
				continue;
			}
			break;

		case PARSE_WS:
			if (c == ' ') {
				/* Skip spaces. */
				continue;
			} else if (unlikely(c == '\n')) {
				/* Unexpected newline. */
				goto error;
			} else if (unlikely(c == '\r')) {
				parse = PARSE_CR;
				continue;
			} else {
				param = p;
				param_len = 1;
				parse = PARSE_PARAM;
				continue;
			}

		case PARSE_CR:
			if (likely(c == '\n')) {
				/* End of line. */
				goto done;
			} else {
				/* Unexpected char. */
				break;
			}

		case PARSE_ERROR:
			if (c == '\n') {
				/* End of erroneous line. */
				goto error;
			} else {
				/* Skip further. */
				continue;
			}
		}

		/* Came across unexpected char that was not handled above. */
		if (c == '\n') {
			/* Quit immediately if this is a newline. */
			goto error;
		}
		/* Otherwise skip input until a newline is met. */
		parse = PARSE_ERROR;
	}

error:
	rc = MC_COMMAND_ERROR;

done:
	state->start_ptr = s;

	LEAVE();
	return rc;
}

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		250

static bool
mc_receive_command(struct mm_net_socket *sock, struct mc_state *state)
{
	ENTER();

	bool ok = false;

	mm_net_set_nonblock(sock);
	ssize_t n = mc_read(sock, state);
	mm_net_clear_nonblock(sock);

	while (n > 0) {
		int rc = mc_parse_command(state);
		if (rc != MC_COMMAND_SHORT) {
			ok = (rc == MC_COMMAND_OK);
			break;
		}

		mm_net_set_timeout(sock, MC_READ_TIMEOUT);
		n = mc_read(sock, state);
		mm_net_set_timeout(sock, MM_TIMEOUT_INFINITE);
	}

	LEAVE();
	return ok;
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
	}

	if (mc_receive_command(sock, state)) {
		mc_process_command(sock, state);
		mc_release_buffers(state);
	}

	LEAVE();
}

/**********************************************************************
 * Module Entry Points.
 **********************************************************************/

/* TCP memcache server. */
static struct mm_net_server *mc_tcp_server;

void
mm_memcache_init(void)
{
	ENTER();

	static struct mm_net_proto proto = {
		.prepare = mc_prepare,
		.cleanup = mc_cleanup,
		.reader_routine = mc_process,
		.writer_routine = NULL,
	};

	mc_tcp_server = mm_net_create_inet_server("memcache", "127.0.0.1", 11211);
	mm_net_start_server(mc_tcp_server, &proto);

	LEAVE();
}

void
mm_memcache_term(void)
{
	ENTER();

	mm_net_stop_server(mc_tcp_server);

	LEAVE();
}
