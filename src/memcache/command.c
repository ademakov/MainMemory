/*
 * memcache/command.c - MainMemory memcache commands.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "memcache/entry.h"
#include "memcache/table.h"

#include "buffer.h"
#include "hash.h"
#include "net.h"
#include "task.h"
#include "trace.h"

// The logging verbosity level.
static uint8_t mc_verbose = 0;

static mm_timeval_t mc_curtime;
static mm_timeval_t mc_exptime;

static struct mm_pool mc_command_pool;

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

/*
 * Define command names.
 */

#if ENABLE_DEBUG

#define MC_COMMAND_NAME(cmd, value)	#cmd,

static const char *mc_command_names[] = {
	MC_COMMAND_LIST(MC_COMMAND_NAME)
};

#undef MC_COMMAND_NAME

const char *
mc_command_name(mc_command_t tag)
{
	static const size_t n = sizeof(mc_command_names) / sizeof(*mc_command_names);
	if (tag >= n)
		return "invalid command";
	return mc_command_names[tag];
}

#endif

/*
 * Define command handling info.
 */

#define MC_COMMAND_TYPE(cmd, value)				\
	static mm_value_t mc_command_exec_##cmd(mm_value_t);	\
	struct mc_command_type mc_desc_##cmd = {		\
		.tag = mc_command_##cmd,			\
		.exec = mc_command_exec_##cmd,			\
		.flags = value,					\
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
		mm_local_free((char *) command->key.str);

	switch (mc_command_result(command)) {
	case MC_RESULT_ENTRY:
	case MC_RESULT_ENTRY_CAS:
	case MC_RESULT_VALUE:
		mc_entry_unref(command->entry);
		break;

	default:
		break;
	}

#if !ENABLE_MEMCACHE_LOCKS
	if (command->future != NULL)
		mm_future_destroy(command->future);
#endif

	mm_pool_shared_free_low(core, &mc_command_pool, command);

	LEAVE();
}

/**********************************************************************
 * Command Processing.
 **********************************************************************/

void
mc_command_execute(struct mc_command *command)
{
	if (unlikely(command->result != MC_RESULT_NONE))
		return;

	if ((command->type->flags & MC_ASYNC) != 0) {
		command->key_hash = mc_hash(command->key.str, command->key.len);

#if ENABLE_SMP && !ENABLE_MEMCACHE_LOCKS
		struct mc_tpart *part = mc_table_part(command->key_hash);
		command->result_type = MC_RESULT_FUTURE;
		command->future = mm_future_create(command->type->exec,
						   (mm_value_t) command);
		mm_future_start(command->future, part->core);
		return;
#endif
	}

	command->result = (command->type->exec)((mm_value_t) command);
}

static mc_command_result_t
mc_command_result_entry(struct mc_command *command,
			struct mc_entry *entry,
			mc_command_result_t result)
{
	mc_entry_ref(entry);
	command->entry = entry;
	return result;
}

static void
mc_command_process_value(struct mc_entry *entry,
			 struct mc_command_params_set *params,
			 uint32_t offset)
{
	ENTER();

	const char *src = params->start;
	uint32_t bytes = params->bytes;
	struct mm_buffer_segment *seg = params->seg;
	ASSERT(src >= seg->data && src <= seg->data + seg->size);

	char *dst = mc_entry_getvalue(entry) + offset;
	for (;;) {
		uint32_t n = (seg->data + seg->size) - src;
		if (n >= bytes) {
			memcpy(dst, src, bytes);
			break;
		}

		memcpy(dst, src, n);
		seg = seg->next;
		src = seg->data;
		dst += n;
		bytes -= n;
	}

	LEAVE();
}

static mm_value_t
mc_command_process_get2(mm_value_t arg, mc_command_result_t rc)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *entry = mc_table_lookup(part, hash, key, key_len);
	if (entry != NULL)
		mc_table_touch(part, entry);

	if (entry != NULL)
		rc = mc_command_result_entry(command, entry, rc);
	else if (command->params.last)
		rc = MC_RESULT_END;
	else
		rc = MC_RESULT_BLANK;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_get(mm_value_t arg)
{
	return mc_command_process_get2(arg, MC_RESULT_ENTRY);
}

static mm_value_t
mc_command_exec_gets(mm_value_t arg)
{
	return mc_command_process_get2(arg, MC_RESULT_ENTRY_CAS);
}

static mm_value_t
mc_command_exec_set(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = mc_entry_create(key_len, params->bytes);
	mc_entry_setmisc(new_entry, key, params->flags, params->exptime, hash);
	mc_command_process_value(new_entry, params, 0);
	mc_table_insert(part, hash, new_entry);

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_add(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry == NULL) {
		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, params->exptime, hash);
		mc_command_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = MC_RESULT_STORED;
	else
		rc = MC_RESULT_NOT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_replace(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, params->exptime, hash);
		mc_command_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = MC_RESULT_STORED;
	else
		rc = MC_RESULT_NOT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_cas(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && old_entry->cas == params->cas) {
		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, params->exptime, hash);
		mc_command_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = MC_RESULT_STORED;
	else if (old_entry != NULL)
		rc = MC_RESULT_EXISTS;
	else
		rc = MC_RESULT_NOT_FOUND;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_append(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_setmisc(new_entry, key, old_entry->flags, old_entry->exp_time, hash);
		char *new_value = mc_entry_getvalue(new_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		mc_command_process_value(new_entry, params, old_entry->value_len);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = MC_RESULT_STORED;
	else
		rc = MC_RESULT_NOT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_prepend(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_command_params_set *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_setmisc(new_entry, key, old_entry->flags, old_entry->exp_time, hash);
		char *new_value = mc_entry_getvalue(new_entry);
		mc_command_process_value(new_entry, params, 0);
		memcpy(new_value + params->bytes, old_value, old_entry->value_len);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = MC_RESULT_STORED;
	else
		rc = MC_RESULT_NOT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_incr(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		value += command->params.val64;

		new_entry = mc_entry_create_u64(key_len, value);
		mc_entry_setmisc(new_entry, key, old_entry->flags, old_entry->exp_time, hash);

		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(part, hash, new_entry);
	}

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = mc_command_result_entry(command, new_entry, MC_RESULT_VALUE);
	else if (old_entry != NULL)
		rc = MC_RESULT_INC_DEC_NON_NUM;
	else
		rc = MC_RESULT_NOT_FOUND;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_decr(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		if (value > command->params.val64)
			value -= command->params.val64;
		else
			value = 0;

		new_entry = mc_entry_create_u64(key_len, value);
		mc_entry_setmisc(new_entry, key, old_entry->flags, old_entry->exp_time, hash);

		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(part, hash, new_entry);
	}

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = mc_command_result_entry(command, new_entry, MC_RESULT_VALUE);
	else if (old_entry != NULL)
		rc = MC_RESULT_INC_DEC_NON_NUM;
	else
		rc = MC_RESULT_NOT_FOUND;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_delete(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (old_entry != NULL)
		rc = MC_RESULT_DELETED;
	else
		rc = MC_RESULT_NOT_FOUND;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_touch(mm_value_t arg __attribute__((unused)))
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *entry = mc_table_lookup(part, hash, key, key_len);
	if (entry != NULL)
		entry->exp_time = command->params.val32;

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (entry != NULL)
		rc = MC_RESULT_TOUCHED;
	else
		rc = MC_RESULT_NOT_FOUND;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_slabs(mm_value_t arg __attribute__((unused)))
{
	return MC_RESULT_NOT_IMPLEMENTED;
}

static mm_value_t
mc_command_exec_stats(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	mc_command_result_t rc;
	if (command->params.stats.nopts)
		rc = MC_RESULT_NOT_IMPLEMENTED;
	else
		rc = MC_RESULT_END;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_flush_part(mm_value_t arg)
{
	ENTER();

	struct mc_tpart *part = &mc_table.parts[arg];
	while (mc_table_evict(part, 256))
		mm_task_yield();

	LEAVE();
	return 0;
}

static mm_value_t
mc_command_exec_flush_all(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	// TODO: really use the exptime.
	mc_exptime = mc_curtime + command->params.val32 * 1000000ull;

	for (mm_core_t i = 0; i < mc_table.nparts; i++) {
#if ENABLE_MEMCACHE_LOCKS
		mm_core_post(MM_CORE_NONE, mc_command_flush_part, i);
#else
		struct mc_tpart *part = &mc_table.parts[i];
		mm_core_post(part->core, mc_process_flush, i);
#endif
	}

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_OK;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_version(mm_value_t arg __attribute__((unused)))
{
	return MC_RESULT_VERSION;
}

static mm_value_t
mc_command_exec_verbosity(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	mc_verbose = min(command->params.val32, 2u);
	DEBUG("set verbosity %d", mc_verbose);

	mc_command_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_OK;

	LEAVE();
	return rc;
}

static mm_value_t
mc_command_exec_quit(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mm_net_shutdown_reader(command->params.sock);

	LEAVE();
	return MC_RESULT_QUIT;
}

