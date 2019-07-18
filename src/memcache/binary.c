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

#define MC_BINARY_STORAGE_EXTRA_SIZE	(8)
#define MC_BINARY_DELTA_EXTRA_SIZE	(20)

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
	// TODO: reuse memory for large sizes.
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
mc_binary_error(struct mc_state *state, const struct mc_binary_header *header, uint32_t body_len, uint16_t status)
{
	if (!mc_binary_skip(state, body_len))
		return false;

	struct mc_command_simple *command = mc_command_create_binary_simple(state, &mc_command_binary_error, header);
	command->action.binary_status = status;

	return true;
}

static bool
mc_binary_unknown_command(struct mc_state *state, const struct mc_binary_header *header, uint32_t body_len)
{
	return mc_binary_error(state, header, body_len, MC_BINARY_STATUS_UNKNOWN_COMMAND);
}

static bool
mc_binary_invalid_arguments(struct mc_state *state, const struct mc_binary_header *header, uint32_t body_len)
{
	return mc_binary_error(state, header, body_len, MC_BINARY_STATUS_INVALID_ARGUMENTS);
}

static void
mc_binary_set_key(struct mc_state *state, struct mc_action *action, uint16_t key_len)
{
	char *key = mm_netbuf_rget(&state->sock);
	char *end = mm_netbuf_rend(&state->sock);
	if (unlikely(key == end)) {
		mm_netbuf_rnext(&state->sock);
		key = mm_netbuf_rget(&state->sock);
		end = mm_netbuf_rend(&state->sock);
	}

	if (key + key_len <= end) {
		mm_netbuf_radd(&state->sock, key_len);
	} else {
		key = mm_buffer_embed(&state->sock.txbuf, key_len);
		mm_netbuf_read(&state->sock, key, key_len);
	}

	mc_action_set_key(action, key, key_len);
}

static bool
mc_binary_lookup_command(struct mc_state *state, const struct mc_command_type *type,
			 const struct mc_binary_header *header, uint16_t key_len)
{
	if (!mc_binary_fill(state, key_len))
		return false;

	struct mc_command_simple *command = mc_command_create_binary_simple(state, type, header);

	// Read the key.
	mc_binary_set_key(state, &command->action, key_len);

	return true;
}

static bool
mc_binary_storage_command(struct mc_state *state, const struct mc_command_type *type,
			  const struct mc_binary_header *header, uint32_t body_len, uint16_t key_len)
{
	if (!mc_binary_fill(state, body_len))
		return false;

	struct mc_command_storage *command = mc_command_create_binary_storage(state, type, header);
	command->action.stamp = mm_load_nll(&header->stamp);

	// Read the extras.
	struct
	{
		uint32_t flags;
		uint32_t exp_time;
	} extras;
	ASSERT(sizeof(extras) == MC_BINARY_STORAGE_EXTRA_SIZE);
	mm_netbuf_read(&state->sock, &extras, MC_BINARY_STORAGE_EXTRA_SIZE);

	// Read the key.
	mc_binary_set_key(state, &command->action.base, key_len);

	// Create an entry.
	uint32_t value_len = body_len - key_len - sizeof extras;
	mc_action_create(&command->action, value_len);

	// Initialize the entry and its key.
	struct mc_entry *entry = command->action.new_entry;
	entry->flags = mm_ntohl(extras.flags);
	entry->exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));
	mc_entry_setkey(entry, command->action.base.key);

	// Read the entry value.
	char *value = mc_entry_getvalue(entry);
	mm_netbuf_read(&state->sock, value, entry->value_len);

	return true;
}

static bool
mc_binary_concat_command(struct mc_state *state, const struct mc_command_type *type,
			 const struct mc_binary_header *header, uint32_t body_len, uint16_t key_len)
{
	if (!mc_binary_fill(state, body_len))
		return false;

	struct mc_command_storage *command = mc_command_create_binary_storage(state, type, header);

	// Read the key.
	mc_binary_set_key(state, &command->action.base, key_len);

	// Find the value length.
	uint32_t value_len = body_len - key_len;
	command->action.value_len = value_len;

	// Read the value.
	char *value = mm_netbuf_rget(&state->sock);
	char *end = mm_netbuf_rend(&state->sock);
	if (unlikely(value == end)) {
		mm_netbuf_rnext(&state->sock);
		value = mm_netbuf_rget(&state->sock);
		end = mm_netbuf_rend(&state->sock);
	}
	if (value + value_len <= end) {
		mm_netbuf_radd(&state->sock, value_len);
		command->action.own_alter_value = false;
	} else {
		value = mm_private_alloc(value_len);
		mm_netbuf_read(&state->sock, value, value_len);
		command->action.own_alter_value = true;
	}
	command->action.alter_value = value;

	return true;
}

static bool
mc_binary_delta_command(struct mc_state *state, const struct mc_command_type *type,
			const struct mc_binary_header *header, uint16_t key_len)
{
	if (!mc_binary_fill(state, key_len + MC_BINARY_DELTA_EXTRA_SIZE))
		return false;

	// Read the extras.
#pragma pack(push, 4)
	struct
	{
		uint64_t delta;
		uint64_t value;
		uint32_t exp_time;
	} extras;
#pragma pack(pop)
	ASSERT(sizeof(extras) == MC_BINARY_DELTA_EXTRA_SIZE);
	mm_netbuf_read(&state->sock, &extras, MC_BINARY_DELTA_EXTRA_SIZE);

	struct mc_command_storage *command = mc_command_create_binary_storage(state, type, header);
	command->binary_delta = mm_ntohll(extras.delta);
	command->binary_value = mm_ntohll(extras.value);
	command->action.base.binary_exp_time = mc_entry_fix_exptime(mm_ntohl(extras.exp_time));

	// Read the key.
	mc_binary_set_key(state, &command->action.base, key_len);

	return true;
}

static bool
mc_binary_flush_command(struct mc_state *state, const struct mc_command_type *type,
			const struct mc_binary_header *header, uint8_t ext_len)
{
	// Read the extras if present.
	uint32_t exp_time = 0;
	if (ext_len) {
		if (!mc_binary_fill(state, sizeof exp_time))
			return false;
		mm_netbuf_read(&state->sock, &exp_time, sizeof exp_time);
	}

	struct mc_command_simple *command = mc_command_create_binary_simple(state, type, header);
	command->action.binary_exp_time = mm_ntohl(exp_time);

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
	const uint8_t ext_len = header->ext_len;
	const uint16_t key_len = mm_load_ns(&header->key_len);
	const uint32_t body_len = mm_load_nl(&header->body_len);
	if (unlikely((key_len + ext_len) > body_len)) {
		rc = mc_binary_invalid_arguments(state, header, body_len);
		goto leave;
	}

	const struct mc_command_type *const type = mc_binary_commands[header->opcode];
	if (unlikely(type == NULL)) {
		rc = mc_binary_unknown_command(state, header, body_len);
		goto leave;
	}
	DEBUG("command type: %s", type->name);

	switch (type->kind) {
	case MC_COMMAND_LOOKUP:
	case MC_COMMAND_DELETE:
		if (unlikely(ext_len != 0) || unlikely(key_len != body_len) || unlikely(key_len == 0))
			rc = mc_binary_invalid_arguments(state, header, body_len);
		else
			rc = mc_binary_lookup_command(state, type, header, key_len);
		break;

	case MC_COMMAND_STORAGE:
		if (unlikely(ext_len != MC_BINARY_STORAGE_EXTRA_SIZE) || unlikely(key_len == 0))
			rc = mc_binary_invalid_arguments(state, header, body_len);
		else
			rc = mc_binary_storage_command(state, type, header, body_len, key_len);
		break;

	case MC_COMMAND_CONCAT:
		if (unlikely(ext_len != 0) || unlikely(key_len == body_len) || unlikely(key_len == 0))
			rc = mc_binary_invalid_arguments(state, header, body_len);
		else
			rc = mc_binary_concat_command(state, type, header, body_len, key_len);
		break;

	case MC_COMMAND_DELTA:
		if (unlikely(ext_len != MC_BINARY_DELTA_EXTRA_SIZE) || unlikely(key_len + ext_len != body_len) || unlikely(key_len == 0))
			rc = mc_binary_invalid_arguments(state, header, body_len);
		else
			rc = mc_binary_delta_command(state, type, header, key_len);
		break;

	case MC_COMMAND_FLUSH:
		if (unlikely(ext_len != 0 && ext_len != 4) || unlikely(key_len != 0) || unlikely(body_len != ext_len))
			rc = mc_binary_invalid_arguments(state, header, body_len);
		else
			rc = mc_binary_flush_command(state, type, header, ext_len);
		break;

	default:
		if ((rc = mc_binary_skip(state, body_len)))
			mc_command_create_binary_simple(state, type, header);
		break;
	}

leave:
	LEAVE();
	return rc;
}
