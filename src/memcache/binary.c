/*
 * memcache/binary.h - MainMemory memcache binary protocol support.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "memcache/binary.h"
#include "memcache/command.h"
#include "memcache/parser.h"
#include "memcache/state.h"

#include "base/log/trace.h"

#include <arpa/inet.h>

struct mc_binary_header
{
	uint8_t magic;
	uint8_t opcode;
	uint16_t keylen;
	uint8_t extlen;
	uint8_t datatype;
	uint16_t status;
	uint32_t bodylen;
	uint32_t opaque;
	uint64_t cas;
};

bool __attribute__((nonnull(1)))
mc_binary_parse(struct mc_parser *parser)
{
	ENTER();
	bool rc = true;

	size_t size = mm_slider_getsize_used(&parser->cursor);
	if (size < sizeof(struct mc_binary_header)) {
		rc = false;
		goto leave;
	}

	struct mc_binary_header header;
	mm_slider_read(&parser->cursor, &header, sizeof header);
	if (header.magic != MC_BINARY_REQUEST) {
		parser->state->trash = true;
		rc = false;
		goto leave;
	}

	uint32_t bodylen = ntohl(header.bodylen);
	//uint16_t keylen = ntohs(header.keylen);
	//uint8_t extlen = header.extlen;

	uint32_t available = mm_slider_getsize_used(&parser->cursor);
	if (bodylen > available) {
		mm_netbuf_demand(&parser->state->sock, bodylen - available);
		do {
			ssize_t n = mm_netbuf_fill(&parser->state->sock);
			if (n <= 0) {
				if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
					parser->state->error = true;
				rc = false;
				goto leave;
			}
			available += n;
		} while (bodylen > available);
		mm_slider_reset_used(&parser->cursor);
	}

	// The current command.
	struct mc_command *command = mc_command_create(mm_core_selfid());
	parser->command = command;

	command->params.binary.opaque = header.opaque;
	command->params.binary.opcode = header.opcode;

	switch (header.opcode) {
	case MC_BINARY_OPCODE_QUIT:
		mm_net_shutdown_reader(&parser->state->sock.sock);
		parser->command->result = MC_RESULT_BINARY_QUIT;
		break;

	case MC_BINARY_OPCODE_NOOP:
		parser->command->result = MC_RESULT_BINARY_NOOP;
		break;

	case MC_BINARY_OPCODE_QUITQ:
		mm_net_shutdown_reader(&parser->state->sock.sock);
		parser->command->result = MC_RESULT_QUIT;
		break;

	default:
		parser->command->result = MC_RESULT_BINARY_UNKNOWN;
		break;
	}

leave:
	LEAVE();
	return rc;
}

void __attribute__((nonnull(1, 2)))
mc_binary_status(struct mc_state *state,
		 struct mc_command *command,
		 uint16_t status)
{
	ENTER();

	struct mc_binary_header header;
	header.magic = MC_BINARY_RESPONSE;
	header.status = status;
	header.opcode = command->params.binary.opcode;
	header.opaque = command->params.binary.opaque;
	header.keylen = 0;
	header.extlen = 0;
	header.datatype = 0;
	header.bodylen = 0;
	header.cas = 0;

	mm_netbuf_write(&state->sock, &header, sizeof header);

	LEAVE();
}
