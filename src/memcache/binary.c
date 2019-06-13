/*
 * memcache/binary.h - MainMemory memcache binary protocol support.
 *
 * Copyright (C) 2015-2019  Aleksey Demakov
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
#include "memcache/state.h"

#include "base/bytes.h"
#include "base/report.h"
#include "base/memory/memory.h"

#define MC_BINARY_DELTA_EXTRA_SIZE	20

static const struct mc_command_type *mc_binary_commands[256] = {
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
mc_binary_fill(struct mc_state *state, uint32_t required)
{
	uint32_t available = mm_netbuf_size(&state->sock);
	while (required > available) {
		ssize_t n = mm_netbuf_fill(&state->sock, required - available);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				state->error = true;
			return false;
		}
		available += n;
	}
	return true;
}

static bool
mc_binary_skip(struct mc_state *state, uint32_t required)
{
	for (;;) {
		required -= mm_netbuf_skip(&state->sock, required);
		if (required == 0)
			break;

		ssize_t n = mm_netbuf_fill(&state->sock, required);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				state->error = true;
			return false;
		}
	}
	return true;
}

static bool
mc_binary_error(struct mc_state *state, uint32_t body_len, uint16_t status)
{
	if (!mc_binary_skip(state, body_len))
		return false;
	state->command->type = &mc_command_binary_error;
	state->command->value = status;
	return true;
}

static bool
mc_binary_unknown_command(struct mc_state *state, uint32_t body_len)
{
	return mc_binary_error(state, body_len, MC_BINARY_STATUS_UNKNOWN_COMMAND);
}

static bool
mc_binary_invalid_arguments(struct mc_state *state, uint32_t body_len)
{
	return mc_binary_error(state, body_len, MC_BINARY_STATUS_INVALID_ARGUMENTS);
}

static void
mc_binary_set_key(struct mc_state *state, uint16_t key_len)
{
	struct mc_command *command = state->command;

	char *end = mm_netbuf_rend(&state->sock);
	if (unlikely(mm_netbuf_rget(&state->sock) == end)) {
		mm_netbuf_rnext(&state->sock);
		end = mm_netbuf_rend(&state->sock);
	}

	char *key = mm_netbuf_rget(&state->sock);
	if (key + key_len <= end) {
		state->sock.rxbuf.head.ptr += key_len;
	} else {
		key = mm_buffer_embed(&state->sock.txbuf, key_len);
		mm_netbuf_read(&state->sock, key, key_len);
	}

	mc_action_set_key(&command->action, key, key_len);
}

static bool
mc_binary_read_key(struct mc_state *state, uint16_t key_len)
{
	if (!mc_binary_fill(state, key_len))
		return false;

	// Read the key.
	mc_binary_set_key(state, key_len);

	return true;
}

static bool
mc_binary_read_entry(struct mc_state *state, uint32_t body_len, uint16_t key_len)
{
	if (!mc_binary_fill(state, body_len))
		return false;

	// Read the extras.
	struct
	{
		uint32_t flags;
		uint32_t exp_time;
	} extras;
	mm_netbuf_read(&state->sock, &extras, sizeof extras);

	// Read the key.
	mc_binary_set_key(state, key_len);

	// Create an entry.
	struct mc_command *command = state->command;
	uint32_t value_len = body_len - key_len - sizeof extras;
	mc_action_create(&command->storage, value_len);

	// Initialize the entry and its key.
	struct mc_entry *entry = command->storage.new_entry;
	entry->flags = mm_ntohl(extras.flags);
	entry->exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));
	mc_entry_setkey(entry, command->action.key);

	// Read the entry value.
	char *value = mc_entry_getvalue(entry);
	mm_netbuf_read(&state->sock, value, entry->value_len);

	return true;
}

static bool
mc_binary_read_chunk(struct mc_state *state, uint32_t body_len, uint16_t key_len)
{
	if (!mc_binary_fill(state, body_len))
		return false;

	// Read the key.
	mc_binary_set_key(state, key_len);

	// Find the value length.
	struct mc_command *command = state->command;
	uint32_t value_len = body_len - key_len;
	command->storage.value_len = value_len;

	// Read the value.
	char *end = mm_netbuf_rend(&state->sock);
	if (unlikely(mm_netbuf_rget(&state->sock) == end)) {
		mm_netbuf_rnext(&state->sock);
		end = mm_netbuf_rend(&state->sock);
	}

	struct mm_buffer_reader *iter = &state->sock.rxbuf.head;
	if (iter->ptr + value_len <= end) {
		command->storage.alter_value = iter->ptr;
		iter->ptr += value_len;
	} else {
		char *value = mm_private_alloc(value_len);
		mm_netbuf_read(&state->sock, value, value_len);
		command->storage.alter_value = value;
		command->own_alter_value = true;
	}

	return true;
}

static bool
mc_binary_read_delta(struct mc_state *state, uint16_t key_len)
{
	if (!mc_binary_fill(state, key_len + MC_BINARY_DELTA_EXTRA_SIZE))
		return false;

	// Read the extras.
	struct
	{
		uint64_t delta;
		uint64_t value;
		uint32_t exp_time;
	} extras;
	mm_netbuf_read(&state->sock, &extras, MC_BINARY_DELTA_EXTRA_SIZE);

	struct mc_command *command = state->command;
	command->delta = mm_ntohll(extras.delta);
	command->value = mm_ntohll(extras.value);
	command->exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));

	// Read the key.
	mc_binary_set_key(state, key_len);

	return true;
}

static bool
mc_binary_read_flush(struct mc_state *state)
{
	if (!mc_binary_fill(state, 4))
		return false;

	uint32_t exp_time;
	mm_netbuf_read(&state->sock, &exp_time, sizeof exp_time);

	struct mc_command *command = state->command;
	command->exp_time = mm_ntohl(exp_time);

	return true;
}

bool NONNULL(1)
mc_binary_parse(struct mc_state *state)
{
	ENTER();
	bool rc = true;

	// Have enough contiguous space to read the command header.
	if (!mm_netbuf_span(&state->sock, sizeof(struct mc_binary_header)))
		ABORT();

	const size_t size = mm_netbuf_size(&state->sock);
	DEBUG("available bytes: %lu", size);
	if (size < sizeof(struct mc_binary_header)) {
		rc = false;
		goto leave;
	}

	const struct mc_binary_header *const header = (struct mc_binary_header *) mm_netbuf_rget(&state->sock);
	if (unlikely(header->magic != MC_BINARY_REQUEST)) {
		state->trash = true;
		rc = false;
		goto leave;
	}
	mm_netbuf_radd(&state->sock, sizeof(struct mc_binary_header));

	// The header pointer might be unaligned so numeric fields on non-x86
	// archs must be accessed with care.
	const struct mc_command_type *const type = mc_binary_commands[header->opcode];
	const uint8_t ext_len = header->ext_len;
	const uint16_t key_len = mm_load_ns(&header->key_len);
	const uint32_t body_len = mm_load_nl(&header->body_len);
	if (unlikely((key_len + ext_len) > body_len)) {
		rc = mc_binary_invalid_arguments(state, body_len);
		goto leave;
	}
	if (unlikely(type == NULL)) {
		rc = mc_binary_unknown_command(state, body_len);
		goto leave;
	}
	DEBUG("command type: %s", type->name);

	switch (type->kind) {
	case MC_COMMAND_LOOKUP:
	case MC_COMMAND_DELETE:
		if (unlikely(ext_len != 0)
		    || unlikely(key_len != body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(state, body_len);
			goto leave;
		}
		state->command = mc_command_create_simple(state, type);
		state->command->binary.opcode = header->opcode;
		state->command->binary.opaque = mm_load_hl(&header->opaque);
		rc = mc_binary_read_key(state, key_len);
		break;

	case MC_COMMAND_STORAGE:
		if (unlikely(ext_len != 8)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(state, body_len);
			goto leave;
		}
		state->command = mc_command_create_storage(state, type);
		state->command->binary.opcode = header->opcode;
		state->command->binary.opaque = mm_load_hl(&header->opaque);
		state->command->storage.stamp = mm_load_nll(&header->stamp);
		rc = mc_binary_read_entry(state, body_len, key_len);
		break;

	case MC_COMMAND_CONCAT:
		if (unlikely(ext_len != 0)
		    || unlikely(key_len == body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(state, body_len);
			goto leave;
		}
		state->command = mc_command_create_storage(state, type);
		state->command->binary.opcode = header->opcode;
		state->command->binary.opaque = mm_load_hl(&header->opaque);
		switch (header->opcode) {
		case MC_BINARY_OPCODE_APPEND:
		case MC_BINARY_OPCODE_APPENDQ:
			state->command->storage.alter_type = MC_ACTION_ALTER_APPEND;
			break;
		case MC_BINARY_OPCODE_PREPEND:
		case MC_BINARY_OPCODE_PREPENDQ:
			state->command->storage.alter_type = MC_ACTION_ALTER_PREPEND;
			break;
		}
		rc = mc_binary_read_chunk(state, body_len, key_len);
		break;

	case MC_COMMAND_DELTA:
		if (unlikely(ext_len != MC_BINARY_DELTA_EXTRA_SIZE)
		    || unlikely(key_len + ext_len != body_len)
		    || unlikely(key_len == 0)) {
			rc = mc_binary_invalid_arguments(state, body_len);
			goto leave;
		}
		state->command = mc_command_create_storage(state, type);
		state->command->binary.opcode = header->opcode;
		state->command->binary.opaque = mm_load_hl(&header->opaque);
		rc = mc_binary_read_delta(state, key_len);
		break;

	case MC_COMMAND_FLUSH:
		if (unlikely(ext_len != 0 && ext_len != 4)
		    || unlikely(key_len != 0)
		    || unlikely(body_len != ext_len)) {
			rc = mc_binary_invalid_arguments(state, body_len);
			goto leave;
		}
		state->command = mc_command_create_simple(state, type);
		state->command->binary.opcode = header->opcode;
		state->command->binary.opaque = mm_load_hl(&header->opaque);
		if (ext_len)
			rc = mc_binary_read_flush(state);
		break;

	default:
		mc_binary_skip(state, body_len);
		break;
	}

leave:
	LEAVE();
	return rc;
}
