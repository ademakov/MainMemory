/*
 * memcache/command.c - MainMemory memcache commands.
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

#include "memcache/command.h"
#include "memcache/binary.h"
#include "memcache/entry.h"
#include "memcache/state.h"
#include "memcache/table.h"

#include "core/task.h"

#include "base/log/trace.h"
#include "base/mem/buffer.h"

#include "net/net.h"

// The logging verbosity level.
static uint8_t mc_verbose = 0;

static mm_timeval_t mc_exptime;

static struct mm_pool mc_command_pool;

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
static char mc_result_version[] = "VERSION " PACKAGE_STRING "\r\n";

#define RES_N(res)		(sizeof(res) - 1)
#define WRITE(sock, res)	mm_netbuf_write(sock, res, RES_N(res))

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

/*
 * Define command handling info.
 */

#define MC_COMMAND_TYPE(cmd, value)			\
	static void					\
	mc_command_execute_##cmd(struct mc_state *,	\
				 struct mc_command *);	\
	struct mc_command_type mc_command_##cmd = {	\
		.exec = mc_command_execute_##cmd,	\
		.kind = value,				\
		.name = #cmd				\
	};

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Memcache command pool initialization and termination.
 **********************************************************************/

void
mc_command_start(void)
{
	ENTER();

	mm_pool_prepare_shared(&mc_command_pool, "memcache command", sizeof(struct mc_command));

	LEAVE();
}

void
mc_command_stop(void)
{
	ENTER();

	mm_pool_cleanup(&mc_command_pool);

	LEAVE();
}

/**********************************************************************
 * Memcache command creation and destruction.
 **********************************************************************/

struct mc_command *
mc_command_create(mm_core_t core)
{
	ENTER();

	struct mc_command *command = mm_pool_shared_alloc_low(core, &mc_command_pool);
	memset(command, 0, sizeof(struct mc_command));

	LEAVE();
	return command;
}

void
mc_command_destroy(mm_core_t core, struct mc_command *command)
{
	ENTER();

	if (command->own_key)
		mm_local_free((char *) command->action.key);

	mc_action_cleanup(&command->action);

	mm_pool_shared_free_low(core, &mc_command_pool, command);

	LEAVE();
}

/**********************************************************************
 * Command Processing.
 **********************************************************************/

static void
mc_command_quit(struct mc_state *state)
{
	mm_netbuf_flush(&state->sock);
	mm_netbuf_close(&state->sock);
}

static void
mc_command_copy_extra(struct mc_entry *new_entry, struct mc_entry *old_entry)
{
	new_entry->flags = old_entry->flags;
	new_entry->exp_time = old_entry->exp_time;
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
mc_command_transmit_entry(struct mc_state *state,
			  struct mc_command *command,
			  bool cas)
{
	ENTER();

	struct mc_entry *entry = command->action.old_entry;
	const char *key = mc_entry_getkey(entry);
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

	if (command->params.last)
		WRITE(&state->sock, mc_result_end2);
	else
		WRITE(&state->sock, mc_result_nl);

	LEAVE();
}

static void
mc_command_transmit_delta(struct mc_state *state,
			  struct mc_command *command)
{
	ENTER();

	struct mc_entry *entry = command->action.new_entry;
	char *value = mc_entry_getvalue(entry);
	uint32_t value_len = entry->value_len;

	mm_netbuf_splice(&state->sock, value, value_len,
			 mc_command_transmit_unref, (uintptr_t) entry);

	WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_get(struct mc_state *state,
			     struct mc_command *command)
{
	ENTER();

	mc_action_lookup(&command->action);

	if (command->action.old_entry != NULL)
		mc_command_transmit_entry(state, command, false);
	else if (command->params.last)
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_gets(struct mc_state *state,
			      struct mc_command *command)
{
	ENTER();

	mc_action_lookup(&command->action);

	if (command->action.old_entry != NULL)
		mc_command_transmit_entry(state, command, true);
	else if (command->params.last)
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

static void
mc_command_execute_ascii_set(struct mc_state *state,
			     struct mc_command *command)
{
	ENTER();

	mc_action_upsert(&command->action);

	if (command->noreply)
		;
	else
		WRITE(&state->sock, mc_result_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_add(struct mc_state *state,
			     struct mc_command *command)
{
	ENTER();

	mc_action_insert(&command->action);

	if (command->noreply)
		;
	else if (command->action.old_entry == NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_replace(struct mc_state *state,
				 struct mc_command *command)
{
	ENTER();

	mc_action_update(&command->action);

	if (command->noreply)
		;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_cas(struct mc_state *state,
			     struct mc_command *command)
{
	ENTER();

	mc_action_compare_and_update(&command->action, false, false);

	if (command->noreply)
		;
	else if (command->action.entry_match)
		WRITE(&state->sock, mc_result_stored);
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_exists);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_append(struct mc_state *state,
				struct mc_command *command)
{
	ENTER();

	struct mc_entry *new_entry = command->action.new_entry;

	char *append_value = mc_entry_getvalue(new_entry);
	uint32_t append_value_len = new_entry->value_len;
	struct mm_link chunks = new_entry->chunks;
	mm_link_init(&new_entry->chunks);

	mc_action_lookup(&command->action);

	while (command->action.old_entry != NULL) {
		struct mc_entry *old_entry = command->action.old_entry;
		size_t value_len = old_entry->value_len + append_value_len;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry->value_len = value_len;
		mc_entry_free_chunks(new_entry);
		mc_entry_alloc_chunks(new_entry);
		mc_entry_setkey(new_entry, command->action.key);
		mc_command_copy_extra(new_entry, old_entry);

		char *new_value = mc_entry_getvalue(new_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		memcpy(new_value + old_entry->value_len, append_value, append_value_len);
		command->action.stamp = old_entry->stamp;

		mc_action_compare_and_update(&command->action, true, false);
		if (command->action.entry_match)
			break;
	}

	mm_chunk_destroy_chain(mm_link_head(&chunks));

	if (command->noreply)
		;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_prepend(struct mc_state *state,
				 struct mc_command *command)
{
	ENTER();

	struct mc_entry *new_entry = command->action.new_entry;

	char *prepend_value = mc_entry_getvalue(new_entry);
	uint32_t prepend_value_len = new_entry->value_len;
	struct mm_link chunks = new_entry->chunks;
	mm_link_init(&new_entry->chunks);

	mc_action_lookup(&command->action);

	while (command->action.old_entry != NULL) {
		struct mc_entry *old_entry = command->action.old_entry;
		size_t value_len = old_entry->value_len + prepend_value_len;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry->value_len = value_len;
		mc_entry_free_chunks(new_entry);
		mc_entry_alloc_chunks(new_entry);
		mc_entry_setkey(new_entry, command->action.key);
		mc_command_copy_extra(new_entry, old_entry);

		char *new_value = mc_entry_getvalue(new_entry);
		memcpy(new_value, prepend_value, prepend_value_len);
		memcpy(new_value + prepend_value_len, old_value, old_entry->value_len);
		command->action.stamp = old_entry->stamp;

		mc_action_compare_and_update(&command->action, true, false);
		if (command->action.entry_match)
			break;
	}

	mm_chunk_destroy_chain(mm_link_head(&chunks));

	if (command->noreply)
		;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_stored);
	else
		WRITE(&state->sock, mc_result_not_stored);

	LEAVE();
}

static void
mc_command_execute_ascii_incr(struct mc_state *state,
			      struct mc_command *command)
{
	ENTER();

	command->action.new_entry = NULL;

	mc_action_lookup(&command->action);

	while (command->action.old_entry != NULL) {
		uint64_t value;
		if (!mc_entry_getnum(command->action.old_entry, &value)) {
			mc_action_finish(&command->action);
			if (command->action.new_entry != NULL)
				mc_action_cancel(&command->action);
			break;
		}
		value += command->params.val64;
		command->action.stamp = command->action.old_entry->stamp;

		if (command->action.new_entry == NULL) {
			mc_action_create(&command->action);
			mc_command_copy_extra(command->action.new_entry,
					      command->action.old_entry);
		} else {
			mc_entry_free_chunks(command->action.new_entry);
		}
		mc_entry_setnum(command->action.new_entry, &command->action, value);

		mc_action_compare_and_update(&command->action, true,
					     !command->noreply);
		if (command->action.entry_match)
			break;
	}

	if (command->noreply)
		;
	else if (command->action.new_entry != NULL)
		mc_command_transmit_delta(state, command);
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_delta_non_num);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_decr(struct mc_state *state,
			      struct mc_command *command)
{
	ENTER();

	command->action.new_entry = NULL;

	mc_action_lookup(&command->action);

	while (command->action.old_entry != NULL) {
		uint64_t value;
		if (!mc_entry_getnum(command->action.old_entry, &value)) {
			mc_action_finish(&command->action);
			if (command->action.new_entry != NULL)
				mc_action_cancel(&command->action);
			break;
		}
		if (value > command->params.val64)
			value -= command->params.val64;
		else
			value = 0;
		command->action.stamp = command->action.old_entry->stamp;

		if (command->action.new_entry == NULL) {
			mc_action_create(&command->action);
			mc_command_copy_extra(command->action.new_entry,
					      command->action.old_entry);
		} else {
			mc_entry_free_chunks(command->action.new_entry);
		}
		mc_entry_setnum(command->action.new_entry, &command->action, value);

		mc_action_compare_and_update(&command->action, true,
					     !command->noreply);
		if (command->action.entry_match)
			break;
	}

	if (command->noreply)
		;
	else if (command->action.new_entry != NULL)
		mc_command_transmit_delta(state, command);
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_delta_non_num);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_delete(struct mc_state *state,
				struct mc_command *command)
{
	ENTER();

	mc_action_delete(&command->action);

	if (command->noreply)
		;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_deleted);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_touch(struct mc_state *state,
			       struct mc_command *command)
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
		command->action.old_entry->exp_time = command->params.val32;
		mc_action_finish(&command->action);
	}

	if (command->noreply)
		;
	else if (command->action.old_entry != NULL)
		WRITE(&state->sock, mc_result_touched);
	else
		WRITE(&state->sock, mc_result_not_found);

	LEAVE();
}

static void
mc_command_execute_ascii_slabs(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	WRITE(&state->sock, mc_result_not_implemented);
}

static void
mc_command_execute_ascii_stats(struct mc_state *state,
			       struct mc_command *command)
{
	ENTER();

	if (command->params.stats.nopts)
		WRITE(&state->sock, mc_result_not_implemented);
	else
		WRITE(&state->sock, mc_result_end);

	LEAVE();
}

#if ENABLE_MEMCACHE_DELEGATE
static mm_value_t
mc_command_exec_flush(mm_value_t arg)
{
	struct mc_action action;
	action.part = &mc_table.parts[arg];
	mc_action_flush(&action);
	return 0;
}
#endif

static void
mc_command_execute_ascii_flush_all(struct mc_state *state,
				   struct mc_command *command)
{
	ENTER();

	// TODO: really use the exptime.
	mm_timeval_t time = mm_core_self()->time_manager.time;
	mc_exptime = time + command->params.val32 * 1000000ull;

	for (mm_core_t i = 0; i < mc_table.nparts; i++) {
#if ENABLE_MEMCACHE_DELEGATE
		struct mc_tpart *part = &mc_table.parts[i];
		mm_core_post(part->core, mc_command_exec_flush, i);
#else
		struct mc_action action;
		action.part = &mc_table.parts[i];
		mc_action_flush(&action);
#endif
	}

	if (command->noreply)
		;
	else
		WRITE(&state->sock, mc_result_ok);

	LEAVE();
}

static void
mc_command_execute_ascii_version(struct mc_state *state,
				 struct mc_command *command __attribute__((unused)))
{
	ENTER();

	WRITE(&state->sock, mc_result_version);

	LEAVE();
}

static void
mc_command_execute_ascii_verbosity(struct mc_state *state,
				   struct mc_command *command)
{
	ENTER();

	mc_verbose = min(command->params.val32, 2u);
	DEBUG("set verbosity %d", mc_verbose);
	if (command->noreply)
		;
	else
		WRITE(&state->sock, mc_result_ok);

	LEAVE();
}

static void
mc_command_execute_ascii_quit(struct mc_state *state,
			      struct mc_command *command __attribute__((unused)))
{
	ENTER();

	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_ascii_error(struct mc_state *state,
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();

	WRITE(&state->sock, mc_result_error);

	LEAVE();
}

static void
mc_command_execute_binary_get(struct mc_state *state __attribute__((unused)),
			      struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_getq(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_getk(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_getkq(struct mc_state *state __attribute__((unused)),
				struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_set(struct mc_state *state __attribute__((unused)),
			      struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_setq(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_add(struct mc_state *state __attribute__((unused)),
			      struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_addq(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_replace(struct mc_state *state __attribute__((unused)),
				  struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_replaceq(struct mc_state *state __attribute__((unused)),
				   struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_append(struct mc_state *state __attribute__((unused)),
				 struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_appendq(struct mc_state *state __attribute__((unused)),
				  struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_prepend(struct mc_state *state __attribute__((unused)),
				  struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_prependq(struct mc_state *state __attribute__((unused)),
				   struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_increment(struct mc_state *state __attribute__((unused)),
				    struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_incrementq(struct mc_state *state __attribute__((unused)),
				     struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_decrement(struct mc_state *state __attribute__((unused)),
				    struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_decrementq(struct mc_state *state __attribute__((unused)),
				     struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_delete(struct mc_state *state __attribute__((unused)),
				 struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_deleteq(struct mc_state *state __attribute__((unused)),
				  struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_noop(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();

	mc_binary_status(state, command, MC_BINARY_STATUS_NO_ERROR);

	LEAVE();
}

static void
mc_command_execute_binary_quit(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();

	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_binary_quitq(struct mc_state *state __attribute__((unused)),
				struct mc_command *command __attribute__((unused)))
{
	ENTER();

	mc_binary_status(state, command, MC_BINARY_STATUS_NO_ERROR);
	mc_command_quit(state);

	LEAVE();
}

static void
mc_command_execute_binary_flush(struct mc_state *state __attribute__((unused)),
				struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_flushq(struct mc_state *state __attribute__((unused)),
				 struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_version(struct mc_state *state __attribute__((unused)),
				  struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_stat(struct mc_state *state __attribute__((unused)),
			       struct mc_command *command __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mc_command_execute_binary_unknown(struct mc_state *state,
				  struct mc_command *command)
{
	ENTER();

	mc_binary_status(state, command, MC_BINARY_STATUS_UNKNOWN_COMMAND);

	LEAVE();
}
