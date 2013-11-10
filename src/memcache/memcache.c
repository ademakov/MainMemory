/*
 * memcache.c - MainMemory memcached protocol support.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#include "../alloc.h"
#include "../buffer.h"
#include "../chunk.h"
#include "../core.h"
#include "../future.h"
#include "../list.h"
#include "../log.h"
#include "../net.h"
#include "../pool.h"
#include "../trace.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define MC_VERSION "VERSION MainMemory 0.0\r\n"

// The logging verbosity level.
static uint8_t mc_verbose = 0;

static mm_timeval_t mc_curtime;
static mm_timeval_t mc_exptime;

/**********************************************************************
 * Hash function.
 **********************************************************************/

/*
 * The Fowler/Noll/Vo (FNV) hash function, variant 1a.
 * 
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html
 */

#define FNV1_32_INIT ((uint32_t) 0x811c9dc5)
#define FNV_32_PRIME ((uint32_t) 0x01000193)

static uint32_t
mc_hash(const void *data, size_t size)
{
	const unsigned char *p = (unsigned char *) data;
	const unsigned char *e = (unsigned char *) data + size;

	uint32_t h = FNV1_32_INIT;
	while (p < e) {
		h ^= (uint32_t) *p++;
		h *= FNV_32_PRIME;
	}

	return h;
}

/**********************************************************************
 * Memcache Entry.
 **********************************************************************/

struct mc_entry
{
	struct mc_entry *next;
	struct mm_list link;
	uint8_t key_len;
	uint32_t value_len;
	uint32_t ref_count;
	uint32_t flags;
	uint64_t cas;
	char data[];
};


static inline size_t
mc_entry_size(uint8_t key_len, size_t value_len)
{
	return sizeof(struct mc_entry) + key_len + value_len;
}

static inline char *
mc_entry_key(struct mc_entry *entry)
{
	return entry->data;
}

static inline char *
mc_entry_value(struct mc_entry *entry)
{
	return entry->data + entry->key_len;
}

static inline void
mc_entry_set_key(struct mc_entry *entry, const char *key)
{
	char *entry_key = mc_entry_key(entry);
	memcpy(entry_key, key, entry->key_len);
}

static inline void
mc_entry_set_value(struct mc_entry *entry, const char *value)
{
	char *entry_value = mc_entry_value(entry);
	memcpy(entry_value, value, entry->value_len);
}

static struct mc_entry *
mc_entry_create(uint8_t key_len, size_t value_len)
{
	ENTER();
	DEBUG("key_len = %d, value_len = %ld", key_len, (long) value_len);

	size_t size = mc_entry_size(key_len, value_len);
	struct mc_entry *entry = mm_alloc(size);
	entry->key_len = key_len;
	entry->value_len = value_len;
	entry->ref_count = 1;

	static uint64_t cas = 0;
	entry->cas = ++cas;

	LEAVE();
	return entry;
}

static void
mc_entry_destroy(struct mc_entry *entry)
{
	ENTER();

	mm_free(entry);

	LEAVE();
}

static void
mc_entry_ref(struct mc_entry *entry)
{
	uint32_t ref_count = ++(entry->ref_count);
	if (unlikely(ref_count == 0)) {
		ABORT();
	}
}

static void
mc_entry_unref(struct mc_entry *entry)
{
	uint32_t ref_count = --(entry->ref_count);
	if (unlikely(ref_count == 0)) {
		mc_entry_destroy(entry);
	}
}

static bool
mc_entry_value_u64(struct mc_entry *entry, uint64_t *value)
{
	if (entry->value_len == 0) {
		return false;
	}

	char *p = mc_entry_value(entry);
	char *e = p + entry->value_len;

	uint64_t v = 0;
	while (p < e) {
		int c = *p++;
		if (!isdigit(c)) {
			return false;
		}

		uint64_t vv = v * 10 + c - '0';
		if (unlikely(vv < v)) {
			return false;
		}

		v = vv;
	}

	*value = v;
	return true;
}

static struct mc_entry *
mc_entry_create_u64(uint8_t key_len, uint64_t value)
{
	char buffer[32];

	size_t value_len = 0;
	do {
		int c = (int) (value % 10);
		buffer[value_len++] = '0' + c;
		value /= 10;
	} while (value);

	struct mc_entry *entry = mc_entry_create(key_len, value_len);
	char *v = mc_entry_value(entry);
	do {
		size_t i = entry->value_len - value_len--;
		v[i] = buffer[value_len];
	} while (value_len);

	return entry;
}

/**********************************************************************
 * Memcache Table.
 **********************************************************************/

#define MC_TABLE_STRIDE		64

#define MC_TABLE_SIZE_MIN	((size_t) 4 * 1024)

#if 0
# define MC_TABLE_SIZE_MAX	((size_t) 64 * 1024 * 1024)
#else
# define MC_TABLE_SIZE_MAX	((size_t) 512 * 1024 * 1024)
#endif

struct mc_table
{
	uint32_t mask;
	uint32_t size;
	uint32_t used;

	bool striding;

	size_t nentries;

	struct mc_entry **table;
};

static struct mc_table mc_table;
static struct mm_list mc_entry_list;

static mm_result_t mc_table_stride_routine(uintptr_t);

static inline size_t
mc_table_size(size_t nbuckets)
{
	return nbuckets * sizeof (struct mc_entry *);
}

static inline uint32_t
mc_table_index(uint32_t h)
{
	uint32_t mask = mc_table.mask;
	uint32_t index = h & mask;
	if (index >= mc_table.used)
		index &= mask >> 1;
	return index;
}

static inline uint32_t
mc_table_key_index(const char *key, uint8_t key_len)
{
	return mc_table_index(mc_hash(key, key_len));
}

static inline bool
mc_table_is_full(void)
{
	if (unlikely(mc_table.size == MC_TABLE_SIZE_MAX)
	    && unlikely(mc_table.used == mc_table.size))
		return false;
	return mc_table.nentries > (mc_table.size * 4);
}

static void
mc_table_expand(size_t size)
{
	ENTER();
	ASSERT(size > mc_table.size);
	/* Assert the size is a power of 2. */
	ASSERT((size & (size - 1)) == 0);

	mm_brief("Set the memcache table size: %ld", (unsigned long) size);

	size_t old_size = mc_table_size(mc_table.size);
	size_t new_size = mc_table_size(size);

	void *address = (char *) mc_table.table + old_size;
	size_t nbytes = new_size - old_size;

	void *area = mmap(address, nbytes, PROT_READ | PROT_WRITE,
			  MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	if (area == MAP_FAILED)
		mm_fatal(errno, "mmap");
	if (area != address)
		mm_fatal(0, "mmap returned wrong address");

	mc_table.size = size;
	mc_table.mask = size - 1;

	LEAVE();
}

static void
mc_table_stride(void)
{
	ENTER();
	ASSERT(mc_table.used < mc_table.size);
	ASSERT(mc_table.used >= mc_table.size / 2);
	ASSERT((mc_table.used + MC_TABLE_STRIDE) <= mc_table.size);

	uint32_t mask = mc_table.mask;
	uint32_t target = mc_table.used;
	uint32_t source = target - mc_table.size / 2;

	for (uint32_t count = 0; count < MC_TABLE_STRIDE; count++) {
		struct mc_entry *entry = mc_table.table[source];

		struct mc_entry *s_entries = NULL;
		struct mc_entry *t_entries = NULL;
		while (entry != NULL) {
			struct mc_entry *next = entry->next;

			uint32_t h = mc_hash(entry->data, entry->key_len);
			uint32_t index = h & mask;
			if (index == source) {
				entry->next = s_entries;
				s_entries = entry;
			} else {
				ASSERT(index == target);
				entry->next = t_entries;
				t_entries = entry;
			}

			entry = next;
		}

		mc_table.table[source++] = s_entries;
		mc_table.table[target++] = t_entries;
	}

	mc_table.used += MC_TABLE_STRIDE;

	LEAVE();
}

static void
mc_table_start_striding(void)
{
	ENTER();

	mm_core_post(false, mc_table_stride_routine, 0);

	LEAVE();
}

static mm_result_t
mc_table_stride_routine(uintptr_t arg __attribute__((unused)))
{
	ENTER();
	ASSERT(mc_table.striding);

	if (unlikely(mc_table.used == mc_table.size)) {
		mc_table_expand(mc_table.size * 2);
	}

	mc_table_stride();

	if (mc_table_is_full()) {
		mc_table_start_striding();
	} else {
		mc_table.striding = false;
	}

	LEAVE();
	return 0;
}

static struct mc_entry *
mc_table_lookup(uint32_t index, const char *key, uint8_t key_len)
{
	ENTER();
	DEBUG("index: %d", index);

	struct mc_entry *entry = mc_table.table[index];
	while (entry != NULL) {
		char *entry_key = mc_entry_key(entry);
		if (key_len == entry->key_len && !memcmp(key, entry_key, key_len))
			break;

		entry = entry->next;
	}

	LEAVE();
	return entry;
}

static struct mc_entry *
mc_table_remove(uint32_t index, const char *key, uint8_t key_len)
{
	ENTER();
	DEBUG("index: %d", index);

	struct mc_entry *entry = mc_table.table[index];
	if (entry == NULL) {
		goto leave;
	}

	char *entry_key = mc_entry_key(entry);
	if (key_len == entry->key_len && !memcmp(key, entry_key, key_len)) {
		mm_list_delete(&entry->link);
		mc_table.table[index] = entry->next;
		--mc_table.nentries;
		goto leave;
	}

	for (;;) {
		struct mc_entry *prev_entry = entry;

		entry = entry->next;
		if (entry == NULL) {
			goto leave;
		}

		entry_key = mc_entry_key(entry);
		if (key_len == entry->key_len && !memcmp(key, entry_key, key_len)) {
			mm_list_delete(&entry->link);
			prev_entry->next = entry->next;
			--mc_table.nentries;
			goto leave;
		}
	}

leave:
	LEAVE();
	return entry;
}

static void
mc_table_insert(uint32_t index, struct mc_entry *entry)
{
	ENTER();
	DEBUG("index: %d", index);

	mm_list_append(&mc_entry_list, &entry->link);
	entry->next = mc_table.table[index];
	mc_table.table[index] = entry;

	++mc_table.nentries;

	if (!mc_table.striding && mc_table_is_full()) {
		mc_table.striding = true;
		mc_table_start_striding();
	}

	LEAVE();
}

static void
mc_table_init(void)
{
	ENTER();

	// Compute the maximal size of the table in bytes.
	size_t nbytes = mc_table_size(MC_TABLE_SIZE_MAX);

	// Reserve the address space for the table.
	mm_brief("Reserve %ld bytes of the address apace for the memcache table.", (unsigned long) nbytes);
	void *area = mmap(NULL, nbytes, PROT_NONE,
			  MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (area == MAP_FAILED)
		mm_fatal(errno, "mmap");

	// Initialize the table.
	mc_table.size = 0;
	mc_table.mask = 0;
	mc_table.striding = false;
	mc_table.nentries = 0;
	mc_table.table = area;

	// Allocate initial space for the table.
	mc_table_expand(MC_TABLE_SIZE_MIN);
	mc_table.used = MC_TABLE_SIZE_MIN;

	// Initialize the entry list.
	mm_list_init(&mc_entry_list);

	LEAVE();
}

static void
mc_table_term(void)
{
	ENTER();

	for (uint32_t index = 0; index < mc_table.used; index++) {
		struct mc_entry *entry = mc_table.table[index];
		while (entry != NULL) {
			struct mc_entry *next = entry->next;
			mm_free(entry);
			entry = next;
		}
	}

	munmap(mc_table.table, mc_table_size(mc_table.size));

	LEAVE();
}

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

/* Forward declarations. */
struct mc_parser;
struct mc_command;

/* Command handling routines. */
typedef bool (*mc_parse_routine)(struct mc_parser *parser);

#define MC_ASYNC 1

/*
 * Some preprocessor magic to emit command definitions.
 */

#define MC_COMMAND_LIST(_)						\
	_(get,		dummy,		get,		MC_ASYNC)	\
	_(gets,		dummy,		gets,		MC_ASYNC)	\
	_(set,		set,		set,		MC_ASYNC)	\
	_(add,		set,		add,		MC_ASYNC)	\
	_(replace,	set,		replace,	MC_ASYNC)	\
	_(append,	set,		append,		MC_ASYNC)	\
	_(prepend,	set,		prepend,	MC_ASYNC)	\
	_(cas,		cas,		cas,		MC_ASYNC)	\
	_(incr,		dummy,		incr,		MC_ASYNC)	\
	_(decr,		dummy,		decr,		MC_ASYNC)	\
	_(delete,	dummy,		delete,		MC_ASYNC)	\
	_(touch,	dummy,		touch,		MC_ASYNC)	\
	_(slabs,	dummy,		slabs,		0)		\
	_(stats,	dummy,		stats,		0)		\
	_(flush_all,	dummy,		flush_all,	0)		\
	_(version,	dummy,		version,	0)		\
	_(verbosity,	dummy,		verbosity,	0)		\
	_(quit,		dummy,		quit,		0)

/*
 * Define enumerated type to tag commands.
 */

#define MC_COMMAND_TAG(cmd, parse_name, process_name, async_val)	\
	mc_command_##cmd,

typedef enum {
	MC_COMMAND_LIST(MC_COMMAND_TAG)
} mc_command_t;

/*
 * Define command handling info.
 */

struct mc_command_type
{
	mc_command_t tag;
	const char *name;
	mc_parse_routine parse;
	mm_routine_t process;
	uint32_t flags;
};

#define MC_COMMAND_TYPE(cmd, parse_name, process_name, value)		\
	static bool mc_parse_##parse_name(struct mc_parser *);		\
	static mm_result_t mc_process_##process_name(uintptr_t);	\
	static struct mc_command_type mc_desc_##cmd = {			\
		.tag = mc_command_##cmd,				\
		.name = #cmd,						\
		.parse = mc_parse_##parse_name,				\
		.process = mc_process_##process_name,			\
		.flags = value,						\
	};

MC_COMMAND_LIST(MC_COMMAND_TYPE)

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
	uint64_t val64;
	uint32_t val32;
	bool last;
};

typedef enum
{
	MC_RESULT_NONE = 0,
	MC_RESULT_BLANK,
	MC_RESULT_REPLY,
	MC_RESULT_ENTRY,
	MC_RESULT_ENTRY_CAS,
	MC_RESULT_VALUE,
	MC_RESULT_QUIT,
} mc_result_t;

union mc_result
{
	struct mc_string reply;
	struct mc_entry *entry;
};

struct mc_command
{
	struct mc_command *next;

	struct mc_command_type *type;
	struct mc_string key;
	union mc_params params;
	union mc_result result;
	mc_result_t result_type;
	bool noreply;
	bool own_key;

	struct mm_future *future;

	char *end_ptr;
};

static struct mm_pool mc_command_pool;

static void
mc_command_init(void)
{
	ENTER();

	mm_pool_prepare(&mc_command_pool, "memcache command",
			&mm_alloc_global, sizeof(struct mc_command));

	LEAVE();
}

static void
mc_command_term()
{
	ENTER();

	mm_pool_cleanup(&mc_command_pool);

	LEAVE();
}

static struct mc_command *
mc_command_create(void)
{
	ENTER();

	struct mc_command *command = mm_pool_alloc(&mc_command_pool);
	memset(command, 0, sizeof(struct mc_command));

	LEAVE();
	return command;
}

static void
mc_command_destroy(struct mc_command *command)
{
	ENTER();

	if (command->type != NULL) {
		if (command->own_key)
			mm_core_free((char *) command->key.str);

		switch (command->result_type) {
		case MC_RESULT_ENTRY:
		case MC_RESULT_ENTRY_CAS:
		case MC_RESULT_VALUE:
			mc_entry_unref(command->result.entry);
			break;

		default:
			break;
		}
	}

	mm_pool_free(&mc_command_pool, command);

	LEAVE();
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

static void
mc_blank(struct mc_command *command)
{
	DEBUG("no reply");

	command->result_type = MC_RESULT_BLANK;
}

static void
mc_reply(struct mc_command *command, const char *str)
{
	DEBUG("reply '%s'", str);

	command->result_type = MC_RESULT_REPLY;
	command->result.reply.str = str;
	command->result.reply.len = strlen(str);
}

static void
mc_entry(struct mc_command *command, struct mc_entry *entry, mc_result_t res_type)
{
	command->result_type = res_type;
	command->result.entry = entry;
	mc_entry_ref(entry);
}

static void
mc_value(struct mc_command *command, struct mc_entry *entry)
{
	command->result_type = MC_RESULT_VALUE;
	command->result.entry = entry;
	mc_entry_ref(entry);
}

/**********************************************************************
 * Aggregate Connection State.
 **********************************************************************/

struct mc_state
{
	// Current parse position.
	char *start_ptr;

	// Command processing queue.
	struct mc_command *command_head;
	struct mc_command *command_tail;

	// The client socket,
	struct mm_net_socket *sock;
	// Receive buffer.
	struct mm_buffer rbuf;
	// Transmit buffer.
	struct mm_buffer tbuf;

	// The quit flag.
	bool quit;
};

static struct mc_state *
mc_create(struct mm_net_socket *sock)
{
	ENTER();

	struct mc_state *state = mm_alloc(sizeof(struct mc_state));

	state->start_ptr = NULL;

	state->command_head = NULL;
	state->command_tail = NULL;

	state->sock = sock;
	mm_buffer_prepare(&state->rbuf);
	mm_buffer_prepare(&state->tbuf);
	state->quit = false;

	LEAVE();
	return state;
}

static void
mc_destroy(struct mc_state *state)
{
	ENTER();

	while (state->command_head != NULL) {
		struct mc_command *command = state->command_head;
		state->command_head = command->next;
		mc_command_destroy(command);
	}

	mm_buffer_cleanup(&state->rbuf);
	mm_buffer_cleanup(&state->tbuf);
	mm_free(state);

	LEAVE();
}

static void
mc_queue_command(struct mc_state *state,
		 struct mc_command *command,
		 struct mc_command *last_command)
{
	ENTER();
	ASSERT(command != NULL);
	ASSERT(last_command != NULL);

	if (state->command_head == NULL) {
		state->command_head = command;
	} else {
		state->command_tail->next = command;
	}
	state->command_tail = last_command;

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
 * I/O Routines.
 **********************************************************************/

static bool
mc_read_hangup(ssize_t n, int error)
{
	ASSERT(n <= 0);

	if (n < 0) {
		if (error == EAGAIN)
			return false;
		if (error == EWOULDBLOCK)
			return false;
		if (error == ETIMEDOUT)
			return false;
		if (error == EINTR)
			return false;
	}

	return true;
}

static ssize_t
mc_read(struct mc_state *state, size_t required, size_t optional, bool *hangup)
{
	ENTER();

	*hangup = false;

	size_t total = required + optional;
	mm_buffer_demand(&state->rbuf, total);

	size_t count = total;
	while (count > optional) {
		ssize_t n = mm_net_readbuf(state->sock, &state->rbuf);
		if (n <= 0) {
			*hangup = mc_read_hangup(n, errno);
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

	char *dst = mc_entry_value(entry) + offset;
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

static mm_result_t
mc_process_get2(uintptr_t arg, mc_result_t res_type)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *entry = mc_table_lookup(index, key, key_len);
	if (entry != NULL)
		mc_entry(command, entry, res_type);
	else if (command->params.last)
		mc_reply(command, "END\r\n");
	else
		mc_blank(command);

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_get(uintptr_t arg)
{
	return mc_process_get2(arg, MC_RESULT_ENTRY);
}

static mm_result_t
mc_process_gets(uintptr_t arg)
{
	return mc_process_get2(arg, MC_RESULT_ENTRY_CAS);
}

static mm_result_t
mc_process_set(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);
	if (old_entry != NULL)
		mc_entry_unref(old_entry);

	struct mc_entry *new_entry = mc_entry_create(key_len, params->bytes);
	mc_entry_set_key(new_entry, key);
	mc_process_value(new_entry, params, 0);
	new_entry->flags = params->flags;

	mc_table_insert(index, new_entry);
	mc_entry_ref(new_entry);

	if (command->noreply)
		mc_blank(command);
	else
		mc_reply(command, "STORED\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_add(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry == NULL) {
		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, params, 0);
		new_entry->flags = params->flags;
		mc_table_insert(index, new_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_reply(command, "STORED\r\n");
	else
		mc_reply(command, "NOT_STORED\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_replace(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		mc_entry_unref(old_entry);

		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, params, 0);
		new_entry->flags = params->flags;
		mc_table_insert(index, new_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_reply(command, "STORED\r\n");
	else
		mc_reply(command, "NOT_STORED\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_cas(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && old_entry->cas == params->cas) {
		struct mc_entry *old_entry2 = mc_table_remove(index, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		new_entry = mc_entry_create(key_len, params->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, params, 0);
		new_entry->flags = params->flags;
		mc_table_insert(index, new_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_reply(command, "STORED\r\n");
	else if (old_entry != NULL)
		mc_reply(command, "EXISTS\r\n");
	else
		mc_reply(command, "NOT_FOUND\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_append(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_value(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_set_key(new_entry, key);
		char *new_value = mc_entry_value(new_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		mc_process_value(new_entry, params, old_entry->value_len);
		new_entry->flags = old_entry->flags;
		mc_table_insert(index, new_entry);

		mc_entry_unref(old_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_reply(command, "STORED\r\n");
	else
		mc_reply(command, "NOT_STORED\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_prepend(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;
	struct mc_set_params *params = &command->params.set;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + params->bytes;
		char *old_value = mc_entry_value(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_set_key(new_entry, key);
		char *new_value = mc_entry_value(new_entry);
		mc_process_value(new_entry, params, 0);
		memcpy(new_value + params->bytes, old_value, old_entry->value_len);
		new_entry->flags = old_entry->flags;
		mc_table_insert(index, new_entry);

		mc_entry_unref(old_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_reply(command, "STORED\r\n");
	else
		mc_reply(command, "NOT_STORED\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_incr(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		value += command->params.val64;

		new_entry = mc_entry_create_u64(key_len, value);
		mc_entry_set_key(new_entry, key);
		new_entry->flags = old_entry->flags;

		struct mc_entry *old_entry2 = mc_table_remove(index, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(index, new_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_value(command, new_entry);
	else if (old_entry != NULL)
		mc_reply(command, "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n");
	else
		mc_reply(command, "NOT_FOUND\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_decr(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		if (value > command->params.val64)
			value -= command->params.val64;
		else
			value = 0;

		new_entry = mc_entry_create_u64(key_len, value);
		mc_entry_set_key(new_entry, key);
		new_entry->flags = old_entry->flags;

		struct mc_entry *old_entry2 = mc_table_remove(index, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(index, new_entry);
	}

	if (command->noreply)
		mc_blank(command);
	else if (new_entry != NULL)
		mc_value(command, new_entry);
	else if (old_entry != NULL)
		mc_reply(command, "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n");
	else
		mc_reply(command, "NOT_FOUND\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_delete(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->key.str;
	size_t key_len = command->key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	if (command->noreply)
		mc_blank(command);
	else if (old_entry != NULL)
		mc_reply(command, "DELETED\r\n");
	else
		mc_reply(command, "NOT_FOUND\r\n");

	mc_entry_destroy(old_entry);

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_touch(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mc_reply(command, "SERVER_ERROR not implemented\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_slabs(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mc_reply(command, "SERVER_ERROR not implemented\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_stats(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	if (command->params.stats.nopts)
		mc_reply(command, "SERVER_ERROR not implemented\r\n");
	else
		mc_reply(command, "END\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_flush_all(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	// TODO: really use the exptime.
	mc_exptime = mc_curtime + command->params.val32 * 1000000ull;

	// TODO: do this as a background task.
	while (!mm_list_empty(&mc_entry_list)) {
		struct mm_list *link = mm_list_head(&mc_entry_list);
		struct mc_entry *entry = containerof(link, struct mc_entry, link);

		char *key = mc_entry_key(entry);
		uint32_t index = mc_table_key_index(key, entry->key_len);
		mc_table_remove(index, key, entry->key_len);

		mc_entry_unref(entry);
	}

	if (command->noreply)
		mc_blank(command);
	else
		mc_reply(command, "OK\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_version(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mc_reply(command, MC_VERSION);

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_verbosity(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;

	mc_verbose = min(command->params.val32, 2u);
	DEBUG("set verbosity %d", mc_verbose);

	if (command->noreply)
		mc_blank(command);
	else
		mc_reply(command, "OK\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_quit(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	command->result_type = MC_RESULT_QUIT;

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_command(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	struct mc_command *last = command;
	if (likely(command->type != NULL)) {
		DEBUG("command %s", command->type->name);
		for (;;) {
			if (last->result_type == MC_RESULT_NONE) {
				// TODO: create a future for async commands.
				last->type->process((intptr_t) last);
			}

			if (last->next == NULL)
				break;
			last = last->next;
		}
	}

	mc_queue_command(state, command, last);
	mm_net_spawn_writer(state->sock);

	LEAVE();
	return 0;
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
	bool error;
};

static inline bool
mc_buffer_contains(struct mm_buffer_cursor *cur, const char *ptr)
{
	return ptr >= cur->ptr && ptr < cur->end;
}

/*
 * Prepare for parsing a command.
 */
static void
mc_start_input(struct mc_parser *parser,
	       struct mc_state *state,
	       struct mc_command *command)
{
	ENTER();

	mm_buffer_first_out(&state->rbuf, &parser->cursor);
	if (state->start_ptr != NULL) {
		while (!mc_buffer_contains(&parser->cursor, state->start_ptr)) {
			mm_buffer_next_out(&state->rbuf, &parser->cursor);
		}
	}

	parser->state = state;
	parser->command = command;
	parser->error = false;

	LEAVE();
}

/*
 * Ask for the next input buffer with additional sanity checking.
 */
static bool
mc_more_input(struct mc_parser *parser, int count)
{
	if (unlikely(count > 1024)) {
		/* The client looks insane. Quit fast. */
		parser->command->result_type = MC_RESULT_QUIT;
		parser->state->quit = true;
		return false;
	}
	return mm_buffer_next_out(&parser->state->rbuf, &parser->cursor);
}

static void
mc_end_input(struct mc_parser *parser)
{
	ENTER();

	struct mc_command *command = parser->command;
	while (command->next != NULL) {
		command->end_ptr = parser->state->start_ptr;
		command = command->next;
	}

	command->end_ptr = parser->cursor.ptr;
	parser->state->start_ptr = parser->cursor.ptr;

	LEAVE();
}

static int
mc_peek_input(struct mc_parser *parser, char *s)
{
	ASSERT(mc_buffer_contains(&parser->cursor, s));

	if ((s + 1) < parser->cursor.end)
		return *(s + 1);

	struct mm_buffer_segment *seg = parser->cursor.seg;
	if (seg != parser->state->rbuf.in_seg) {
		seg = seg->next;
		if (seg != parser->state->rbuf.in_seg || parser->state->rbuf.in_off) {
			return seg->data[0];
		}
	}

	return 256; // not a char
}

static bool
mc_parse_space(struct mc_parser *parser)
{
	ENTER();
	bool rc = true;

	// The count of scanned chars. Used to check if the client sends
	// too much junk data.
	int count = 0;

	do {
		char *s = parser->cursor.ptr;
		char *e = parser->cursor.end;

		for (; s < e; s++) {
			if (*s != ' ') {
				parser->cursor.ptr = s;
				goto leave;
			}
		}

		count += e - parser->cursor.ptr;
		rc = mc_more_input(parser, count);

	} while (rc);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_error(struct mc_parser *parser, const char *error_string)
{
	ENTER();
	bool rc = true;
	parser->error = true;

	// Skip everything until a LF char.
	do {
		char *s = parser->cursor.ptr;
		char *e = parser->cursor.end;

		// Scan input for a newline.
		char *p = memchr(s, '\n', e - s);
		if (p != NULL) {
			// Go past the newline for the next command.
			parser->cursor.ptr = p + 1;
			// Report the error.
			mc_reply(parser->command, error_string);
			break;
		}

		rc = mm_buffer_next_out(&parser->state->rbuf, &parser->cursor);

	} while (rc);

	LEAVE();
	return rc;
}

static bool
mc_parse_eol(struct mc_parser *parser)
{
	ENTER();
	bool rc = true;

	// The count of scanned chars. Used to check if the client sends
	// too much junk data.
	int count = 0;

	// Scan for end of line skipping possible spaces.
	do {
		char *s = parser->cursor.ptr;
		char *e = parser->cursor.end;

		for (; s < e; s++) {
			int c = *s;

			// Check for optional CR and required LF chars.
			if (c == '\r') {
				++s;
				if (unlikely(s == e)) {
					if (!mm_buffer_next_out(&parser->state->rbuf,
								&parser->cursor)) {
						rc = false;
						goto leave;
					}
					s = parser->cursor.ptr;
					e = parser->cursor.end;
					if (s == e) {
						rc = false;
						goto leave;
					}
				}
				parser->cursor.ptr = s + 1;
				if (unlikely(*s != '\n'))
					rc = mc_parse_error(parser, "CLIENT_ERROR unexpected parameter\r\n");
				goto leave;
			} else if (c == '\n') {
				parser->cursor.ptr = s + 1;
				goto leave;
			} else if (c != ' ') {
				// Oops, unexpected char.
				parser->cursor.ptr = s + 1;
				rc = mc_parse_error(parser, "CLIENT_ERROR unexpected parameter\r\n");
				goto leave;
			}
		}

		count += e - parser->cursor.ptr;
		rc = mc_more_input(parser, count);

	} while (rc);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_param(struct mc_parser *parser, struct mc_string *value, bool required)
{
	ENTER();

	bool rc = mc_parse_space(parser);
	if (!rc)
		goto leave;

	char *s, *e;

retry:
	s = parser->cursor.ptr;
	e = parser->cursor.end;
	for (; s < e; s++) {
		int c = *s;
		if (c == ' ' || (c == '\r' && mc_peek_input(parser, s) == '\n') || c == '\n') {
			int count = s - parser->cursor.ptr;
			if (required && count == 0) {
				rc = mc_parse_error(parser, "CLIENT_ERROR missing parameter\r\n");
			} else if (count > MC_KEY_LEN_MAX) {
				rc = mc_parse_error(parser, "CLIENT_ERROR parameter is too long\r\n");
			} else {
				value->len = count;
				value->str = parser->cursor.ptr;
				parser->cursor.ptr = s;

				DEBUG("%.*s", (int) value->len, value->str);
			}
			goto leave;
		}
	}

	size_t count = e - parser->cursor.ptr;
	if (count > MC_KEY_LEN_MAX) {
		rc = mc_parse_error(parser, "CLIENT_ERROR parameter is too long\r\n");
		goto leave;
	}

	struct mm_buffer *rbuf = &parser->state->rbuf;
	struct mm_buffer_segment *seg = parser->cursor.seg;
	if (seg == rbuf->in_seg) {
		ASSERT(e == (rbuf->in_seg->data + rbuf->in_off));
		if (rbuf->in_seg->size > rbuf->in_off) {
			rc = false;
			goto leave;
		}

		if (seg->next == NULL) {
			mm_buffer_demand(rbuf, MC_KEY_LEN_MAX + 1);
			ASSERT(seg->next != NULL);
		}

		memcpy(seg->next->data, parser->cursor.ptr, count);
		memset(parser->cursor.ptr, ' ', count);
		mm_buffer_expand(rbuf, count);

	} else if (seg->next == rbuf->in_seg) {
		size_t n = rbuf->in_seg->size - rbuf->in_off;
		if (n < count) {
			// TODO: handle this case
			ABORT();
		}

		memmove(seg->next->data + count, seg->next->data, rbuf->in_off);
		memcpy(seg->next->data, parser->cursor.ptr, count);
		memset(parser->cursor.ptr, ' ', count);
		mm_buffer_expand(rbuf, count);

	} else {
		// TODO: handle this case
		ABORT();
	}

	mm_buffer_next_out(rbuf, &parser->cursor);
	goto retry;

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_u32(struct mc_parser *parser, uint32_t *value)
{
	ENTER();

	struct mc_string param;
	bool rc = mc_parse_param(parser, &param, true);
	if (rc && !parser->error) {
		char *endp;
		unsigned long v = strtoul(param.str, &endp, 10);
		if (endp < param.str + param.len) {
			rc = mc_parse_error(parser, "CLIENT_ERROR invalid number parameter\r\n");
		} else {
			*value = v;
		}
	}

	LEAVE();
	return rc;
}

static bool
mc_parse_u64(struct mc_parser *parser, uint64_t *value)
{
	ENTER();

	struct mc_string param;
	bool rc = mc_parse_param(parser, &param, true);
	if (rc && !parser->error) {
		char *endp;
		unsigned long long v = strtoull(param.str, &endp, 10);
		if (endp < param.str + param.len) {
			rc = mc_parse_error(parser, "CLIENT_ERROR invalid number parameter\r\n");
		} else {
			*value = v;
		}
	}

	LEAVE();
	return rc;
}

static bool
mc_parse_noreply(struct mc_parser *parser, bool *value)
{
	ENTER();

	bool rc = mc_parse_space(parser);
	if (!rc)
		goto leave;

	char *s, *e;
	s = parser->cursor.ptr;
	e = parser->cursor.end;

	const char *t = "noreply";

	int n = e - s;
	if (n > 7) {
		n = 7;
	} else if (n < 7) {
		if (memcmp(s, t, n) != 0) {
			*value = false;
			goto leave;
		}

		rc = mm_buffer_next_out(&parser->state->rbuf, &parser->cursor);
		if (!rc)
			goto leave;
		s = parser->cursor.ptr;
		e = parser->cursor.end;

		t = t + n;
		n = 7 - n;

		if ((e - s) < n) {
			rc = false;
			goto leave;
		}
	}

	if (memcmp(s, t, n) != 0) {
		*value = false;
		goto leave;
	}

	*value = true;
	parser->cursor.ptr += n;

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_data(struct mc_parser *parser, uint32_t bytes)
{
	ENTER();
	DEBUG("bytes: %d", bytes);

	bool rc = true;
	uint32_t cr = 1;

	/* Save current input buffer position. */
  	parser->command->params.set.seg = parser->cursor.seg;
	parser->command->params.set.start = parser->cursor.ptr;

	for (;;) {
		uint32_t avail = parser->cursor.end - parser->cursor.ptr;
		DEBUG("parse data: avail = %ld, bytes = %ld", (long) avail, (long) bytes);
		if (avail > bytes) {
			parser->cursor.ptr += bytes;
			avail -= bytes;
			bytes = 0;

			if (parser->cursor.ptr[0] == '\n') {
				parser->cursor.ptr++;
				break;
			}

			if (!cr
			    || parser->cursor.ptr[0] != '\r'
			    || (avail > 1 && parser->cursor.ptr[1] != '\n')) {
				parser->error = true;
				mc_reply(parser->command,
					 "CLIENT_ERROR bad data chunk\r\n");
			}

			if (!cr || avail > 1) {
				parser->cursor.ptr++;
				if (cr)
					parser->cursor.ptr++;
				break;
			}

			parser->cursor.ptr++;
			cr = 0;
		} else {
			parser->cursor.ptr += avail;
			bytes -= avail;
		}

		if (!mm_buffer_next_out(&parser->state->rbuf, &parser->cursor)) {
			bool hangup;
			ssize_t r = bytes + 1;
			ssize_t n = mc_read(parser->state, r, cr, &hangup);
			if (n < r) {
				parser->command->result_type = MC_RESULT_QUIT;
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
mc_parse_command(struct mc_parser *parser)
{
	ENTER();

	enum parse_state {
		S_START,
		S_CMD_1,
		S_CMD_2,
		S_CMD_3,
		S_MATCH,
		S_SPACE,

		S_GET_1,
		S_GET_N,
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

		S_KEY,
		S_KEY_N,
		S_KEY_EDGE,
		S_KEY_COPY,

		S_OPT,
		S_OPT_N,

		S_NUM32,
		S_NUM64,
		S_EOL,
		S_EOL_1,

		S_ERROR,
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
	struct mc_command *command = parser->command;

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
					// Unexpected line end.
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
				if (c == '\n') {
					// Unexpected line end.
					state = S_ERROR;
					goto again;
				}
				start |= c << 16;
				state = S_CMD_2;
				break;

			case S_CMD_2:
				// Store the third command char.
				if (c == '\n') {
					// Unexpected line end.
					state = S_ERROR;
					goto again;
				}
				start |= c << 8;
				state = S_CMD_3;
				break;

			case S_CMD_3:
				// Have the first 4 chars of the command,
				// it is enough to learn what it is.
				start |= c;

#define Cx4(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
				if (start == Cx4('g', 'e', 't', ' ')) {
					command->type = &mc_desc_get;
					parser->cursor.ptr = s + 1;
					state = S_SPACE;
					shift = S_GET_1;
					break;
				} else if (start == Cx4('s', 'e', 't', ' ')) {
					command->type = &mc_desc_set;
					parser->cursor.ptr = s + 1;
					goto leave;
				} else if (start == Cx4('r', 'e', 'p', 'l')) {
					command->type = &mc_desc_replace;
					state = S_MATCH;
					match = "ace";
					break;
				} else if (start == Cx4('d', 'e', 'l', 'e')) {
					command->type = &mc_desc_delete;
					state = S_MATCH;
					match = "te";
					shift = S_DELETE_1;
					break;
				} else if (start == Cx4('a', 'd', 'd', ' ')) {
					command->type = &mc_desc_add;
					parser->cursor.ptr = s + 1;
					goto leave;
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
					parser->cursor.ptr = s + 1;
					goto leave;
				} else if (start == Cx4('a', 'p', 'p', 'e')) {
					command->type = &mc_desc_append;
					state = S_MATCH;
					match = "nd";
					break;
				} else if (start == Cx4('p', 'r', 'e', 'p')) {
					command->type = &mc_desc_prepend;
					state = S_MATCH;
					match = "end";
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
					state = S_SPACE;
					shift = S_EOL;
					break;
				} else {
					// Unrecognized command.
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
				} else if (*match) {
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
					// Unexpected char after the end.
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

			case S_GET_1:
				ASSERT(c != ' ');
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
					command->next = mc_command_create();
					command->next->type = command->type;
					command = command->next;
					goto again;
				}

			case S_ARITH_1:
				state = S_KEY;
				shift = S_ARITH_2;
				goto again;

			case S_ARITH_2:
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
					state = S_ERROR;
					goto again;
				} else {
					num64 = 0;
					state = S_NUM64;
					shift = S_VAL64;
					goto again;
				}

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
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
					state = S_ERROR;
					goto again;
				} else {
					num32 = 0;
					state = S_NUM32;
					shift = S_VAL32;
					goto again;
				}

			case S_FLUSH_ALL_1:
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
					state = S_EOL;
					goto again;
				} else if (c >= '0' && c <= '9') {
					num32 = 0;
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
					num32 = 0;
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
				ASSERT(c != ' ');
				command->noreply = true;
				state = S_EOL;
				goto again;

			case S_KEY:
				ASSERT(c != ' ');
				if (c == '\r' || c == '\n') {
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
						state = S_ERROR;
					} else {
						state = S_SPACE;
						command->key.len = len;
					}
					break;
				} else if (c == '\r' || c == '\n') {
					size_t len = s - command->key.str;
					if (len > MC_KEY_LEN_MAX) {
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
				} else if (c == '\r' || c == '\n') {
					state = shift;
					command->key.len = MC_KEY_LEN_MAX;
					goto again;
				} else {
					state = S_ERROR;
					break;
				}

			case S_KEY_COPY:
				if (c == ' ') {
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					state = shift;
					goto again;
				} else {
					struct mc_string *key = &command->key;
					if (key->len == MC_KEY_LEN_MAX) {
						state = S_ERROR;
					} else {
						char *str = (char *) key->str;
						str[key->len++] = c;
					}
					break;
				}

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

			case S_NUM32:
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
						mc_command_destroy(tmp);
					} while (command != NULL);

					parser->command->next = NULL;
					command = parser->command;
				}

				if (c == '\n') {
					mc_reply(command, "ERROR\r\n");
					parser->cursor.ptr = s + 1;
					parser->error = true;
					goto leave;
				}
				break;

			case S_ABORT:
#if 1
				parser->cursor.ptr = s;
				goto leave;
#else
				ABORT();
#endif
			}
		}

		if (state == S_KEY) {
			size_t len = e - command->key.str;
			if (len > MC_KEY_LEN_MAX) {
				state = S_ERROR;
			} else if (len == MC_KEY_LEN_MAX) {
				state = S_KEY_EDGE;
			} else {
				state = S_KEY_COPY;

				char *str = mm_core_alloc(MC_KEY_LEN_MAX);
				memcpy(str, command->key.str, len);
				command->key.len = len;
				command->key.str = str;
				command->own_key = true;
			}
		}

		count += e - parser->cursor.ptr;
		rc = mc_more_input(parser, count);

	} while (rc);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_dummy(struct mc_parser *parser __attribute__((unused)))
{
	return true;
}

static bool
mc_parse_set(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.flags);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.exptime);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.bytes);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_data(parser, parser->command->params.set.bytes);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_cas(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.flags);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.exptime);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.bytes);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u64(parser, &parser->command->params.set.cas);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_data(parser, parser->command->params.set.bytes);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse(struct mc_parser *parser)
{
	ENTER();

	/* Parse the command name. */
	bool rc = mc_parse_command(parser);
	if (!rc || parser->error)
		goto leave;

	/* Parse the rest of the command. */
	rc = parser->command->type->parse(parser);

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
mc_transmit_buffer(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	switch (command->result_type) {
	case MC_RESULT_BLANK:
		break;

	case MC_RESULT_REPLY:
		mm_buffer_append(&state->tbuf,
				 command->result.reply.str,
				 command->result.reply.len);
		break;

	case MC_RESULT_ENTRY:
	case MC_RESULT_ENTRY_CAS: {
		struct mc_entry *entry = command->result.entry;
		const char *key = mc_entry_key(entry);
		char *value = mc_entry_value(entry);
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
		struct mc_entry *entry = command->result.entry;
		char *value = mc_entry_value(entry);
		uint32_t value_len = entry->value_len;

		mc_entry_ref(entry);
		mm_buffer_splice(&state->tbuf, value, value_len,
				 mc_transmit_unref, (uintptr_t) entry);

		mm_buffer_append(&state->tbuf, "END\r\n", 5);
		break;
	}

	case MC_RESULT_QUIT:
		state->quit = true;
		mm_net_close(state->sock);
		break;

	default:
		ABORT();
	}

	LEAVE();
}

static void
mc_transmit(struct mc_state *state)
{
	ENTER();

	ssize_t n = mm_net_writebuf(state->sock, &state->tbuf);
	if (n > 0)
		mm_buffer_rectify(&state->tbuf);

	LEAVE();
}

/**********************************************************************
 * Protocol Handlers.
 **********************************************************************/

#define MC_READ_TIMEOUT		10000

static void
mc_prepare(struct mm_net_socket *sock)
{
	ENTER();

	sock->data = 0;

	LEAVE();
}

static void
mc_cleanup(struct mm_net_socket *sock)
{
	ENTER();

	if (sock->data) {
		mc_destroy((struct mc_state *) sock->data);
		sock->data = 0;
	}

	LEAVE();
}

static void
mc_reader_routine(struct mm_net_socket *sock)
{
	ENTER();

	// Get the protocol data.
	struct mc_state *state = (struct mc_state *) sock->data;
	if (state == NULL) {
		state = mc_create(sock);
		sock->data = (intptr_t) state;
	}

	// Try to get some input w/o blocking.
	bool hangup;
	mm_net_set_read_timeout(state->sock, 0);
	ssize_t n = mc_read(state, 1, 0, &hangup);
	mm_net_set_read_timeout(state->sock, MC_READ_TIMEOUT);

	// Get out if there is no input available.
	if (n <= 0) {
		// If the socket is closed queue a quit command.
		if (hangup) {
			struct mc_command *command = mc_command_create();
			command->result_type = MC_RESULT_QUIT;
			command->end_ptr = state->start_ptr;
			mc_process_command(state, command);
		}
		goto leave;
	}

	// Initialize the parser.
	struct mc_parser parser;
	mc_start_input(&parser, state, NULL);
	parser.command = mc_command_create();
	// TODO: protect the created command against cancellation.

	// Try to parse the received input.
	for (;;) {
		bool rc = mc_parse(&parser);
		if (rc) {
			// Mark the parsed input as consumed.
			mc_end_input(&parser);
			// Process the parsed command.
			mc_process_command(state, parser.command);

			mm_buffer_rectify(&state->rbuf);

			// TODO: check if there is more input.
			parser.command = mc_command_create();
			parser.error = false;
			continue;
		} else if (state->quit) {
			mc_command_destroy(parser.command);
			goto leave;
		}

		// The input is incomplete, try to get some more.
		n = mc_read(state, 1, 0, &hangup);

		// Get out if there is no more input.
		if (n <= 0) {
 			if (hangup) {
				parser.command->result_type = MC_RESULT_QUIT;
				parser.command->end_ptr = parser.cursor.ptr;
				mc_process_command(state, parser.command);
			} else {
				mc_command_destroy(parser.command);
			}
			goto leave;
		}

		mc_start_input(&parser, state, parser.command);
	}

leave:
	LEAVE();
}

static void
mc_writer_routine(struct mm_net_socket *sock)
{
	ENTER();

	// Get the protocol data if any.
	struct mc_state *state = (struct mc_state *) sock->data;
	if (unlikely(state == NULL))
		goto leave;

	// Check to see if there at least one ready result.
	struct mc_command *last = state->command_head;
	if (unlikely(last == NULL))
		goto leave;
	if (unlikely(last->result_type == MC_RESULT_NONE))
		goto leave;

	// Put the results into the transmit buffer.
	while (!state->quit) {
		mc_transmit_buffer(state, last);

		struct mc_command *next = last->next;
		if (next == NULL || next->result_type == MC_RESULT_NONE)
			break;

		last = next;
	}

	// Transmit buffered results.
	mc_transmit(state);

	// Free the receive buffers.
	mc_release_buffers(state, last->end_ptr);

	// Release the command data
	for (;;) {
		struct mc_command *head = state->command_head;
		state->command_head = head->next;
		mc_command_destroy(head);

		if (head == last) {
			if (state->command_head == NULL)
				state->command_tail = NULL;
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

void
mm_memcache_init(void)
{
	ENTER();

	mc_table_init();
	mc_command_init();

	static struct mm_net_proto proto = {
		.flags = MM_NET_INBOUND,
		.prepare = mc_prepare,
		.cleanup = mc_cleanup,
		.reader = mc_reader_routine,
		.writer = mc_writer_routine,
	};

	mc_tcp_server = mm_net_create_inet_server("memcache", &proto,
						  "127.0.0.1", 11211);
	mm_core_register_server(mc_tcp_server);

	LEAVE();
}

void
mm_memcache_term(void)
{
	ENTER();

	mc_command_term();
	mc_table_term();

	LEAVE();
}
