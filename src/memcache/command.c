/*
 * memcache/command.c - MainMemory memcache commands.
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

#include "memcache/command.h"
#include "memcache/binary.h"
#include "memcache/entry.h"
#include "memcache/state.h"
#include "memcache/table.h"

#include "base/bytes.h"
#include "base/report.h"
#include "base/memory/buffer.h"
#include "base/memory/memory.h"
#include "base/net/net.h"

// The logging verbosity level.
static uint8_t mc_verbose = 0;

static uint32_t mc_exptime;

static char mc_result_nl[] = "\r\n";
static char mc_result_ok[] = "OK\r\n";
static char mc_result_end[] = "END\r\n";
static char mc_result_end2[] = "\r\nEND\r\n";
static char mc_result_error[] = "ERROR\r\n";
static char mc_result_exists[] = "EXISTS\r\n";
static char mc_result_stored[] = "STORED\r\n";
static char mc_result_deleted[] = "DELETED\r\n";
static char mc_result_touched[] = "TOUCHED\r\n";
static char mc_result_not_found[] = "NOT_FOUND\r\n";
static char mc_result_not_stored[] = "NOT_STORED\r\n";
static char mc_result_delta_non_num[] = "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n";
static char mc_result_not_implemented[] = "SERVER_ERROR not implemented\r\n";
static char mc_result_version[] = "VERSION " VERSION "\r\n";

#define RES_N(res)		(sizeof(res) - 1)
#define WRITE(sock, res)	mm_netbuf_write(sock, res, RES_N(res))

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

/*
 * Define command handling info.
 */

#define MC_COMMAND_TYPE(proto, cmd, actn_kind, cmd_kind)	\
	static void						\
	mc_command_execute_##proto##_##cmd(			\
		struct mc_state *,				\
		struct mc_command_##actn_kind *);		\
	struct mc_command_type mc_command_##proto##_##cmd = {	\
		.exec = (mc_command_execute_t)			\
			mc_command_execute_##proto##_##cmd,	\
		.kind = cmd_kind,				\
		.name = stringify_expanded(proto##_##cmd)	\
	};

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Memcache command creation.
 **********************************************************************/

static void
mc_command_prepare_base(struct mc_command_base *base, const struct mc_command_type *type, struct mc_state *state)
{
	base->type = type;
	base->next = NULL;

	if (state->command_last == NULL) {
		state->command_first = base;
		state->command_last = base;
	} else {
		state->command_last->next = base;
		state->command_last = base;
	}
}

struct mc_command_simple * NONNULL(1, 2)
mc_command_create_simple(struct mc_state *state, const struct mc_command_type *type)
{
	ENTER();

	struct mc_command_simple *command = mm_buffer_embed(&state->sock.txbuf, sizeof(struct mc_command_simple));
	mc_command_prepare_base(&command->base, type, state);

	LEAVE();
	return command;
}

struct mc_command_storage * NONNULL(1, 2)
mc_command_create_ascii_storage(struct mc_state *state, const struct mc_command_type *type)
{
	ENTER();

	struct mc_command_storage *command = mm_buffer_embed(&state->sock.txbuf, sizeof(struct mc_command_storage));
	mc_command_prepare_base(&command->base, type, state);

	LEAVE();
	return command;
}

struct mc_command_simple * NONNULL(1, 2, 3)
mc_command_create_binary_simple(struct mc_state *state, const struct mc_command_type *type, const struct mc_binary_header *header)
{
	ENTER();

	struct mc_command_simple *command = mm_buffer_embed(&state->sock.txbuf, sizeof(struct mc_command_simple));
	mc_command_prepare_base(&command->base, type, state);
	command->action.binary_opcode = header->opcode;
	command->action.binary_opaque = mm_load_hl(&header->opaque);

	LEAVE();
	return command;
}

struct mc_command_storage * NONNULL(1, 2, 3)
mc_command_create_binary_storage(struct mc_state *state, const struct mc_command_type *type, const struct mc_binary_header *header)
{
	ENTER();

	struct mc_command_storage *command = mm_buffer_embed(&state->sock.txbuf, sizeof(struct mc_command_storage));
	mc_command_prepare_base(&command->base, type, state);
	command->action.base.binary_opcode = header->opcode;
	command->action.base.binary_opaque = mm_load_hl(&header->opaque);

	LEAVE();
	return command;
}

/**********************************************************************
 * Command processing helpers.
 **********************************************************************/

static void
mc_command_quit(struct mc_state *state)
{
	mm_netbuf_flush(&state->sock);
	mm_netbuf_close(&state->sock);
}

static void
mc_command_flush(uint32_t exptime)
{
	// TODO: really use the exptime.
	struct mm_event_listener *const listener = mm_context_listener();
	mm_timeval_t real_time = mm_event_getrealtime(listener);
	mc_exptime = real_time / 1000000 + exptime;

	for (mm_thread_t i = 0; i < mc_table.nparts; i++) {
		struct mc_action action;
		action.part = &mc_table.parts[i];
		mc_action_flush(&action);
	}
}

static void
mc_command_transmit_unref(uintptr_t data)
{
	ENTER();

	struct mc_entry *entry = (struct mc_entry *) data;

	struct mc_action action;
	action.part = mc_table_part(entry->hash);
	action.old_entry = entry;
	mc_action_finish(&action);

	LEAVE();
}

static void
mc_command_transmit_entry(struct mc_state *state, struct mc_command_simple *command, bool cas)
{
	ENTER();

	struct mc_entry *entry = command->action.old_entry;
	char *key = mc_entry_getkey(entry);
	char *value = mc_entry_getvalue(entry);
	uint8_t key_len = entry->key_len;
	uint32_t value_len = entry->value_len;

	if (cas) {
		mm_netbuf_printf(
			&state->sock,
			"VALUE %.*s %u %u %llu\r\n",
			key_len, key,
			entry->flags, value_len,
			(unsigned long long) entry->stamp);
	} else {
		mm_netbuf_printf(
			&state->sock,
			"VALUE %.*s %u %u\r\n",
			key_len, key,
			entry->flags, value_len);
	}

	mm_netbuf_splice(&state->sock, value, value_len,
			 mc_command_transmit_unref, (uintptr_t) entry);

	if (command->action.ascii_get_last)
		WRITE(&state->sock, mc_result_end2);
	else
		WRITE(&state->sock, mc_result_nl);

	LEAVE();
}

static void
mc_command_transmit_delta(struct mc_state *state, const char *value)
{
	ENTER();

	mm_netbuf_write(&state->sock, value, strlen(value));

	WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_transmit_binary_status(struct mc_state *state, struct mc_action *action, uint16_t status)
{
	ENTER();

	struct mc_binary_header header;
	header.magic = MC_BINARY_RESPONSE;
	header.status = mm_htons(status);
	header.opcode = action->binary_opcode;
	header.opaque = action->binary_opaque;
	header.key_len = 0;
	header.ext_len = 0;
	header.data_type = 0;
	header.body_len = 0;
	header.stamp = 0;

	mm_netbuf_write(&state->sock, &header, sizeof header);

	LEAVE();
}

static void
mc_command_transmit_binary_string(struct mc_state *state, struct mc_action *action, uint16_t status, const char *string, uint32_t length)
{
	ENTER();

	struct mc_binary_header header;
	header.magic = MC_BINARY_RESPONSE;
	header.status = mm_htons(status);
	header.opcode = action->binary_opcode;
	header.opaque = action->binary_opaque;
	header.key_len = 0;
	header.ext_len = 0;
	header.data_type = 0;
	header.body_len = mm_htonl(length);
	header.stamp = 0;

	mm_netbuf_write(&state->sock, &header, sizeof header);
	mm_netbuf_write(&state->sock, string, length);

	LEAVE();
}

static void
mc_command_transmit_binary_stamp(struct mc_state *state, struct mc_action *action, uint64_t stamp)
{
	ENTER();

	struct mc_binary_header header;
	header.magic = MC_BINARY_RESPONSE;
	header.status = MC_BINARY_STATUS_NO_ERROR;
	header.opcode = action->binary_opcode;
	header.opaque = action->binary_opaque;
	header.key_len = 0;
	header.ext_len = 0;
	header.data_type = 0;
	header.body_len = 0;
	header.stamp = mm_htonll(stamp);

	mm_netbuf_write(&state->sock, &header, sizeof header);

	LEAVE();
}

static void
mc_command_transmit_binary_entry(struct mc_state *state, struct mc_action *action, bool with_key)
{
	ENTER();

	struct mc_entry *entry = action->old_entry;
	uint16_t key_len = with_key ? entry->key_len : 0;
	char *value = mc_entry_getvalue(entry);
	uint32_t value_len = entry->value_len;

	struct
	{
		struct mc_binary_header header;
		uint32_t flags;
	} packet;

	packet.header.magic = MC_BINARY_RESPONSE;
	packet.header.status = MC_BINARY_STATUS_NO_ERROR;
	packet.header.opcode = action->binary_opcode;
	packet.header.opaque = action->binary_opaque;
	packet.header.key_len = mm_htons(key_len);
	packet.header.ext_len = 4;
	packet.header.data_type = 0;
	packet.header.body_len = mm_htonl(4 + key_len + entry->value_len);
	packet.header.stamp = mm_htonll(entry->stamp);
	packet.flags = mm_htonl(entry->flags);

	mm_netbuf_write(&state->sock, &packet, 28);
	if (with_key) {
		char *key = mc_entry_getkey(entry);
		mm_netbuf_splice(&state->sock, key, key_len, NULL, 0);
	}
	mm_netbuf_splice(&state->sock, value, value_len, mc_command_transmit_unref, (uintptr_t) entry);

	LEAVE();
}

static void
mc_command_transmit_binary_value(struct mc_state *state, struct mc_action_storage *action, uint64_t value)
{
	ENTER();

	struct mc_entry *entry = action->new_entry;
	struct
	{
		struct mc_binary_header header;
		uint64_t value;
	} packet;

	packet.header.magic = MC_BINARY_RESPONSE;
	packet.header.status = MC_BINARY_STATUS_NO_ERROR;
	packet.header.opcode = action->base.binary_opcode;
	packet.header.opaque = action->base.binary_opaque;
	packet.header.key_len = 0;
	packet.header.ext_len = 0;
	packet.header.data_type = 0;
	packet.header.body_len = mm_htonl(8);
	packet.header.stamp = mm_htonll(entry->stamp);
	packet.value = mm_htonll(value);

	mm_netbuf_write(&state->sock, &packet, 32);

	LEAVE();
}

static void
mm_command_store_value(char *buffer, struct mc_entry *entry)
{
	if (buffer) {
		char *value = mc_entry_getvalue(entry);
		uint32_t value_len = entry->value_len;
		memcpy(buffer, value, value_len);
		buffer[value_len] = 0;
	}
}

static void
mc_command_append(struct mc_action_storage *action)
{
	ENTER();
	const char *alter_value = action->alter_value;
	uint32_t alter_value_len = action->value_len;
	action->new_entry = NULL;

	mc_action_lookup(&action->base);

	struct mc_entry *const old_entry = action->base.old_entry;
	while (old_entry != NULL) {
		uint32_t value_len = old_entry->value_len + alter_value_len;
		if (action->new_entry == NULL) {
			mc_action_create(action, value_len);
			mc_entry_setkey(action->new_entry, action->base.key);
		} else if (action->new_entry->value_len != value_len) {
			mc_action_resize(action, value_len);
			mc_entry_setkey(action->new_entry, action->base.key);
		}

		struct mc_entry *new_entry = action->new_entry;
		char *new_value = mc_entry_getvalue(new_entry);
		char *old_value = mc_entry_getvalue(old_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		memcpy(new_value + old_entry->value_len, alter_value, alter_value_len);
		action->stamp = old_entry->stamp;

		mc_action_alter(action);
		if (action->entry_match)
			break;
	}

	if (action->own_alter_value)
		mm_private_free((char *) alter_value);

	LEAVE();
}

static void
mc_command_prepend(struct mc_action_storage *action)
{
	ENTER();

	const char *alter_value = action->alter_value;
	uint32_t alter_value_len = action->value_len;
	action->new_entry = NULL;

	mc_action_lookup(&action->base);

	struct mc_entry *const old_entry = action->base.old_entry;
	while (old_entry != NULL) {
		uint32_t value_len = old_entry->value_len + alter_value_len;
		if (action->new_entry == NULL) {
			mc_action_create(action, value_len);
			mc_entry_setkey(action->new_entry, action->base.key);
		} else if (action->new_entry->value_len != value_len) {
			mc_action_resize(action, value_len);
			mc_entry_setkey(action->new_entry, action->base.key);
		}

		struct mc_entry *new_entry = action->new_entry;
		char *new_value = mc_entry_getvalue(new_entry);
		char *old_value = mc_entry_getvalue(old_entry);
		memcpy(new_value, alter_value, alter_value_len);
		memcpy(new_value + alter_value_len, old_value, old_entry->value_len);
		action->stamp = old_entry->stamp;

		mc_action_alter(action);
		if (action->entry_match)
			break;
	}

	if (action->own_alter_value)
		mm_private_free((char *) alter_value);

	LEAVE();
}

static uint64_t
mc_command_increment(struct mc_command_storage *command, bool ascii, char *buffer)
{
	ENTER();
	uint64_t value = 0;
	struct mc_action_storage *action = &command->action;
	action->new_entry = NULL;

	mc_action_lookup(&action->base);

	for (;;) {
		struct mc_entry *const old_entry = action->base.old_entry;
		if (old_entry == NULL) {
			if (ascii)
				break;

			value = command->binary_value;
			action->stamp = 0;
		} else {
			if (!mc_entry_getnum(old_entry, &value)) {
				mc_action_finish(&action->base);
				if (action->new_entry != NULL)
					mc_action_cancel(action);
				break;
			}
			value += command->binary_delta;
			action->stamp = old_entry->stamp;
		}

		if (action->new_entry == NULL) {
			mc_action_create(action, MC_ENTRY_NUM_LEN_MAX);
			mc_entry_setkey(action->new_entry, action->base.key);
		}
		mc_entry_setnum(action->new_entry, value);

		if (old_entry == NULL) {
			mc_action_insert(action);
			if (action->base.old_entry == NULL)
				break;
		} else {
			mm_command_store_value(buffer, action->new_entry);
			mc_action_alter(action);
			if (action->entry_match)
				break;
		}
	}

	LEAVE();
	return value;
}

static uint64_t
mc_command_decrement(struct mc_command_storage *command, bool ascii, char *buffer)
{
	ENTER();
	uint64_t value = 0;
	struct mc_action_storage *action = &command->action;
	action->new_entry = NULL;

	mc_action_lookup(&action->base);

	for (;;) {
		struct mc_entry *const old_entry = action->base.old_entry;
		if (old_entry == NULL) {
			if (ascii)
				break;

			value = command->binary_value;
			action->stamp = 0;
		} else {
			if (!mc_entry_getnum(old_entry, &value)) {
				mc_action_finish(&action->base);
				if (action->new_entry != NULL)
					mc_action_cancel(action);
				break;
			}
			if (value > command->binary_delta)
				value -= command->binary_delta;
			else
				value = 0;
			action->stamp = old_entry->stamp;
		}

		if (action->new_entry == NULL) {
			mc_action_create(action, MC_ENTRY_NUM_LEN_MAX);
			mc_entry_setkey(action->new_entry, action->base.key);
		}
		mc_entry_setnum(action->new_entry, value);

		if (old_entry == NULL) {
			mc_action_insert(action);
			if (action->base.old_entry == NULL)
				break;
		} else {
			mm_command_store_value(buffer, action->new_entry);
			mc_action_alter(action);
			if (action->entry_match)
				break;
		}
	}

	LEAVE();
	return value;
}

/**********************************************************************
 * ASCII protocol commands.
 **********************************************************************/

static void
mc_command_execute_ascii_get(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_entry(state, command, false);
	else if (command->action.ascii_get_last)
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_gets(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_entry(state, command, true);
	else if (command->action.ascii_get_last)
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_set(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_upsert(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else
		WRITE(&state->sock, mc_result_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_add(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_insert(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.base.old_entry == NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_replace(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_update(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_cas(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_update(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.entry_match)
		WRITE(&state->sock, mc_result_stored);
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_exists);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_append(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_append(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_prepend(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_prepend(&command->action);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_incr(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	char buffer[MC_ENTRY_NUM_LEN_MAX + 1];
	mc_command_increment(command, true, buffer);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.new_entry != NULL)
		mc_command_transmit_delta(state, buffer);
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_delta_non_num);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_decr(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	char buffer[MC_ENTRY_NUM_LEN_MAX + 1];
	mc_command_decrement(command, true, buffer);
	if (command->action.base.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.new_entry != NULL)
		mc_command_transmit_delta(state, buffer);
	else if (command->action.base.old_entry != NULL)
		WRITE(&state->sock, mc_result_delta_non_num);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_delete(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_delete(&command->action);
	if (command->action.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_deleted);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_touch(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);

	if (command->action.old_entry != NULL) {
		// There is no much need for synchronization here.
		// * A concurrent touch is not a big problem. Nobody knows
		// which one of them is set to win;
		// * If we set exptime on an entry that has concurrently
		// been deleted then there is absolutely no harm;
		// * If we set exptime on an entry that has concurrently
		// been replaced then the replace command has its own
		// exptime which wins and this seems to be just fine;
		// * If we set exptime on an entry that is being incremented
		// or decremented then admittedly we might loose the exptime
		// update. But after all who is going to ever sensibly use
		// exptime and incr/decr together?
		command->action.old_entry->exp_time = command->action.ascii_exp_time;
		mc_action_finish(&command->action);
	}

	if (command->action.ascii_noreply)
		/* Be quiet. */;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_touched);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_slabs(struct mc_state *state, struct mc_command_simple *command UNUSED)
{
	WRITE(&state->sock, mc_result_not_implemented);
}

static void
mc_command_execute_ascii_stats(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	if (command->action.ascii_stats)
		WRITE(&state->sock, mc_result_not_implemented);
	else
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_flush_all(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_flush(command->action.ascii_exp_time);
	if (command->action.ascii_noreply)
		/* Be quiet. */;
	else
		WRITE(&state->sock, mc_result_ok);

	LEAVE();
}

static void
mc_command_execute_ascii_version(struct mc_state *state, struct mc_command_simple *command UNUSED)
{
	ENTER();

	WRITE(&state->sock, mc_result_version);

	LEAVE();
}

static void
mc_command_execute_ascii_verbosity(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_verbose = min(command->action.ascii_level, 2u);
	DEBUG("set verbosity %d", mc_verbose);
	if (command->action.ascii_noreply)
		/* Be quiet. */;
	else
		WRITE(&state->sock, mc_result_ok);

	LEAVE();
}

static void
mc_command_execute_ascii_quit(struct mc_state *state, struct mc_command_simple *command UNUSED)
{
	ENTER();

	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_ascii_error(struct mc_state *state, struct mc_command_simple *command UNUSED)
{
	ENTER();

	WRITE(&state->sock, mc_result_error);

	LEAVE();
}

/**********************************************************************
 * Binary protocol commands.
 **********************************************************************/

static void
mc_command_execute_binary_get(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_binary_entry(state, &command->action, false);
	else
		mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_getq(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_binary_entry(state, &command->action, false);

	LEAVE();
}

static void
mc_command_execute_binary_getk(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_binary_entry(state, &command->action, true);
	else
		mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_getkq(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_lookup(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_binary_entry(state, &command->action, true);

	LEAVE();
}

static void
mc_command_execute_binary_set(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	if (command->action.stamp) {
		mc_action_update(&command->action);
		if (command->action.entry_match)
			mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
		else if (command->action.base.old_entry != NULL)
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_ITEM_NOT_STORED);
	} else {
		mc_action_upsert(&command->action);
		mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
	}

	LEAVE();
}

static void
mc_command_execute_binary_setq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	if (command->action.stamp) {
		mc_action_update(&command->action);
		if (command->action.entry_match)
			/* Be quiet. */;
		else if (command->action.base.old_entry != NULL)
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_ITEM_NOT_STORED);
	} else {
		mc_action_upsert(&command->action);
	}

	LEAVE();
}

static void
mc_command_execute_binary_add(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_insert(&command->action);
	if (command->action.base.old_entry == NULL)
		mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);

	LEAVE();
}

static void
mc_command_execute_binary_addq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_action_insert(&command->action);
	if (command->action.base.old_entry == NULL)
		/* Be quiet. */;
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);

	LEAVE();
}

static void
mc_command_execute_binary_replace(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	if (command->action.stamp) {
		mc_action_update(&command->action);
		if (command->action.entry_match)
			mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
		else if (command->action.base.old_entry != NULL)
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);
	} else {
		mc_action_update(&command->action);
		if (command->action.base.old_entry != NULL)
			mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);
	}

	LEAVE();
}

static void
mc_command_execute_binary_replaceq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	if (command->action.stamp) {
		mc_action_update(&command->action);
		if (command->action.entry_match)
			/* Be quiet. */;
		else if (command->action.base.old_entry != NULL)
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_EXISTS);
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);
	} else {
		mc_action_update(&command->action);
		if (command->action.base.old_entry != NULL)
			/* Be quiet. */;
		else
			mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);
	}

	LEAVE();
}

static void
mc_command_execute_binary_append(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_append(&command->action);
	if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_appendq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_append(&command->action);
	if (command->action.base.old_entry != NULL)
		/* Be quiet. */;
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_prepend(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_prepend(&command->action);
	if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_stamp(state, &command->action.base, command->action.stamp);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_prependq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_prepend(&command->action);
	if (command->action.base.old_entry != NULL)
		/* Be quiet. */;
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_increment(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	uint64_t value = mc_command_increment(command, false, NULL);
	if (command->action.new_entry != NULL)
		mc_command_transmit_binary_value(state, &command->action, value);
	else if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_NON_NUMERIC_VALUE);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_incrementq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_increment(command, false, NULL);
	if (command->action.new_entry != NULL)
		/* Be quiet. */;
	else if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_NON_NUMERIC_VALUE);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_decrement(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	uint64_t value = mc_command_decrement(command, false, NULL);
	if (command->action.new_entry != NULL)
		mc_command_transmit_binary_value(state, &command->action, value);
	else if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_NON_NUMERIC_VALUE);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_decrementq(struct mc_state *state, struct mc_command_storage *command)
{
	ENTER();

	mc_command_decrement(command, false, NULL);
	if (command->action.new_entry != NULL)
		/* Be quiet. */;
	else if (command->action.base.old_entry != NULL)
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_NON_NUMERIC_VALUE);
	else
		mc_command_transmit_binary_status(state, &command->action.base, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_delete(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_delete(&command->action);
	if (command->action.old_entry != NULL)
		mc_command_transmit_binary_stamp(state, &command->action, 0);
	else
		mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_deleteq(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_action_delete(&command->action);
	if (command->action.old_entry != NULL)
		;
	else
		mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_KEY_NOT_FOUND);

	LEAVE();
}

static void
mc_command_execute_binary_noop(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_NO_ERROR);

	LEAVE();
}

static void
mc_command_execute_binary_quit(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_NO_ERROR);
	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_binary_quitq(struct mc_state *state, struct mc_command_simple *command UNUSED)
{
	ENTER();

	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_binary_flush(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_flush(command->action.binary_exp_time);
	mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_NO_ERROR);

	LEAVE();
}

static void
mc_command_execute_binary_flushq(struct mc_state *state UNUSED, struct mc_command_simple *command)
{
	ENTER();

	mc_command_flush(command->action.binary_exp_time);

	LEAVE();
}

static void
mc_command_execute_binary_version(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_transmit_binary_string(state, &command->action, MC_BINARY_STATUS_NO_ERROR, VERSION, RES_N(VERSION));

	LEAVE();
}

static void
mc_command_execute_binary_stat(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_transmit_binary_status(state, &command->action, MC_BINARY_STATUS_NO_ERROR);

	LEAVE();
}

static void
mc_command_execute_binary_error(struct mc_state *state, struct mc_command_simple *command)
{
	ENTER();

	mc_command_transmit_binary_status(state, &command->action, command->action.binary_status);

	LEAVE();
}
