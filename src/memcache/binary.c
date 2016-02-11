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

#include "base/bytes.h"
#include "base/log/trace.h"

static struct mc_command_type *mc_binary_commands[256] = {
	[MC_BINARY_OPCODE_GET]		= &mc_command_binary_get,
	[MC_BINARY_OPCODE_GETQ]		= &mc_command_binary_getq,
	[MC_BINARY_OPCODE_GETK]		= &mc_command_binary_getk,
	[MC_BINARY_OPCODE_GETKQ]	= &mc_command_binary_getkq,
	[MC_BINARY_OPCODE_SET]		= &mc_command_binary_set,
	[MC_BINARY_OPCODE_SETQ]		= &mc_command_binary_setq,
	[MC_BINARY_OPCODE_ADD]		= &mc_command_binary_add,
	[MC_BINARY_OPCODE_ADDQ]		= &mc_command_binary_addq,
	[MC_BINARY_OPCODE_REPLACE]	= &mc_command_binary_replace,
	[MC_BINARY_OPCODE_REPLACEQ]	= &mc_command_binary_replaceq,
	[MC_BINARY_OPCODE_APPEND]	= &mc_command_binary_append,
	[MC_BINARY_OPCODE_APPENDQ]	= &mc_command_binary_appendq,
	[MC_BINARY_OPCODE_PREPEND]	= &mc_command_binary_prepend,
	[MC_BINARY_OPCODE_PREPENDQ]	= &mc_command_binary_prependq,
	[MC_BINARY_OPCODE_INCREMENT]	= &mc_command_binary_increment,
	[MC_BINARY_OPCODE_INCREMENTQ]	= &mc_command_binary_incrementq,
	[MC_BINARY_OPCODE_DECREMENT]	= &mc_command_binary_decrement,
	[MC_BINARY_OPCODE_DECREMENTQ]	= &mc_command_binary_decrementq,
	[MC_BINARY_OPCODE_DELETE]	= &mc_command_binary_delete,
	[MC_BINARY_OPCODE_DELETEQ]	= &mc_command_binary_deleteq,
	[MC_BINARY_OPCODE_NOOP]		= &mc_command_binary_noop,
	[MC_BINARY_OPCODE_QUIT]		= &mc_command_binary_quit,
	[MC_BINARY_OPCODE_QUITQ]	= &mc_command_binary_quitq,
	[MC_BINARY_OPCODE_FLUSH]	= &mc_command_binary_flush,
	[MC_BINARY_OPCODE_FLUSHQ]	= &mc_command_binary_flushq,
	[MC_BINARY_OPCODE_VERSION]	= &mc_command_binary_version,
	[MC_BINARY_OPCODE_STAT]		= &mc_command_binary_stat,
};

static bool
mc_binary_fill(struct mc_parser *parser, uint32_t required)
{
	uint32_t available = mm_netbuf_getleft(&parser->state->sock);
	while (required > available) {
		ssize_t n = mm_netbuf_fill(&parser->state->sock, required - available);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				parser->state->error = true;
			return false;
		}
		available += n;
	}
	return true;
}

static bool
mc_binary_skip(struct mc_parser *parser, uint32_t required)
{
	for (;;) {
		required -= mm_netbuf_reduce(&parser->state->sock, required);
		if (required == 0)
			break;

		ssize_t n = mm_netbuf_fill(&parser->state->sock, required);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				parser->state->error = true;
			return false;
		}
	}
	return true;
}

static bool
mc_binary_error(struct mc_parser *parser, uint32_t body_len, uint16_t status)
{
	if (!mc_binary_skip(parser, body_len))
		return false;
	parser->command->type = &mc_command_binary_error;
	parser->command->value = status;
	return true;
}

static bool
mc_binary_unknown_command(struct mc_parser *parser, uint32_t body_len)
{
	return mc_binary_error(parser, body_len, MC_BINARY_STATUS_UNKNOWN_COMMAND);
}

static bool
mc_binary_invalid_arguments(struct mc_parser *parser, uint32_t body_len)
{
	return mc_binary_error(parser, body_len, MC_BINARY_STATUS_INVALID_ARGUMENTS);
}

static void
mc_binary_set_key(struct mc_parser *parser, uint32_t key_len)
{
	struct mc_command *command = parser->command;

	struct mm_buffer_iterator *iter = &parser->state->sock.rxbuf.head;
	if (unlikely(iter->ptr == iter->end))
		mm_netbuf_read_next(&parser->state->sock);

	char *key;
	if (iter->ptr + key_len <= iter->end) {
		key = iter->ptr;
		iter->ptr += key_len;
	} else {
		key = mm_private_alloc(key_len);
		mm_netbuf_read(&parser->state->sock, key, key_len);
		command->own_key = true;
	}

	mc_action_set_key(&command->action, key, key_len);
}

static bool
mc_binary_read_key(struct mc_parser *parser, uint32_t key_len)
{
	if (!mc_binary_fill(parser, key_len))
		return false;

	// Read the key.
	mc_binary_set_key(parser, key_len);

	return true;
}

static bool
mc_binary_read_entry(struct mc_parser *parser, uint32_t body_len, uint32_t key_len)
{
	if (!mc_binary_fill(parser, body_len))
		return false;

	// Read the extras.
	struct
	{
		uint32_t flags;
		uint32_t exp_time;
	} extras;
	mm_netbuf_read(&parser->state->sock, &extras, sizeof extras);

	// Read the key.
	mc_binary_set_key(parser, key_len);

	// Create an entry.
	struct mc_command *command = parser->command;
	uint32_t value_len = body_len - key_len - sizeof extras;
	mc_action_create(&command->action, value_len);

	// Initialize the entry and its key.
	struct mc_entry *entry = command->action.new_entry;
	entry->flags = mm_ntohl(extras.flags);
	entry->exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));
	mc_entry_setkey(entry, command->action.key);

	// Read the entry value.
	char *value = mc_entry_getvalue(entry);
	mm_netbuf_read(&parser->state->sock, value, entry->value_len);

	return true;
}

static bool
mc_binary_read_chunk(struct mc_parser *parser, uint32_t body_len, uint32_t key_len)
{
	if (!mc_binary_fill(parser, body_len))
		return false;

	// Read the key.
	mc_binary_set_key(parser, key_len);

	// Find the value length.
	struct mc_command *command = parser->command;
	uint32_t value_len = body_len - key_len;
	command->action.value_len = value_len;

	// Read the value.
	struct mm_buffer_iterator *iter = &parser->state->sock.rxbuf.head;
	if (unlikely(iter->ptr == iter->end))
		mm_netbuf_read_next(&parser->state->sock);
	if (iter->ptr + value_len <= iter->end) {
		command->action.alter_value = iter->ptr;
		iter->ptr += value_len;
	} else {
		char *value = mm_private_alloc(value_len);
		mm_netbuf_read(&parser->state->sock, value, value_len);
		command->action.alter_value = value;
		command->own_alter_value = true;
	}

	return true;
}

static bool
mc_binary_read_delta(struct mc_parser *parser, uint32_t key_len)
{
	if (!mc_binary_fill(parser, key_len))
		return false;

	// Read the extras.
	struct
	{
		uint64_t delta;
		uint64_t value;
		uint32_t exp_time;
	} extras;
	mm_netbuf_read(&parser->state->sock, &extras, 20);

	struct mc_command *command = parser->command;
	command->delta = mm_ntohll(extras.delta);
	command->value = mm_ntohll(extras.value);
	command->exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));

	// Read the key.
	mc_binary_set_key(parser, key_len);

	return true;
}

static bool
mc_binary_read_flush(struct mc_parser *parser)
{
	if (!mc_binary_fill(parser, 4))
		return false;

	uint32_t exp_time;
	mm_netbuf_read(&parser->state->sock, &exp_time, sizeof exp_time);

	struct mc_command *command = parser->command;
	command->exp_time = mm_ntohl(exp_time);

	return true;
}

bool NONNULL(1)
mc_binary_parse(struct mc_parser *parser)
{
	ENTER();
	bool rc = true;

	size_t size = mm_netbuf_getleft(&parser->state->sock);
	DEBUG("available bytes: %lu", size);
	if (size < sizeof(struct mc_binary_header)) {
		rc = false;
		goto leave;
	}

	struct mc_binary_header header;
	mm_netbuf_read(&parser->state->sock, &header, sizeof header);
	if (unlikely(header.magic != MC_BINARY_REQUEST)) {
		parser->state->trash = true;
		rc = false;
		goto leave;
	}

	struct mc_command *command = mc_command_create(mm_core_self());
	command->binary.opaque = header.opaque;
	command->binary.opcode = header.opcode;
	parser->command = command;

	// The current command.
	uint32_t body_len = mm_ntohl(header.body_len);
	uint32_t key_len = mm_ntohs(header.key_len);
	uint32_t ext_len = header.ext_len;
	if (unlikely((key_len + ext_len) > body_len)) {
		rc = mc_binary_invalid_arguments(parser, body_len);
		goto leave;
	}

	command->type = mc_binary_commands[header.opcode];
	if (unlikely(command->type == NULL)) {
		rc = mc_binary_unknown_command(parser, body_len);
		goto leave;
	}
	DEBUG("command type: %s", command->type->name);

	switch (command->type->kind) {
	case MC_COMMAND_LOOKUP:
	case MC_COMMAND_DELETE:
		if (unlikely(ext_len != 0)
		    || unlikely(key_len != body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(parser, body_len);
			goto leave;
		}
		rc = mc_binary_read_key(parser, key_len);
		break;

	case MC_COMMAND_STORAGE:
		if (unlikely(ext_len != 8)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(parser, body_len);
			goto leave;
		}
		rc = mc_binary_read_entry(parser, body_len, key_len);
		command->action.stamp = mm_ntohll(header.stamp);
		break;

	case MC_COMMAND_CONCAT:
		if (unlikely(ext_len != 0)
		    || unlikely(key_len == body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(parser, body_len);
			goto leave;
		}
		switch (header.opcode) {
		case MC_BINARY_OPCODE_APPEND:
		case MC_BINARY_OPCODE_APPENDQ:
			command->action.alter_type = MC_ACTION_ALTER_APPEND;
			break;
		case MC_BINARY_OPCODE_PREPEND:
		case MC_BINARY_OPCODE_PREPENDQ:
			command->action.alter_type = MC_ACTION_ALTER_PREPEND;
			break;
		}
		rc = mc_binary_read_chunk(parser, body_len, key_len);
		break;

	case MC_COMMAND_DELTA:
		if (unlikely(ext_len != 20)
		    || unlikely(key_len + ext_len != body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(parser, body_len);
			goto leave;
		}
		rc = mc_binary_read_delta(parser, key_len);
		break;

	case MC_COMMAND_FLUSH:
		if (unlikely(ext_len != 0 && ext_len != 4)
		    || unlikely(key_len != 0)
		    || unlikely(body_len != ext_len)) {
			rc = mc_binary_invalid_arguments(parser, body_len);
			goto leave;
		}
		if (ext_len)
			rc = mc_binary_read_flush(parser);
		break;

	default:
		mc_binary_skip(parser, body_len);
		break;
	}

leave:
	LEAVE();
	return rc;
}
