/*
 * memcache.c - MainMemory memcached protocol support.
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

#include "memcache.h"

#include "entry.h"
#include "table.h"

#include "../alloc.h"
#include "../bitops.h"
#include "../buffer.h"
#include "../chunk.h"
#include "../core.h"
#include "../hash.h"
#include "../future.h"
#include "../list.h"
#include "../log.h"
#include "../net.h"
#include "../pool.h"
#include "../trace.h"

#include <stdio.h>
#include <stdlib.h>

#define MC_VERSION		"VERSION MainMemory 0.0\r\n"

// The logging verbosity level.
static uint8_t mc_verbose = 0;

struct mm_memcache_config mc_config;

static mm_timeval_t mc_curtime;
static mm_timeval_t mc_exptime;

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

#define MC_ASYNC 1

/*
 * Some preprocessor magic to emit command definitions.
 */

#define MC_COMMAND_LIST(_)				\
	_(get,		get,		MC_ASYNC)	\
	_(gets,		gets,		MC_ASYNC)	\
	_(set,		set,		MC_ASYNC)	\
	_(add,		add,		MC_ASYNC)	\
	_(replace,	replace,	MC_ASYNC)	\
	_(append,	append,		MC_ASYNC)	\
	_(prepend,	prepend,	MC_ASYNC)	\
	_(cas,		cas,		MC_ASYNC)	\
	_(incr,		incr,		MC_ASYNC)	\
	_(decr,		decr,		MC_ASYNC)	\
	_(delete,	delete,		MC_ASYNC)	\
	_(touch,	touch,		MC_ASYNC)	\
	_(slabs,	slabs,		0)		\
	_(stats,	stats,		0)		\
	_(flush_all,	flush_all,	0)		\
	_(version,	version,	0)		\
	_(verbosity,	verbosity,	0)		\
	_(quit,		quit,		0)

/*
 * Define enumerated type to tag commands.
 */

#define MC_COMMAND_TAG(cmd, process_name, value)	mc_command_##cmd,

typedef enum {
	MC_COMMAND_LIST(MC_COMMAND_TAG)
} mc_command_t;

/*
 * Define command handling info.
 */

struct mc_command_type
{
	mc_command_t tag;
	mm_routine_t process;
	uint32_t flags;
};

#define MC_COMMAND_TYPE(cmd, process_name, value)			\
	static mm_value_t mc_process_##process_name(mm_value_t);	\
	static struct mc_command_type mc_desc_##cmd = {			\
		.tag = mc_command_##cmd,				\
		.process = mc_process_##process_name,			\
		.flags = value,						\
	};

MC_COMMAND_LIST(MC_COMMAND_TYPE)

/*
 * Define command names.
 */

#if ENABLE_DEBUG

#define MC_COMMAND_NAME(cmd, process_name, value)	#cmd,

static const char *mc_command_names[] = {
	MC_COMMAND_LIST(MC_COMMAND_NAME)
};

static const char *
mc_command_name(mc_command_t tag)
{
	static const size_t n = sizeof(mc_command_names) / sizeof(*mc_command_names);

	if (tag >= n)
		return "bad command";
	else
		return mc_command_names[tag];
}

#endif

/**********************************************************************
 * Command Data.
 **********************************************************************/

struct mc_string
{
	size_t len;
	const char *str;
};

struct mc_set_params
{
	struct mm_buffer_segment *seg;
	const char *start;
	uint32_t bytes;

	uint32_t flags;
	uint32_t exptime;
	uint64_t cas;
};

struct mc_slabs_params
{
	uint32_t nopts;
};

struct mc_stats_params
{
	uint32_t nopts;
};

union mc_params
{
	struct mc_set_params set;
	struct mc_slabs_params slabs;
	struct mc_stats_params stats;
	struct mm_net_socket *sock;
	uint64_t val64;
	uint32_t val32;
	bool last;
};

typedef enum
{
	MC_RESULT_NONE = 0,
	MC_RESULT_FUTURE,

	MC_RESULT_BLANK,
	MC_RESULT_OK,
	MC_RESULT_END,
	MC_RESULT_ERROR,
	MC_RESULT_EXISTS,
	MC_RESULT_STORED,
	MC_RESULT_DELETED,
	MC_RESULT_NOT_FOUND,
	MC_RESULT_NOT_STORED,
	MC_RESULT_INC_DEC_NON_NUM,
	MC_RESULT_NOT_IMPLEMENTED,
	MC_RESULT_CANCELED,
	MC_RESULT_VERSION,

	MC_RESULT_ENTRY,
	MC_RESULT_ENTRY_CAS,
	MC_RESULT_VALUE,

	MC_RESULT_QUIT,
} mc_result_t;

struct mc_command
{
	struct mc_command *next;

	struct mc_command_type *type;
	struct mc_string key;
	union mc_params params;
	struct mc_entry *entry;
	mc_result_t result_type;
	bool noreply;
	bool own_key;
	uint32_t key_hash;

	struct mm_future *future;

	char *end_ptr;
};

static struct mm_pool mc_command_pool;

static void
mc_command_start(void)
{
	ENTER();

	mm_pool_prepare_shared(&mc_command_pool, "memcache command", sizeof(struct mc_command));

	LEAVE();
}

static void
mc_command_stop()
{
	ENTER();

	mm_pool_cleanup(&mc_command_pool);

	LEAVE();
}

static struct mc_command *
mc_command_create(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_command *command = mm_pool_shared_alloc_low(sock->core, &mc_command_pool);
	memset(command, 0, sizeof(struct mc_command));

	LEAVE();
	return command;
}

// TODO: Really support some options.
static void
mc_command_option(struct mc_command *command)
{
	ENTER();

	if (command->type != NULL) {

		switch (command->type->tag) {
		case mc_command_slabs:
			command->params.slabs.nopts++;
			break;

		case mc_command_stats:
			command->params.stats.nopts++;
			break;

		default:
			break;
		}
	}

	LEAVE();
}

static mc_result_t
mc_command_entry(struct mc_command *command, struct mc_entry *entry, mc_result_t res_type)
{
	mc_entry_ref(entry);
	command->entry = entry;
	return res_type;
}

static mc_result_t
mc_command_result(struct mc_command *command)
{
	mc_result_t result = command->result_type;
	if (result == MC_RESULT_FUTURE) {
		result = mm_future_wait(command->future);
		if (mm_future_is_canceled(command->future)) {
			result = MC_RESULT_CANCELED;
		}
		command->result_type = result;
	}
	return result;
}

static void
mc_command_destroy(struct mm_net_socket *sock, struct mc_command *command)
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

	if (command->future != NULL)
		mm_future_destroy(command->future);

	mm_pool_shared_free_low(sock->core, &mc_command_pool, command);

	LEAVE();
}

/**********************************************************************
 * Aggregate Connection State.
 **********************************************************************/

struct mc_state
{
	// The client socket,
	struct mm_net_socket sock;
	// Receive buffer.
	struct mm_buffer rbuf;
	// Transmit buffer.
	struct mm_buffer tbuf;

	// Current parse position.
	char *start_ptr;
	// Last processed position.
	char *end_ptr;

	// Command processing queue.
	struct mc_command *command_head;
	struct mc_command *command_tail;

	// Flags.
	bool error;
	bool trash;
};

static void
mc_queue_command(struct mc_state *state,
		 struct mc_command *first,
		 struct mc_command *last)
{
	ENTER();
	ASSERT(first != NULL);
	ASSERT(last != NULL);

	if (state->command_head == NULL) {
		state->command_head = first;
	} else {
		state->command_tail->next = first;
	}
	state->command_tail = last;

	LEAVE();
}

static void
mc_release_buffers(struct mc_state *state, char *ptr)
{
	ENTER();

	size_t size = 0;

	struct mm_buffer_cursor cur;
	bool rc = mm_buffer_first_out(&state->rbuf, &cur);
	while (rc) {
		if (ptr >= cur.ptr && ptr <= cur.end) {
			// The buffer is (might be) still in use.
			if (ptr == cur.end && state->start_ptr == cur.end)
				state->start_ptr = NULL;
			size += ptr - cur.ptr;
			break;
		}

		size += cur.end - cur.ptr;
		rc = mm_buffer_next_out(&state->rbuf, &cur);
	}

	if (size > 0)
		mm_buffer_reduce(&state->rbuf, size);

	LEAVE();
}

/**********************************************************************
 * Command Processing.
 **********************************************************************/

static void
mc_process_value(struct mc_entry *entry, struct mc_set_params *params, uint32_t offset)
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
mc_process_get2(mm_value_t arg, mc_result_t rc)
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
		rc = mc_command_entry(command, entry, rc);
	else if (command->params.last)
		rc = MC_RESULT_END;
	else
		rc = MC_RESULT_BLANK;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_get(mm_value_t arg)
{
	return mc_process_get2(arg, MC_RESULT_ENTRY);
}

static mm_value_t
mc_process_gets(mm_value_t arg)
{
	return mc_process_get2(arg, MC_RESULT_ENTRY_CAS);
}

static mm_value_t
mc_process_set(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = mc_entry_create(key_len, params->bytes);
	mc_entry_setmisc(new_entry, key, params->flags, hash);
	mc_process_value(new_entry, params, 0);
	mc_table_insert(part, hash, new_entry);

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_STORED;

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_add(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry == NULL) {
		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, hash);
		mc_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	mc_result_t rc;
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
mc_process_replace(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, hash);
		mc_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_result_t rc;
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
mc_process_cas(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_lookup(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && old_entry->cas == params->cas) {
		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_setmisc(new_entry, key, params->flags, hash);
		mc_process_value(new_entry, params, 0);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	mc_result_t rc;
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
mc_process_append(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_setmisc(new_entry, key, old_entry->flags, hash);
		char *new_value = mc_entry_getvalue(new_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		mc_process_value(new_entry, params, old_entry->value_len);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_result_t rc;
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
mc_process_prepend(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	uint32_t hash = command->key_hash;
	struct mc_set_params *params = &command->params.set;

	struct mc_tpart *part = mc_table_part(command->key_hash);
	mc_table_lock(part);

	struct mc_entry *old_entry = mc_table_remove(part, hash, key, key_len);
	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_getvalue(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_setmisc(new_entry, key, old_entry->flags, hash);
		char *new_value = mc_entry_getvalue(new_entry);
		mc_process_value(new_entry, params, 0);
		memcpy(new_value + params->bytes, old_value, old_entry->value_len);
		mc_table_insert(part, hash, new_entry);
	}

	mc_table_unlock(part);

	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	mc_result_t rc;
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
mc_process_incr(mm_value_t arg)
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
		mc_entry_setmisc(new_entry, key, old_entry->flags, hash);

		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(part, hash, new_entry);
	}

	mc_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = mc_command_entry(command, new_entry, MC_RESULT_VALUE);
	else if (old_entry != NULL)
		rc = MC_RESULT_INC_DEC_NON_NUM;
	else
		rc = MC_RESULT_NOT_FOUND;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_decr(mm_value_t arg)
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
		mc_entry_setmisc(new_entry, key, old_entry->flags, hash);

		struct mc_entry *old_entry2 = mc_table_remove(part, hash, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(part, hash, new_entry);
	}

	mc_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else if (new_entry != NULL)
		rc = mc_command_entry(command, new_entry, MC_RESULT_VALUE);
	else if (old_entry != NULL)
		rc = MC_RESULT_INC_DEC_NON_NUM;
	else
		rc = MC_RESULT_NOT_FOUND;

	mc_table_unlock(part);

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_delete(mm_value_t arg)
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

	mc_result_t rc;
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
mc_process_touch(mm_value_t arg __attribute__((unused)))
{
	return MC_RESULT_NOT_IMPLEMENTED;
}

static mm_value_t
mc_process_slabs(mm_value_t arg __attribute__((unused)))
{
	return MC_RESULT_NOT_IMPLEMENTED;
}

static mm_value_t
mc_process_stats(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	mc_result_t rc;
	if (command->params.stats.nopts)
		rc = MC_RESULT_NOT_IMPLEMENTED;
	else
		rc = MC_RESULT_END;

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_flush(mm_value_t arg)
{
	ENTER();

	struct mc_tpart *part = &mc_table.parts[arg];
	while (mc_table_evict(part, 256))
		mm_task_yield();

	LEAVE();
	return 0;
}

static mm_value_t
mc_process_flush_all(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	// TODO: really use the exptime.
	mc_exptime = mc_curtime + command->params.val32 * 1000000ull;

	for (mm_core_t i = 0; i < mc_table.nparts; i++) {
#if ENABLE_MEMCACHE_LOCKS
		mm_core_post(MM_CORE_NONE, mc_process_flush, i);
#else
		struct mc_tpart *part = &mc_table.parts[i];
		mm_core_post(part->core, mc_process_flush, i);
#endif
	}

	mc_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_OK;

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_version(mm_value_t arg __attribute__((unused)))
{
	return MC_RESULT_VERSION;
}

static mm_value_t
mc_process_verbosity(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	mc_verbose = min(command->params.val32, 2u);
	DEBUG("set verbosity %d", mc_verbose);

	mc_result_t rc;
	if (command->noreply)
		rc = MC_RESULT_BLANK;
	else
		rc = MC_RESULT_OK;

	LEAVE();
	return rc;
}

static mm_value_t
mc_process_quit(mm_value_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mm_net_shutdown_reader(command->params.sock);

	LEAVE();
	return MC_RESULT_QUIT;
}

static void
mc_process_start(struct mc_command *command)
{
	if (command->result_type != MC_RESULT_NONE)
		return;

	if ((command->type->flags & MC_ASYNC) != 0) {
		command->key_hash = mc_hash(command->key.str, command->key.len);

#if ENABLE_SMP && !ENABLE_MEMCACHE_LOCKS
		struct mc_tpart *part = mc_table_part(command->key_hash);
		command->result_type = MC_RESULT_FUTURE;
		command->future = mm_future_create(command->type->process,
						   (mm_value_t) command);
		mm_future_start(command->future, part->core);
		return;
#endif
	}

	command->result_type = (command->type->process)((mm_value_t) command);
}

static mm_value_t
mc_process_command(struct mc_state *state, struct mc_command *first)
{
	ENTER();
 
	struct mc_command *last = first;
	if (likely(first->type != NULL)) {
		DEBUG("command %s", mc_command_name(first->type->tag));
		for (;;) {
			mc_process_start(last);
			if (last->next == NULL)
				break;
			last = last->next;
		}
	}

	mc_queue_command(state, first, last);
	mm_net_spawn_writer(&state->sock);

	LEAVE();
	return 0;
}

/**********************************************************************
 * Receiving commands.
 **********************************************************************/

static ssize_t
mc_read(struct mc_state *state, size_t required, size_t optional)
{
	ENTER();

	size_t total = required + optional;
	mm_buffer_demand(&state->rbuf, total);

	size_t count = total;
	while (count > optional) {
		ssize_t n = mm_net_readbuf(&state->sock, &state->rbuf);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				state->error = true;
			break;
		}
		if (count < (size_t) n) {
			count = 0;
			break;
		}
		count -= n;
	}

	LEAVE();
	return (total - count);
}

/**********************************************************************
 * Command Parsing.
 **********************************************************************/

#define MC_KEY_LEN_MAX		250

#define MC_BINARY_REQ		0x80
#define MC_BINARY_RES		0x81

struct mc_parser
{
	struct mm_buffer_cursor cursor;
	struct mc_command *command;
	struct mc_state *state;
};

static inline bool
mc_cursor_contains(struct mm_buffer_cursor *cur, const char *ptr)
{
	return ptr >= cur->ptr && ptr < cur->end;
}

/*
 * Prepare for parsing a command.
 */
static void
mc_start_input(struct mc_parser *parser, struct mc_state *state)
{
	ENTER();
	DEBUG("Start parser.");

	mm_buffer_first_out(&state->rbuf, &parser->cursor);
	if (state->start_ptr != NULL) {
		while (!mc_cursor_contains(&parser->cursor, state->start_ptr)) {
			mm_buffer_next_out(&state->rbuf, &parser->cursor);
		}
		if (parser->cursor.ptr < state->start_ptr) {
			parser->cursor.ptr = state->start_ptr;
		}
	}

	parser->state = state;
	parser->command = NULL;

	LEAVE();
}

static bool
mc_parse_lf(struct mc_parser *parser, char *s)
{
	ASSERT(mc_cursor_contains(&parser->cursor, s));

	if ((s + 1) < parser->cursor.end)
		return *(s + 1) == '\n';

	struct mm_buffer_segment *seg = parser->cursor.seg;
	if (seg != parser->state->rbuf.in_seg) {
		seg = seg->next;
		if (seg != parser->state->rbuf.in_seg || parser->state->rbuf.in_off) {
			return seg->data[0] == '\n';
		}
	}

	return false;
}

static bool
mc_parse_value(struct mc_parser *parser)
{
	ENTER();

	bool rc = true;
	uint32_t bytes = parser->command->params.set.bytes;

	// Store the start position.
  	parser->command->params.set.seg = parser->cursor.seg;
	parser->command->params.set.start = parser->cursor.ptr;

	for (;;) {
		uint32_t avail = parser->cursor.end - parser->cursor.ptr;
		DEBUG("parse data: avail = %ld, bytes = %ld", (long) avail, (long) bytes);
		if (avail > bytes) {
			parser->cursor.ptr += bytes;
			break;
		}

		parser->cursor.ptr += avail;
		bytes -= avail;

		if (!mm_buffer_next_out(&parser->state->rbuf, &parser->cursor)) {
			// Try to read the value and required LF and optional CR.
			ssize_t r = bytes + 1;
			ssize_t n = mc_read(parser->state, r, 1);
			if (n < r) {
				rc = false;
				break;
			}
			mm_buffer_size_out(&parser->state->rbuf, &parser->cursor);
		}
	}

	LEAVE();
	return rc;
}

static bool
mc_parse(struct mc_parser *parser)
{
	ENTER();

	enum parse_state {
		S_START,
		S_CMD_1,
		S_CMD_2,
		S_CMD_3,
		S_MATCH,
		S_SPACE,
		S_KEY,
		S_KEY_N,
		S_KEY_EDGE,
		S_KEY_COPY,
		S_NUM32,
		S_NUM32_N,
		S_NUM64,
		S_NUM64_N,
		S_GET_1,
		S_GET_N,
		S_SET_1,
		S_SET_2,
		S_SET_3,
		S_SET_4,
		S_SET_5,
		S_SET_6,
		S_CAS,
		S_ARITH_1,
		S_ARITH_2,
		S_DELETE_1,
		S_DELETE_2,
		S_TOUCH_1,
		S_TOUCH_2,
		S_FLUSH_ALL_1,
		S_VERBOSITY_1,
		S_VAL32,
		S_VAL64,
		S_NOREPLY,
		S_OPT,
		S_OPT_N,
		S_VALUE,
		S_VALUE_1,
		S_VALUE_2,
		S_EOL,
		S_EOL_1,
		S_ERROR,
		S_ERROR_1,

		S_ABORT
	};

	// Initialize the result.
	bool rc = true;

	// Initialize the scanner state.
	enum parse_state state = S_START;
	enum parse_state shift = S_ABORT;
	uint32_t start = -1;
	uint32_t num32 = 0;
	uint64_t num64 = 0;
	char *match = "";

	// The current command.
	struct mc_command *command = mc_command_create(&parser->state->sock);
	parser->command = command;

	// The count of scanned chars. Used to check if the client sends
	// too much junk data.
	int count = 0;

	do {
		// Get the input buffer position.
		char *s = parser->cursor.ptr;
		char *e = parser->cursor.end;
		DEBUG("'%.*s'", (int) (e - s), s);

		for (; s < e; s++) {
			int c = *s;	
again:
			switch (state) {
			case S_START:
				if (c == ' ') {
					// Skip space.
					break;
				} else if (c == '\n') {
					DEBUG("Unexpected line end.");
					state = S_ERROR;
					goto again;
				} else {
					// Store the first command char.
					start = c << 24;
					state = S_CMD_1;
					break;
				}

			case S_CMD_1:
				// Store the second command char.
				if (unlikely(c == '\n')) {
					DEBUG("Unexpected line end.");
					state = S_ERROR;
					goto again;
				} else {
					start |= c << 16;
					state = S_CMD_2;
					break;
				}

			case S_CMD_2:
				// Store the third command char.
				if (unlikely(c == '\n')) {
					DEBUG("Unexpected line end.");
					state = S_ERROR;
					goto again;
				} else {
					start |= c << 8;
					state = S_CMD_3;
					break;
				}

			case S_CMD_3:
				// Have the first 4 chars of the command,
				// it is enough to learn what it is.
				start |= c;

#define Cx4(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
				if (start == Cx4('g', 'e', 't', ' ')) {
					command->type = &mc_desc_get;
					state = S_SPACE;
					shift = S_GET_1;
					break;
				} else if (start == Cx4('s', 'e', 't', ' ')) {
					command->type = &mc_desc_set;
					state = S_SPACE;
					shift = S_SET_1;
					break;
				} else if (start == Cx4('r', 'e', 'p', 'l')) {
					command->type = &mc_desc_replace;
					state = S_MATCH;
					match = "ace";
					shift = S_SET_1;
					break;
				} else if (start == Cx4('d', 'e', 'l', 'e')) {
					command->type = &mc_desc_delete;
					state = S_MATCH;
					match = "te";
					shift = S_DELETE_1;
					break;
				} else if (start == Cx4('a', 'd', 'd', ' ')) {
					command->type = &mc_desc_add;
					state = S_SPACE;
					shift = S_SET_1;
					break;
				} else if (start == Cx4('i', 'n', 'c', 'r')) {
					command->type = &mc_desc_incr;
					state = S_MATCH;
					//match = "";
					shift = S_ARITH_1;
					break;
				} else if (start == Cx4('d', 'e', 'c', 'r')) {
					command->type = &mc_desc_decr;
					state = S_MATCH;
					//match = "";
					shift = S_ARITH_1;
					break;
				} else if (start == Cx4('g', 'e', 't', 's')) {
					command->type = &mc_desc_gets;
					state = S_MATCH;
					//match = "";
					shift = S_GET_1;
					break;
				} else if (start == Cx4('c', 'a', 's', ' ')) {
					command->type = &mc_desc_cas;
					state = S_SPACE;
					shift = S_SET_1;
					break;
				} else if (start == Cx4('a', 'p', 'p', 'e')) {
					command->type = &mc_desc_append;
					state = S_MATCH;
					match = "nd";
					shift = S_SET_1;
					break;
				} else if (start == Cx4('p', 'r', 'e', 'p')) {
					command->type = &mc_desc_prepend;
					state = S_MATCH;
					match = "end";
					shift = S_SET_1;
					break;
				} else if (start == Cx4('t', 'o', 'u', 'c')) {
					command->type = &mc_desc_touch;
					state = S_MATCH;
					match = "h";
					break;
				} else if (start == Cx4('s', 'l', 'a', 'b')) {
					command->type = &mc_desc_slabs;
					state = S_MATCH;
					match = "s";
					shift = S_OPT;
					break;
				} else if (start == Cx4('s', 't', 'a', 't')) {
					command->type = &mc_desc_stats;
					state = S_MATCH;
					match = "s";
					shift = S_OPT;
					break;
				} else if (start == Cx4('f', 'l', 'u', 's')) {
					command->type = &mc_desc_flush_all;
					state = S_MATCH;
					match = "h_all";
					shift = S_FLUSH_ALL_1;
					break;
				} else if (start == Cx4('v', 'e', 'r', 's')) {
					command->type = &mc_desc_version;
					state = S_MATCH;
					match = "ion";
					shift = S_EOL;
					break;
				} else if (start == Cx4('v', 'e', 'r', 'b')) {
					command->type = &mc_desc_verbosity;
					state = S_MATCH;
					match = "osity";
					shift = S_VERBOSITY_1;
					break;
				} else if (start == Cx4('q', 'u', 'i', 't')) {
					command->type = &mc_desc_quit;
					command->params.sock = &parser->state->sock;
					state = S_SPACE;
					shift = S_EOL;
					break;
				} else {
					DEBUG("Unrecognized command.");
					state = S_ERROR;
					goto again;
				}
#undef Cx4

			case S_MATCH:
				if (c == *match) {
					// So far so good.
					if (unlikely(c == 0)) {
						// Hmm, zero byte in the input.
						state = S_ERROR;
						break;
					}
					match++;
					break;
				} else if (unlikely(*match)) {
					// Unexpected char before the end.
					state = S_ERROR;
					goto again;
				} else if (c == ' ') {
					// It matched.
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					// It matched as well.
					state = shift;
					goto again;
				} else {
					DEBUG("Unexpected char after the end.");
					state = S_ERROR;
					break;
				}

			case S_SPACE:
				if (c == ' ') {
					// Skip space.
					break;
				} else {
					state = shift;
					goto again;
				}

			case S_KEY:
				ASSERT(c != ' ');
				if ((c == '\r' && mc_parse_lf(parser, s)) || c == '\n') {
					DEBUG("Missing key.");
					state = S_ERROR;
					goto again;
				} else {
					state = S_KEY_N;
					command->key.str = s;
					break;
				}

			case S_KEY_N:
				if (c == ' ') {
					size_t len = s - command->key.str;
					if (len > MC_KEY_LEN_MAX) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						state = S_SPACE;
						command->key.len = len;
					}
					break;
				} else if ((c == '\r' && mc_parse_lf(parser, s)) || c == '\n') {
					size_t len = s - command->key.str;
					if (len > MC_KEY_LEN_MAX) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						state = shift;
						command->key.len = len;
					}
					goto again;
				} else {
					// Move over to the next char.
					break;
				}

			case S_KEY_EDGE:
				if (c == ' ') {
					state = S_SPACE;
					command->key.len = MC_KEY_LEN_MAX;
					break;
				} else if ((c == '\r' && mc_parse_lf(parser, s)) || c == '\n') {
					state = shift;
					command->key.len = MC_KEY_LEN_MAX;
					goto again;
				} else {
					DEBUG("Too long key.");
					state = S_ERROR;
					break;
				}

			case S_KEY_COPY:
				if (c == ' ') {
					state = S_SPACE;
					break;
				} else if ((c == '\r' && mc_parse_lf(parser, s)) || c == '\n') {
					state = shift;
					goto again;
				} else {
					struct mc_string *key = &command->key;
					if (key->len == MC_KEY_LEN_MAX) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						char *str = (char *) key->str;
						str[key->len++] = c;
					}
					break;
				}

			case S_NUM32:
				ASSERT(c != ' ');
				if (c >= '0' && c <= '9') {
					state = S_NUM32_N;
					num32 = c - '0';
					break;
				} else {
					state = S_ERROR;
					goto again;
				}

			case S_NUM32_N:
				if (c >= '0' && c <= '9') {
					// TODO: overflow check?
					num32 = num32 * 10 + (c - '0');
					break;
				} else if (c == ' ') {
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					state = shift;
					goto again;
				} else {
					state = S_ERROR;
					break;
				}

			case S_NUM64:
				ASSERT(c != ' ');
				if (c >= '0' && c <= '9') {
					state = S_NUM64_N;
					num64 = c - '0';
					break;
				} else {
					state = S_ERROR;
					goto again;
				}

			case S_NUM64_N:
				if (c >= '0' && c <= '9') {
					// TODO: overflow check?
					num64 = num64 * 10 + (c - '0');
					break;
				} else if (c == ' ') {
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					state = shift;
					goto again;
				} else {
					state = S_ERROR;
					break;
				}

			case S_GET_1:
				state = S_KEY;
				shift = S_GET_N;
				goto again;

			case S_GET_N:
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
					state = S_EOL;
					command->params.last = true;
					goto again;
				} else {
					state = S_KEY;
					command->end_ptr = s;
					command->next = mc_command_create(&parser->state->sock);
					command->next->type = command->type;
					command = command->next;
					goto again;
				}

			case S_SET_1:
				state = S_KEY;
				shift = S_SET_2;
				goto again;

			case S_SET_2:
				state = S_NUM32;
				shift = S_SET_3;
				goto again;

			case S_SET_3:
				command->params.set.flags = num32;
				state = S_NUM32;
				shift = S_SET_4;
				goto again;

			case S_SET_4:
				command->params.set.exptime = num32;
				state = S_NUM32;
				shift = S_SET_5;
				goto again;

			case S_SET_5:
				command->params.set.bytes = num32;
				if (command->type->tag == mc_command_cas) {
					state = S_NUM64;
					shift = S_CAS;
					goto again;
				} else if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_SET_6;
					break;
				} else {
					state = S_VALUE;
					goto again;
				}

			case S_SET_6:
				command->noreply = true;
				state = S_VALUE;
				goto again;

			case S_CAS:
				command->params.set.cas = num64;
				ASSERT(c != ' ');
				if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_SET_6;
					break;
				} else {
					state = S_VALUE;
					goto again;
				}

			case S_ARITH_1:
				state = S_KEY;
				shift = S_ARITH_2;
				goto again;

			case S_ARITH_2:
				state = S_NUM64;
				shift = S_VAL64;
				goto again;

			case S_DELETE_1:
				state = S_KEY;
				shift = S_DELETE_2;
				goto again;

			case S_DELETE_2:
				ASSERT(c != ' ');
				if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_NOREPLY;
					break;
				} else {
					state = S_EOL;
					goto again;
				}

			case S_TOUCH_1:
				state = S_KEY;
				shift = S_TOUCH_2;
				goto again;

			case S_TOUCH_2:
				state = S_NUM32;
				shift = S_VAL32;
				goto again;

			case S_FLUSH_ALL_1:
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
					state = S_EOL;
					goto again;
				} else if (c >= '0' && c <= '9') {
					state = S_NUM32;
					shift = S_VAL32;
					goto again;
				} else if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_NOREPLY;
					break;
				} else {
					state = S_ERROR;
					goto again;
				}

			case S_VERBOSITY_1:
				ASSERT(c != ' ');
				if (c >= '0' && c <= '9') {
					state = S_NUM32;
					shift = S_VAL32;
					goto again;
				} else {
					state = S_ERROR;
					goto again;
				}

			case S_VAL32:
				command->params.val32 = num32;
				ASSERT(c != ' ');
				if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_NOREPLY;
					break;
				} else {
					state = S_EOL;
					goto again;
				}

			case S_VAL64:
				command->params.val64 = num64;
				ASSERT(c != ' ');
				if (c == 'n') {
					state = S_MATCH;
					match = "oreply";
					shift = S_NOREPLY;
					break;
				} else {
					state = S_EOL;
					goto again;
				}

			case S_NOREPLY:
				command->noreply = true;
				state = S_EOL;
				goto again;

			case S_OPT:
				if (c == '\r' || c == '\n') {
					state = S_EOL;
					goto again;
				} else {
					// TODO: add c to the option value
					state = S_OPT_N;
					break;
				}

			case S_OPT_N:
				// TODO: limit the option number
				// TODO: use the option value
				if (c == ' ') {
					mc_command_option(command);
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					mc_command_option(command);
					state = S_EOL;
					goto again;
				} else {
					// TODO: add c to the option value
					break;
				}

			case S_VALUE:
				ASSERT(c != ' ');
				if (c == '\r') {
					state = S_VALUE_1;
					break;
				}
				// FALLTHRU
			case S_VALUE_1:
				if (c == '\n') {
					state = S_VALUE_2;
					break;
				} else {
					state = S_ERROR;
					break;
				}

			case S_VALUE_2:
				parser->cursor.ptr = s;
				rc = mc_parse_value(parser);
				if (!rc)
					goto leave;
				s = parser->cursor.ptr;
				e = parser->cursor.end;
				state = S_EOL;
				break;

			case S_EOL:
				ASSERT(c != ' ');
				if (c == '\r') {
					state = S_EOL_1;
					break;
				}
				// FALLTHRU
			case S_EOL_1:
				if (c == '\n') {
					parser->cursor.ptr = s + 1;
					command->end_ptr = parser->cursor.ptr;
					goto leave;
				} else {
					state = S_ERROR;
					break;
				}

			case S_ERROR:
				if (parser->command->next != NULL) {
					command = parser->command->next;
					do {
						struct mc_command *tmp = command;
						command = command->next;
						mc_command_destroy(&parser->state->sock, tmp);
					} while (command != NULL);

					parser->command->next = NULL;
					command = parser->command;
				}
				state = S_ERROR_1;
				// FALLTHRU
			case S_ERROR_1:
				if (c == '\n') {
					parser->cursor.ptr = s + 1;
					command->end_ptr = parser->cursor.ptr;
					command->result_type = MC_RESULT_ERROR;
					goto leave;
				} else {
					// Skip char.
					break;
				}

			case S_ABORT:
				ABORT();
			}
		}

		count += e - parser->cursor.ptr;
		if (unlikely(count > 1024)) {
			bool too_much = true;
			if (command->type != NULL
			    && (command->type->tag == mc_command_get
			        || command->type->tag == mc_command_gets)
			    && count < (16 * 1024))
				too_much = false;

			// The client looks insane. Quit fast.
			if (too_much) {
				parser->state->trash = true;
				goto leave;
			}
		}

		if (state == S_KEY_N) {
			DEBUG("Split key.");

			size_t len = e - command->key.str;
			if (len > MC_KEY_LEN_MAX) {
				DEBUG("Too long key.");
				state = S_ERROR;
			} else if (len == MC_KEY_LEN_MAX) {
				state = S_KEY_EDGE;
			} else {
				state = S_KEY_COPY;

				char *str = mm_local_alloc(MC_KEY_LEN_MAX);
				memcpy(str, command->key.str, len);
				command->key.len = len;
				command->key.str = str;
				command->own_key = true;
			}
		}

		rc = mm_buffer_next_out(&parser->state->rbuf, &parser->cursor);

	} while (rc);

leave:
	LEAVE();
	return rc;
}

/**********************************************************************
 * Transmitting command results.
 **********************************************************************/

static void
mc_transmit_unref(uintptr_t data)
{
	ENTER();

	struct mc_entry *entry = (struct mc_entry *) data;
	mc_entry_unref(entry);

	LEAVE();
}

static void
mc_transmit(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	switch (mc_command_result(command)) {

#define SL(x) x, (sizeof (x) - 1)

	case MC_RESULT_BLANK:
		break;

	case MC_RESULT_OK:
		mm_buffer_append(&state->tbuf, SL("OK\r\n"));
		break;

	case MC_RESULT_END:
		mm_buffer_append(&state->tbuf, SL("END\r\n"));
		break;

	case MC_RESULT_ERROR:
		mm_buffer_append(&state->tbuf, SL("ERROR\r\n"));
		break;

	case MC_RESULT_EXISTS:
		mm_buffer_append(&state->tbuf, SL("EXISTS\r\n"));
		break;

	case MC_RESULT_STORED:
		mm_buffer_append(&state->tbuf, SL("STORED\r\n"));
		break;

	case MC_RESULT_DELETED:
		mm_buffer_append(&state->tbuf, SL("DELETED\r\n"));
		break;

	case MC_RESULT_NOT_FOUND:
		mm_buffer_append(&state->tbuf, SL("NOT_FOUND\r\n"));
		break;

	case MC_RESULT_NOT_STORED:
		mm_buffer_append(&state->tbuf, SL("NOT_STORED\r\n"));
		break;

	case MC_RESULT_INC_DEC_NON_NUM:
		mm_buffer_append(&state->tbuf, SL("CLIENT_ERROR cannot increment or decrement non-numeric value\r\n"));
		break;

	case MC_RESULT_NOT_IMPLEMENTED:
		mm_buffer_append(&state->tbuf, SL("SERVER_ERROR not implemented\r\n"));
		break;

	case MC_RESULT_CANCELED:
		mm_buffer_append(&state->tbuf, SL("SERVER_ERROR command canceled\r\n"));
		break;

	case MC_RESULT_VERSION:
		mm_buffer_append(&state->tbuf, SL(MC_VERSION));
		break;

#undef SL

	case MC_RESULT_ENTRY:
	case MC_RESULT_ENTRY_CAS: {
		struct mc_entry *entry = command->entry;
		const char *key = mc_entry_getkey(entry);
		char *value = mc_entry_getvalue(entry);
		uint8_t key_len = entry->key_len;
		uint32_t value_len = entry->value_len;

		if (command->result_type == MC_RESULT_ENTRY) {
			mm_buffer_printf(
				&state->tbuf,
				"VALUE %.*s %u %u\r\n",
				key_len, key,
				entry->flags, value_len);
		} else {
			mm_buffer_printf(
				&state->tbuf,
				"VALUE %.*s %u %u %llu\r\n",
				key_len, key,
				entry->flags, value_len,
				(unsigned long long) entry->cas);
		}

		mc_entry_ref(entry);
		mm_buffer_splice(&state->tbuf, value, value_len,
				 mc_transmit_unref, (uintptr_t) entry);

		if (command->params.last)
			mm_buffer_append(&state->tbuf, "\r\nEND\r\n", 7);
		else
			mm_buffer_append(&state->tbuf, "\r\n", 2);
		break;
	}

	case MC_RESULT_VALUE: {
		struct mc_entry *entry = command->entry;
		char *value = mc_entry_getvalue(entry);
		uint32_t value_len = entry->value_len;

		mc_entry_ref(entry);
		mm_buffer_splice(&state->tbuf, value, value_len,
				 mc_transmit_unref, (uintptr_t) entry);

		mm_buffer_append(&state->tbuf, "END\r\n", 5);
		break;
	}

	case MC_RESULT_QUIT:
		mm_net_close(&state->sock);
		break;

	default:
		ABORT();
	}

	LEAVE();
}

static void
mc_transmit_flush(struct mc_state *state)
{
	ENTER();

	ssize_t n = mm_net_writebuf(&state->sock, &state->tbuf);
	if (n > 0)
		mm_buffer_rectify(&state->tbuf);

	LEAVE();
}

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		10000

static struct mm_net_socket *
mc_alloc(void)
{
	ENTER();

	struct mc_state *state = mm_shared_alloc(sizeof(struct mc_state));

	LEAVE();
	return &state->sock;
}

static void
mc_free(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	mm_shared_free(state);

	LEAVE();
}

static void
mc_prepare(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	state->start_ptr = NULL;

	state->command_head = NULL;
	state->command_tail = NULL;

	mm_buffer_prepare(&state->rbuf);
	mm_buffer_prepare(&state->tbuf);

	state->error = false;
	state->trash = false;

	LEAVE();
}

static void
mc_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	while (state->command_head != NULL) {
		struct mc_command *command = state->command_head;
		state->command_head = command->next;
		mc_command_destroy(sock, command);
	}

	mm_buffer_cleanup(&state->rbuf);
	mm_buffer_cleanup(&state->tbuf);

	LEAVE();
}

static void
mc_reader_routine(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	// Reset the buffer state.
	if (mm_buffer_empty(&state->rbuf)) {
		mm_buffer_rectify(&state->rbuf);
		state->start_ptr = NULL;
	}

	// Try to get some input w/o blocking.
	mm_net_set_read_timeout(&state->sock, 0);
	ssize_t n = mc_read(state, 1, 0);
	mm_net_set_read_timeout(&state->sock, MC_READ_TIMEOUT);

retry:
	// Get out of here if there is no more input available.
	if (n <= 0) {
		// If the socket is closed queue a quit command.
		if (state->error && !mm_net_is_reader_shutdown(sock)) {
			struct mc_command *command = mc_command_create(sock);
			command->type = &mc_desc_quit;
			command->params.sock = sock;
			command->end_ptr = state->start_ptr;
			mc_process_command(state, command);
		}
		goto leave;
	}

	// Initialize the parser.
	struct mc_parser parser;
	mc_start_input(&parser, state);

parse:
	// Try to parse the received input.
	if (!mc_parse(&parser)) {
		if (parser.command != NULL) {
			mc_command_destroy(sock, parser.command);
			parser.command = NULL;
		}
		if (state->trash) {
			mm_net_close(&state->sock);
			goto leave;
		}

		// The input is incomplete, try to get some more.
		n = mc_read(state, 1, 0);
		goto retry;
	}

	// Mark the parsed input as consumed.
	state->start_ptr = parser.cursor.ptr;

	// Process the parsed command.
	mc_process_command(state, parser.command);

	// If there is more input in the buffer then try to parse the next
	// command.
	if (!mm_buffer_depleted(&state->rbuf, &parser.cursor))
		goto parse;

leave:
	LEAVE();
}

static void
mc_writer_routine(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = containerof(sock, struct mc_state, sock);

	// Check to see if there at least one ready result.
	struct mc_command *command = state->command_head;
	if (unlikely(command == NULL))
		goto leave;

	// Put the results into the transmit buffer.
	for (;;) {
		mc_transmit(state, command);

		struct mc_command *next = command->next;
		if (next == NULL)
			break;

		command = next;
	}

	// Transmit buffered results.
	mc_transmit_flush(state);

	// Free the receive buffers.
	mc_release_buffers(state, command->end_ptr);

	// Release the command data
	for (;;) {
		struct mc_command *head = state->command_head;
		state->command_head = head->next;
		if (state->command_head == NULL)
			state->command_tail = NULL;

		mc_command_destroy(sock, head);

		if (head == command) {
			break;
		}
	}

leave:
	LEAVE();
}

/**********************************************************************
 * Module Entry Points.
 **********************************************************************/

// TCP memcache server.
static struct mm_net_server *mc_tcp_server;

static void
mc_memcache_start(void)
{
	ENTER();

	mc_table_init(&mc_config);
	mc_command_start();
	mm_net_start_server(mc_tcp_server);

	LEAVE();
}

static void
mc_memcache_stop(void)
{
	ENTER();

	mm_net_stop_server(mc_tcp_server);
	mc_command_stop();
	mc_table_term();

	LEAVE();
}

void
mm_memcache_init(const struct mm_memcache_config *config)
{
	ENTER();

	static struct mm_net_proto proto = {
		.flags = MM_NET_INBOUND,
		.alloc = mc_alloc,
		.free = mc_free,
		.prepare = mc_prepare,
		.cleanup = mc_cleanup,
		.reader = mc_reader_routine,
		.writer = mc_writer_routine,
	};

	mc_tcp_server = mm_net_create_inet_server("memcache", &proto,
						  "127.0.0.1", 11211);

#if ENABLE_MEMCACHE_LOCKS
	if (config != NULL && config->nparts)
		mc_config.nparts = config->nparts;
	else
		mc_config.nparts = 1;
#else
	mm_bitset_prepare(&mc_config.affinity, &mm_alloc_global, mm_core_getnum());
	if (config != NULL)
		mm_bitset_or(&mc_config.affinity, &config->affinity);
	if (!mm_bitset_any(&mc_config.affinity))
		mm_bitset_set(&mc_config.affinity, 0);
#endif

	mm_core_hook_start(mc_memcache_start);
	mm_core_hook_stop(mc_memcache_stop);

	LEAVE();
}
