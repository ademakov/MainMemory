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

// The logging verbosity level.
static int mc_verbose = 0;

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
 * Protocol I/O Buffer.
 **********************************************************************/

#define MC_DEFAULT_BUFFER_SIZE	4000

static struct mm_chunk *
mc_buffer_create(size_t size)
{
	ENTER();

	size += MC_DEFAULT_BUFFER_SIZE - 1;
	size -= size % MC_DEFAULT_BUFFER_SIZE;
	struct mm_chunk *buffer = mm_chunk_create(size);

	LEAVE();
	return buffer;
}

static inline bool
mc_buffer_contains(struct mm_chunk *buffer, const char *ptr)
{
	return ptr >= buffer->data && ptr < (buffer->data + buffer->size);
}

static inline bool
mc_buffer_finished(struct mm_chunk *buffer, const char *ptr)
{
	return ptr == (buffer->data + buffer->size);
}

/**********************************************************************
 * Command Data.
 **********************************************************************/

/* Forward declaration. */
struct mc_parser;

struct mc_string
{
	size_t len;
	const char *str;
};

struct mc_value
{
	struct mm_chunk *buffer;
	const char *start;
	uint32_t bytes;
};

struct mc_get_params
{
	struct mc_string *keys;
	uint32_t nkeys;
};

struct mc_set_params
{
	struct mc_string key;
	uint32_t flags;
	uint32_t exptime;
	struct mc_value value;
	bool noreply;
};

struct mc_cas_params
{
	struct mc_string key;
	uint32_t flags;
	uint32_t exptime;
	struct mc_value value;
	uint64_t cas;
	bool noreply;
};

struct mc_inc_params
{
	struct mc_string key;
	uint64_t value;
	bool noreply;
};

struct mc_del_params
{
	struct mc_string key;
	bool noreply;
};

struct mc_touch_params
{
	struct mc_string key;
	uint32_t exptime;
	bool noreply;
};

union mc_params
{
	struct mc_set_params set;
	struct mc_get_params get;
	struct mc_cas_params cas;
	struct mc_inc_params inc;
	struct mc_del_params del;
	struct mc_touch_params touch;
};

typedef enum
{
	MC_RESULT_NONE,
	MC_RESULT_REPLY,
	MC_RESULT_ENTRY,
	MC_RESULT_ENTRY_CAS,
	MC_RESULT_VALUE,
	MC_RESULT_BLANK,
	MC_RESULT_QUIT,
} mc_result_t;

struct mc_result_entries
{
	struct mc_entry **entries;
	uint32_t nentries;
};

union mc_result
{
	struct mc_string reply;
	struct mc_result_entries entries;
	struct mc_entry *entry;
};

struct mc_command
{
	struct mc_command *next;

	struct mc_command_desc *desc;
	union mc_params params;
	char *end_ptr;

	union mc_result result;
	mc_result_t result_type;

	struct mc_future *future;
};

/* Command parsing routine. */
typedef bool (*mc_parse_routine)(struct mc_parser *parser);
typedef void (*mc_destroy_routine)(struct mc_command *command);

/* Command parsing and processing info. */
struct mc_command_desc
{
	const char *name;
	mc_parse_routine parse;
	mm_routine_t process;
	mc_destroy_routine destroy;
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
	command->next = NULL;
	command->desc = NULL;
	command->end_ptr = NULL;
	command->result_type = MC_RESULT_NONE;
	command->future = NULL;

	LEAVE();
	return command;
}

static void
mc_command_destroy(struct mc_command *command)
{
	ENTER();

	if (command->desc != NULL) {
		command->desc->destroy(command);
	}
	mm_pool_free(&mc_command_pool, command);

	LEAVE();
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
mc_blank(struct mc_command *command)
{
	DEBUG("no reply");

	command->result_type = MC_RESULT_BLANK;
}

/**********************************************************************
 * Command Destruction.
 **********************************************************************/

static void
mc_destroy_dummy(struct mc_command *command __attribute__((unused)))
{
}

static void
mc_destroy_get(struct mc_command *command)
{
	ENTER();

	mm_free(command->params.get.keys);
	if (command->result_type == MC_RESULT_ENTRY) {
		for (uint32_t i = 0; i < command->result.entries.nentries; i++) {
			mc_entry_unref(command->result.entries.entries[i]);
		}
		mm_free(command->result.entries.entries);
	}

	LEAVE();
}

static void
mc_destroy_gets(struct mc_command *command)
{
	ENTER();

	mm_free(command->params.get.keys);
	if (command->result_type == MC_RESULT_ENTRY_CAS) {
		for (uint32_t i = 0; i < command->result.entries.nentries; i++) {
			mc_entry_unref(command->result.entries.entries[i]);
		}
		mm_free(command->result.entries.entries);
	}

	LEAVE();
}

static void
mc_destroy_incr(struct mc_command *command)
{
	ENTER();

	if (command->result_type == MC_RESULT_VALUE) {
		mc_entry_unref(command->result.entry);
	}

	LEAVE();
}

/**********************************************************************
 * Command Descriptors.
 **********************************************************************/

#define MC_DESC(cmd, parse_name, process_name, destroy_name)		\
	static bool mc_parse_##parse_name(struct mc_parser *);		\
	static mm_result_t mc_process_##process_name(uintptr_t);	\
	static struct mc_command_desc mc_desc_##cmd = {			\
		.name = #cmd,						\
		.parse = mc_parse_##parse_name,				\
		.process = mc_process_##process_name,			\
		.destroy = mc_destroy_##destroy_name,			\
	}

MC_DESC(get,		get,		get,		get);
MC_DESC(gets,		get,		gets,		gets);
MC_DESC(set,		set,		set,		dummy);
MC_DESC(add,		set,		add,		dummy);
MC_DESC(replace,	set,		replace,	dummy);
MC_DESC(append,		set,		append,		dummy);
MC_DESC(prepend,	set,		prepend,	dummy);
MC_DESC(cas,		cas,		cas,		dummy);
MC_DESC(incr,		incr,		incr,		incr);
MC_DESC(decr,		incr,		decr,		incr);
MC_DESC(delete,		delete,		delete,		dummy);
MC_DESC(touch,		touch,		touch,		dummy);
MC_DESC(slabs,		slabs,		slabs,		dummy);
MC_DESC(stats,		stats,		stats,		dummy);
MC_DESC(flush_all,	flush_all,	flush_all,	dummy);
MC_DESC(version,	version,	dummy,		dummy);
MC_DESC(verbosity,	verbosity,	dummy,		dummy);
MC_DESC(quit,		quit,		dummy,		dummy);

/**********************************************************************
 * Aggregate Connection State.
 **********************************************************************/

struct mc_state
{
	// Current parse position.
	char *start_ptr;

	// Last released position.
	char *clear_ptr;

	// Input buffer queue.
	struct mm_chunk *read_head;
	struct mm_chunk *read_tail;

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
	state->clear_ptr = NULL;

	state->read_head = NULL;
	state->read_tail = NULL;

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

	while (state->read_head != NULL) {
		struct mm_chunk *buffer = state->read_head;
		state->read_head = buffer->next;
		mm_chunk_destroy(buffer);
	}

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

static struct mm_chunk *
mc_add_read_buffer(struct mc_state *state, size_t size)
{
	ENTER();

	struct mm_chunk *buffer = mc_buffer_create(size);
	if (state->read_tail == NULL) {
		state->read_head = buffer;
	} else {
		state->read_tail->next = buffer;
	}
	state->read_tail = buffer;

	LEAVE();
	return buffer;
}

static void
mc_queue_command(struct mc_state *state, struct mc_command *command)
{
	ENTER();
	ASSERT(command != NULL);

	if (state->command_head == NULL) {
		state->command_head = command;
	} else {
		state->command_tail->next = command;
	}
	state->command_tail = command;

	LEAVE();
}

static void
mc_release_buffers(struct mc_state *state, char *ptr)
{
	ENTER();

	struct mm_chunk *buffer = state->read_head;
	if (buffer == NULL)
		goto leave;

	for (;;) {
		if (mc_buffer_contains(buffer, ptr)) {
			/* The buffer is (might be) still in use. */
			state->clear_ptr = ptr;
			break;
		}

		state->read_head = buffer->next;
		if (state->read_head == NULL) {
			mm_chunk_destroy(buffer);
			state->read_tail = NULL;
			state->start_ptr = NULL;
			state->clear_ptr = NULL;
			break;
		}

		if (mc_buffer_finished(buffer, ptr) && state->start_ptr == ptr) {
			mm_chunk_destroy(buffer);
			state->start_ptr = NULL;
			state->clear_ptr = NULL;
			break;
		}

		mm_chunk_destroy(buffer);
		buffer = state->read_head;
	}

leave:
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
	size_t count = total;

	struct mm_chunk *buffer = state->read_head;
	while (count > optional) {
		size_t size;
		if (buffer == NULL) {
			buffer = mc_add_read_buffer(state, count);
			size = buffer->size;
		} else {
			size = buffer->size - buffer->used;
			if (size == 0) {
				buffer = buffer->next;
				continue;
			}
		}

		ssize_t n = mm_net_read(state->sock, buffer->data + buffer->used, size);
		if (n <= 0) {
			*hangup = mc_read_hangup(n, errno);
			break;
		}

		buffer->used += n;
		if (count > (size_t) n) {
			count -= n;
		} else {
			count = 0;
		}
	}

	LEAVE();
	return (total - count);
}

/**********************************************************************
 * Command Processing.
 **********************************************************************/

static mm_result_t
mc_process_dummy(uintptr_t arg __attribute__((unused)))
{
	return 0;
}

static void
mc_process_value(struct mc_entry *entry, struct mc_value *value, uint32_t offset)
{
	ENTER();

	const char *src = value->start;
	uint32_t bytes = value->bytes;
	struct mm_chunk *buffer = value->buffer;
	ASSERT(src >= buffer->data && src <= buffer->data + buffer->used);

	char *dst = mc_entry_value(entry) + offset;
	for (;;) {
		uint32_t n = (buffer->data + buffer->used) - src;
		if (n >= bytes) {
			memcpy(dst, src, bytes);
			break;
		}

		memcpy(dst, src, n);
		buffer = buffer->next;
		src = buffer->data;
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
	struct mc_entry **entries = mm_alloc(command->params.get.nkeys * sizeof(struct mc_entry *));
	uint32_t nentries = 0;

	for (uint32_t i = 0; i < command->params.get.nkeys; i++) {
		const char *key = command->params.get.keys[i].str;
		size_t key_len = command->params.get.keys[i].len;

		uint32_t index = mc_table_key_index(key, key_len);
		struct mc_entry *entry = mc_table_lookup(index, key, key_len);
		if (entry != NULL) {
			entries[nentries++] = entry;
			mc_entry_ref(entry);
		}
	}

	command->result_type = res_type;
	command->result.entries.entries = entries;
	command->result.entries.nentries = nentries;

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
	const char *key = command->params.set.key.str;
	size_t key_len = command->params.set.key.len;
	struct mc_value *value = &command->params.set.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);
	if (old_entry != NULL) {
		mc_entry_unref(old_entry);
	}

	struct mc_entry *new_entry = mc_entry_create(key_len, value->bytes);
	mc_entry_set_key(new_entry, key);
	mc_process_value(new_entry, value, 0);
	new_entry->flags = command->params.set.flags;

	mc_table_insert(index, new_entry);
	mc_entry_ref(new_entry);

	if (command->params.set.noreply) {
		mc_blank(command);
	} else {
		mc_reply(command, "STORED\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_add(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.set.key.str;
	size_t key_len = command->params.set.key.len;
	struct mc_value *value = &command->params.set.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry == NULL) {
		new_entry = mc_entry_create(key_len, value->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, value, 0);
		new_entry->flags = command->params.set.flags;
		mc_table_insert(index, new_entry);
	}

	if (command->params.set.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		mc_reply(command, "STORED\r\n");
	} else {
		mc_reply(command, "NOT_STORED\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_replace(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.set.key.str;
	size_t key_len = command->params.set.key.len;
	struct mc_value *value = &command->params.set.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		mc_entry_unref(old_entry);

		new_entry = mc_entry_create(key_len, value->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, value, 0);
		new_entry->flags = command->params.set.flags;
		mc_table_insert(index, new_entry);
	}

	if (command->params.set.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		mc_reply(command, "STORED\r\n");
	} else {
		mc_reply(command, "NOT_STORED\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_cas(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.cas.key.str;
	size_t key_len = command->params.cas.key.len;
	struct mc_value *value = &command->params.cas.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && old_entry->cas == command->params.cas.cas) {
		struct mc_entry *old_entry2 = mc_table_remove(index, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		new_entry = mc_entry_create(key_len, value->bytes);
		mc_entry_set_key(new_entry, key);
		mc_process_value(new_entry, value, 0);
		new_entry->flags = command->params.cas.flags;
		mc_table_insert(index, new_entry);
	}

	if (command->params.cas.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		mc_reply(command, "STORED\r\n");
	} else if (old_entry != NULL){
		mc_reply(command, "EXISTS\r\n");
	} else {
		mc_reply(command, "NOT_FOUND\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_append(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.set.key.str;
	size_t key_len = command->params.set.key.len;
	struct mc_value *value = &command->params.set.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + value->bytes;
		char *old_value = mc_entry_value(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_set_key(new_entry, key);
		char *new_value = mc_entry_value(new_entry);
		memcpy(new_value, old_value, old_entry->value_len);
		mc_process_value(new_entry, value, old_entry->value_len);
		new_entry->flags = old_entry->flags;
		mc_table_insert(index, new_entry);

		mc_entry_unref(old_entry);
	}

	if (command->params.set.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		mc_reply(command, "STORED\r\n");
	} else {
		mc_reply(command, "NOT_STORED\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_prepend(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.set.key.str;
	size_t key_len = command->params.set.key.len;
	struct mc_value *value = &command->params.set.value;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL) {
		size_t value_len = old_entry->value_len + value->bytes;
		char *old_value = mc_entry_value(old_entry);

		new_entry = mc_entry_create(key_len, value_len);
		mc_entry_set_key(new_entry, key);
		char *new_value = mc_entry_value(new_entry);
		mc_process_value(new_entry, value, 0);
		memcpy(new_value + value->bytes, old_value, old_entry->value_len);
		new_entry->flags = old_entry->flags;
		mc_table_insert(index, new_entry);

		mc_entry_unref(old_entry);
	}

	if (command->params.set.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		mc_reply(command, "STORED\r\n");
	} else {
		mc_reply(command, "NOT_STORED\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_incr(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.inc.key.str;
	size_t key_len = command->params.inc.key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		value += command->params.inc.value;

		new_entry = mc_entry_create_u64(key_len, value);
		mc_entry_set_key(new_entry, key);
		new_entry->flags = old_entry->flags;

		struct mc_entry *old_entry2 = mc_table_remove(index, key, key_len);
		ASSERT(old_entry == old_entry2);
		mc_entry_unref(old_entry2);

		mc_table_insert(index, new_entry);
	}

	if (command->params.inc.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		command->result_type = MC_RESULT_VALUE;
		command->result.entry = new_entry;
		mc_entry_ref(new_entry);
	} else if (old_entry != NULL) {
		mc_reply(command, "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n");
	} else {
		mc_reply(command, "NOT_FOUND\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_decr(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.inc.key.str;
	size_t key_len = command->params.inc.key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_lookup(index, key, key_len);
	uint64_t value;

	struct mc_entry *new_entry = NULL;
	if (old_entry != NULL && mc_entry_value_u64(old_entry, &value)) {
		if (value > command->params.inc.value)
			value -= command->params.inc.value;
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

	if (command->params.inc.noreply) {
		mc_blank(command);
	} else if (new_entry != NULL) {
		command->result_type = MC_RESULT_VALUE;
		command->result.entry = new_entry;
		mc_entry_ref(new_entry);
	} else if (old_entry != NULL) {
		mc_reply(command, "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n");
	} else {
		mc_reply(command, "NOT_FOUND\r\n");
	}

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_delete(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	const char *key = command->params.del.key.str;
	size_t key_len = command->params.del.key.len;

	uint32_t index = mc_table_key_index(key, key_len);
	struct mc_entry *old_entry = mc_table_remove(index, key, key_len);

	if (command->params.del.noreply) {
		mc_blank(command);
	} else if (old_entry != NULL) {
		mc_reply(command, "DELETED\r\n");
	} else {
		mc_reply(command, "NOT_FOUND\r\n");
	}

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
	mc_reply(command, "SERVER_ERROR not implemented\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_flush_all(uintptr_t arg)
{
	ENTER();

	struct mc_command *command = (struct mc_command *) arg;
	mc_reply(command, "SERVER_ERROR not implemented\r\n");

	LEAVE();
	return 0;
}

static mm_result_t
mc_process_command(struct mc_state *state, struct mc_command *command)
{
	ENTER();

	if (likely(command->desc != NULL)) {
		DEBUG("command %s", command->desc->name);
		if (command->result_type == MC_RESULT_NONE) {
			command->desc->process((intptr_t) command);
		}
	}

	mc_queue_command(state, command);
	mm_net_spawn_writer(state->sock);

	// If the command completion requires execution
	// of other tasks give them a chance to run.
	//mm_task_yield();
	//mm_task_yield();

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
	char *start_ptr;
	char *end_ptr;

	struct mm_chunk *buffer;

	struct mc_command *command;
	struct mc_state *state;

	bool error;
};

/*
 * Prepare for parsing a command.
 */
static void
mc_start_input(struct mc_parser *parser,
	       struct mc_state *state,
	       struct mc_command *command)
{
	ENTER();

	struct mm_chunk *buffer = state->read_head;
	if (state->start_ptr == NULL) {
		parser->start_ptr = buffer->data;
	} else {
		while (!mc_buffer_contains(buffer, state->start_ptr))
			buffer = buffer->next;
		parser->start_ptr = state->start_ptr;
	}
	parser->end_ptr = buffer->data + buffer->used;

	parser->buffer = buffer;

	parser->state = state;
	parser->command = command;
	parser->error = false;

	LEAVE();
}

static void
mc_end_input(struct mc_parser *parser)
{
	ENTER();

	parser->command->end_ptr = parser->start_ptr;
	parser->state->start_ptr = parser->start_ptr;

	LEAVE();
}

/*
 * Move to the next input buffer.
 */
static bool
mc_shift_input(struct mc_parser *parser)
{
	ENTER();

	struct mm_chunk *buffer = parser->buffer->next;
	bool rc = (buffer != NULL && buffer->used > 0);
	if (rc) {
		parser->buffer = buffer;
		parser->start_ptr = buffer->data;
		parser->end_ptr = buffer->data + buffer->used;
	}

	LEAVE();
	return rc;
}

/*
 * Update parser after reading more data into the input buffer.
 */
static void
mc_widen_input(struct mc_parser *parser)
{
	ENTER();

	struct mm_chunk *buffer = parser->buffer;
	parser->end_ptr = buffer->data + buffer->used;

	LEAVE();
}

/* 
 * Carry the param over to the next input buffer. Create one if there is
 * no already.
 */
static void
mc_carry_param(struct mc_parser *parser, int count)
{
	ENTER();

	struct mm_chunk *buffer = parser->buffer->next;
	if (buffer == NULL) {
		buffer = mc_add_read_buffer(parser->state, MC_DEFAULT_BUFFER_SIZE);
	} else if (buffer->used > 0) {
		ASSERT((buffer->size - buffer->used) >= MC_KEY_LEN_MAX);
		memmove(buffer->data + count, buffer->data, buffer->used);
	}

	memcpy(buffer->data, parser->start_ptr, count);
	memset(parser->start_ptr, ' ', count);
	buffer->used += count;

	parser->buffer = buffer;
	parser->start_ptr = buffer->data;
	parser->end_ptr = buffer->data + buffer->used;

	LEAVE();
}

/*
 * Ask for the next input buffer with additional sanity checking.
 */
static bool
mc_claim_input(struct mc_parser *parser, int count)
{
	if (count > 1024) {
		/* The client looks insane. Quit fast. */
		parser->command->result_type = MC_RESULT_QUIT;
		parser->state->quit = true;
		return false;
	}
	return mc_shift_input(parser);
}

static int
mc_peek_input(struct mc_parser *parser, char *s, char *e)
{
	ASSERT(mc_buffer_contains(parser->buffer, s));
	ASSERT(e == (parser->buffer->data + parser->buffer->used));

	if ((s + 1) < e)
		return s[1];
	if (parser->buffer->next && parser->buffer->next->used > 0)
		return parser->buffer->next->data[0];
	return 256; /* a non-char */
}

static bool
mc_parse_space(struct mc_parser *parser)
{
	ENTER();

	bool rc = true;

	/* The count of scanned chars. Used to check if the client sends
	   too much junk data. */
	int count = 0;

	for (;;) {
		char *s = parser->start_ptr;
		char *e = parser->end_ptr;

		for (; s < e; s++) {
			if (*s != ' ') {
				parser->start_ptr = s;
				goto leave;
			}
		}

		count += parser->end_ptr - parser->start_ptr;
		if (!mc_claim_input(parser, count)) {
			rc = false;
			goto leave;
		}
	}

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_error(struct mc_parser *parser, const char *error_string)
{
	ENTER();

	/* Initialize the result. */
	bool rc = true;
	parser->error = true;

	/* The count of scanned chars. Used to check if the client sends
	   too much junk data. */
	int count = 0;

	for (;;) {
		char *s = parser->start_ptr;
		char *e = parser->end_ptr;

		/* Scan input for a newline. */
		char *p = memchr(s, '\n', e - s);
		if (p != NULL) {
			/* Go past the newline ready for the next command. */
			parser->start_ptr = p + 1;
			/* Report the error. */
			mc_reply(parser->command, error_string);
			break;
		}

		count += parser->end_ptr - parser->start_ptr;
		if (!mc_claim_input(parser, count)) {
			/* The line is not complete, wait for completion
			   before reporting error. */
			rc = false;
			break;
		}
	}

	LEAVE();
	return rc;
}

static bool
mc_parse_eol(struct mc_parser *parser)
{
	ENTER();

	/* Skip spaces. */
	bool rc = mc_parse_space(parser);
	if (!rc) {
		goto leave;
	}

	char *s = parser->start_ptr;
	char *e = parser->end_ptr;

	int c = *s;

	/* Check for optional CR char and required LF. */
	if ((c == '\r' && mc_peek_input(parser, s, e) == '\n') || c == '\n') {
		/* All right, got the line end. */
		if (c == '\r' && ++s == e) {
			struct mm_chunk *buffer = parser->buffer->next;
			parser->buffer = buffer;
			parser->start_ptr = buffer->data + 1;
			parser->end_ptr = buffer->data + buffer->used;
		} else {
			parser->start_ptr = s + 1;
		}
	} else {
		/* Oops, unexpected char. */
		rc = mc_parse_error(parser, "CLIENT_ERROR unexpected parameter\r\n");
	}

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_param(struct mc_parser *parser, struct mc_string *value, bool required)
{
	ENTER();

	bool rc = mc_parse_space(parser);
	if (!rc) {
		goto leave;
	}

	char *s, *e;

retry:
	s = parser->start_ptr;
	e = parser->end_ptr;
	for (; s < e; s++) {
		int c = *s;
		if (c == ' ' || (c == '\r' && mc_peek_input(parser, s, e) == '\n') || c == '\n') {
			int count = s - parser->start_ptr;
			if (required && count == 0) {
				rc = mc_parse_error(parser, "CLIENT_ERROR missing parameter\r\n");
			} else if (count > MC_KEY_LEN_MAX) {
				rc = mc_parse_error(parser, "CLIENT_ERROR parameter is too long\r\n");
			} else {
				value->len = count;
				value->str = parser->start_ptr;
				parser->start_ptr = s;

				DEBUG("%.*s", (int) value->len, value->str);
			}
			goto leave;
		}
	}

	DEBUG("buffer size: %d, used: %d",
	      (int) parser->buffer->size,
	      (int) parser->buffer->used);

	int count = e - parser->start_ptr;
	if (count > MC_KEY_LEN_MAX) {
		rc = mc_parse_error(parser, "CLIENT_ERROR parameter is too long\r\n");
	} else if (parser->buffer->used < parser->buffer->size) {
		rc = false;
	} else {
		mc_carry_param(parser, count);
		goto retry;
	}

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
	if (!rc) {
		goto leave;
	}

	char *s, *e;
	s = parser->start_ptr;
	e = parser->end_ptr;

	char *t = "noreply";

	int n = e - s;
	if (n > 7) {
		n = 7;
	} else if (n < 7) {
		if (memcmp(s, t, n) != 0) {
			*value = false;
			goto leave;
		}

		if (!mc_shift_input(parser)) {
			rc = false;
			goto leave;
		}
		s = parser->start_ptr;
		e = parser->end_ptr;

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
	parser->start_ptr += n;

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_data(struct mc_parser *parser, struct mc_value *data, uint32_t bytes)
{
	ENTER();
	DEBUG("bytes: %d", bytes);

	bool rc = true;
	uint32_t cr = 1;

	/* Save current input buffer position. */
  	data->buffer = parser->buffer;
	data->start = parser->start_ptr;

	for (;;) {
		uint32_t avail = parser->end_ptr - parser->start_ptr;
		DEBUG("parse data: avail = %ld, bytes = %ld", (long) avail, (long) bytes);
		if (avail > bytes) {
			parser->start_ptr += bytes;
			avail -= bytes;
			bytes = 0;

			if (parser->start_ptr[0] == '\n') {
				parser->start_ptr++;
				break;
			}

			if (!cr
			    || parser->start_ptr[0] != '\r'
			    || (avail > 1 && parser->start_ptr[1] != '\n')) {
				parser->error = true;
				mc_reply(parser->command,
					 "CLIENT_ERROR bad data chunk\r\n");
			}

			if (!cr || avail > 1) {
				parser->start_ptr++;
				if (cr)
					parser->start_ptr++;
				break;
			}

			parser->start_ptr++;
			cr = 0;
		} else {
			parser->start_ptr += avail;
			bytes -= avail;
		}

		if (!mc_shift_input(parser)) {
			bool hangup;
			ssize_t r = bytes + 1;
			ssize_t n = mc_read(parser->state, r, cr, &hangup);
			if (n < r) {
				parser->command->result_type = MC_RESULT_QUIT;
				rc = false;
				break;
			}
			mc_widen_input(parser);
		}
	}

	LEAVE();
	return rc;
}

static bool
mc_parse_command(struct mc_parser *parser)
{
	ENTER();

	enum scan {
		SCAN_START,
		SCAN_CMD,
		SCAN_CMD_GE,
		SCAN_CMD_GET,
		SCAN_CMD_DE,
		SCAN_CMD_VE,
		SCAN_CMD_VER,
		SCAN_CMD_REST,
	};

	/* Initialize the result. */
	bool rc = mc_parse_space(parser);
	if (!rc) {
		goto leave;
	}

	/* Get current position in the input buffer. */
	char *s = parser->start_ptr;
	char *e = parser->end_ptr;

	/* Must have some ready input at this point. */
	ASSERT(s < e);
	DEBUG("'%.*s'", (int) (e - s), s);

	/* Initialize the scanner state. */
	enum scan scan = SCAN_START;
	int cmd_first = -1;
	char *cmd_rest = "";

	/* The count of scanned chars. Used to check if the client sends
	   too much junk data. */
	int count = 0;

	for (;;) {
		int c = *s;
		switch (scan) {
		case SCAN_START:
			if (likely(c != '\n')) {
				/* Got the first command char. */
				scan = SCAN_CMD;
				cmd_first = c;
				goto next;
			} else {
				/* Unexpected line end. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD:
#define C(a, b) (((a) << 8) | (b))
			switch (C(cmd_first, c)) {
			case C('g', 'e'):
				scan = SCAN_CMD_GE;
				goto next;
			case C('s', 'e'):
				parser->command->desc = &mc_desc_set;
				scan = SCAN_CMD_REST;
				cmd_rest = "t";
				goto next;
			case C('a', 'd'):
				parser->command->desc = &mc_desc_add;
				scan = SCAN_CMD_REST;
				cmd_rest = "d";
				goto next;
			case C('r', 'e'):
				parser->command->desc = &mc_desc_replace;
				scan = SCAN_CMD_REST;
				cmd_rest = "place";
				goto next;
			case C('a', 'p'):
				parser->command->desc = &mc_desc_append;
				scan = SCAN_CMD_REST;
				cmd_rest = "pend";
				goto next;
			case C('p', 'r'):
				parser->command->desc = &mc_desc_prepend;
				scan = SCAN_CMD_REST;
				cmd_rest = "epend";
				goto next;
			case C('c', 'a'):
				parser->command->desc = &mc_desc_cas;
				scan = SCAN_CMD_REST;
				cmd_rest = "s";
				goto next;
			case C('i', 'n'):
				parser->command->desc = &mc_desc_incr;
				scan = SCAN_CMD_REST;
				cmd_rest = "cr";
				goto next;
			case C('d', 'e'):
				scan = SCAN_CMD_DE;
				goto next;
			case C('t', 'o'):
				parser->command->desc = &mc_desc_touch;
				scan = SCAN_CMD_REST;
				cmd_rest = "uch";
				goto next;
			case C('s', 'l'):
				parser->command->desc = &mc_desc_slabs;
				scan = SCAN_CMD_REST;
				cmd_rest = "abs";
				goto next;
			case C('s', 't'):
				parser->command->desc = &mc_desc_stats;
				scan = SCAN_CMD_REST;
				cmd_rest = "ats";
				goto next;
			case C('f', 'l'):
				parser->command->desc = &mc_desc_flush_all;
				scan = SCAN_CMD_REST;
				cmd_rest = "ush_all";
				goto next;
			case C('v', 'e'):
				scan = SCAN_CMD_VE;
				goto next;
			case C('q', 'u'):
				parser->command->desc = &mc_desc_quit;
				scan = SCAN_CMD_REST;
				cmd_rest = "it";
				goto next;
			default:
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}
#undef C

		case SCAN_CMD_GE:
			if (likely(c == 't')) {
				scan = SCAN_CMD_GET;
				goto next;
			} else {
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD_GET:
			if (c == ' ') {
				parser->command->desc = &mc_desc_get;
				parser->start_ptr = s;
				goto leave;
			} else if (c == 's') {
				parser->command->desc = &mc_desc_gets;
				/* Scan one char more with empty "rest" string
				   to verify that the command name ends here. */
				scan = SCAN_CMD_REST;
				goto next;
			} else if (c == '\r' || c == '\n') {
				/* Well, this turns out to be a get command
				   without arguments, albeit pointless this
				   is actually legal. */
				parser->command->desc = &mc_desc_get;
				parser->start_ptr = s;
				goto leave;
			} else {
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD_DE:
			if (likely(c == 'c')) {
				parser->command->desc = &mc_desc_decr;
				scan = SCAN_CMD_REST;
				cmd_rest = "r";
				goto next;
			} else if (likely(c == 'l')) {
				parser->command->desc = &mc_desc_delete;
				scan = SCAN_CMD_REST;
				cmd_rest = "ete";
				goto next;
			} else {
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD_VE:
			if (c == 'r') {
				scan = SCAN_CMD_VER;
				goto next;
			} else {
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD_VER:
			if (c == 's') {
				parser->command->desc = &mc_desc_version;
				scan = SCAN_CMD_REST;
				cmd_rest = "ion";
				goto next;
			} else if (c == 'b') {
				parser->command->desc = &mc_desc_verbosity;
				scan = SCAN_CMD_REST;
				cmd_rest = "osity";
				goto next;
			} else {
				/* Unexpected char. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}

		case SCAN_CMD_REST:
			if (c == *cmd_rest) {
				if (unlikely(c == 0)) {
					/* Hmm, zero byte in the input. */
					parser->start_ptr = s;
					rc = mc_parse_error(parser, "ERROR\r\n");
					goto leave;
				}
				/* So far so good. */
				cmd_rest++;
				goto next;
			} else if (*cmd_rest != 0) {
				/* Unexpected char in the command name. */
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			} else if (c == ' ') {
				/* Success. */
				parser->start_ptr = s;
				goto leave;
			} else if (c == '\r' || c == '\n') {
				/* Success. */
				parser->start_ptr = s;
				goto leave;
			} else {
				/* Unexpected char after the command name. */
				parser->start_ptr = s;
				rc = mc_parse_error(parser, "ERROR\r\n");
				goto leave;
			}
		}

	next:
		if (++s == e) {
			count += parser->end_ptr - parser->start_ptr;
			if (!mc_claim_input(parser, count)) {
				rc = false;
				goto leave;
			}

			s = parser->start_ptr;
			e = parser->end_ptr;
		}
	}

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_get(struct mc_parser *parser)
{
	ENTER();

	parser->command->params.get.keys = NULL;
	parser->command->params.get.nkeys = 0;

	bool rc;
	int nkeys = 0, nkeys_max = 8;
	struct mc_string *keys = mm_alloc(nkeys_max * sizeof(struct mc_string));
	// TODO: free it

	for (;;) {
		rc = mc_parse_param(parser, &keys[nkeys], nkeys == 0);
		if (!rc || parser->error) {
			mm_free(keys);
			goto leave;
		}

		if (keys[nkeys].len == 0) {
			break;
		}

		if (++nkeys == nkeys_max) {
			nkeys_max += 8;
			keys = mm_realloc(keys, nkeys_max * sizeof(struct mc_string));
		}
	}

	rc = mc_parse_eol(parser);
	if (!rc || parser->error) {
		mm_free(keys);
		goto leave;
	}

	parser->command->params.get.keys = keys;
	parser->command->params.get.nkeys = nkeys;

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_set(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->params.set.key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.flags);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.exptime);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.set.value.bytes);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->params.set.noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_data(parser,
		&parser->command->params.set.value,
		parser->command->params.set.value.bytes);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_cas(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->params.cas.key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.cas.flags);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.cas.exptime);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.cas.value.bytes);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u64(parser, &parser->command->params.cas.cas);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->params.cas.noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_data(parser,
		&parser->command->params.cas.value,
		parser->command->params.cas.value.bytes);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_incr(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->params.inc.key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u64(parser, &parser->command->params.inc.value);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->params.inc.noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_delete(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->params.del.key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->params.del.noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_touch(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_param(parser, &parser->command->params.touch.key, true);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_u32(parser, &parser->command->params.touch.exptime);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &parser->command->params.touch.noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_slabs(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_error(parser, "CLIENT_ERROR not implemented\r\n");

	LEAVE();
	return rc;
}

static bool
mc_parse_stats(struct mc_parser *parser)
{
	ENTER();

	bool rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;

	mc_reply(parser->command, "END\r\n");

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_flush_all(struct mc_parser *parser)
{
	ENTER();

	uint32_t exptime = 0;
	bool noreply = false;
	struct mc_string param;

	bool rc = mc_parse_param(parser, &param, false);
	if (rc && !parser->error && param.len) {
		char *endp;
		unsigned long v = strtoul(param.str, &endp, 10);
		if (endp < param.str + param.len) {
			if (param.len == 7 && memcmp(param.str, "noreply", 7) == 0) {
				noreply = true;
			} else {
				rc = mc_parse_error(parser, "CLIENT_ERROR invalid number parameter\r\n");
				goto leave;
			}
		} else {
			exptime = v;

			rc = mc_parse_noreply(parser, &noreply);
			if (!rc || parser->error)
				goto leave;
		}
	}
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;

	// TODO: really use the exptime.
	mc_exptime = mc_curtime + exptime * 1000000ull;

	// TODO: do this as a background task.
	while (!mm_list_empty(&mc_entry_list)) {
		struct mm_list *link = mm_list_head(&mc_entry_list);
		struct mc_entry *entry = containerof(link, struct mc_entry, link);

		char *key = mc_entry_key(entry);
		uint32_t index = mc_table_key_index(key, entry->key_len);
		mc_table_remove(index, key, entry->key_len);

		mc_entry_unref(entry);
	}

	if (noreply) {
		mc_blank(parser->command);
	} else {
		mc_reply(parser->command, "OK\r\n");
	}

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_verbosity(struct mc_parser *parser)
{
	ENTER();

	uint32_t verbose;
	bool noreply;

	bool rc = mc_parse_u32(parser, &verbose);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_noreply(parser, &noreply);
	if (!rc || parser->error)
		goto leave;
	rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;

	mc_verbose = (int) (verbose < 2 ? verbose : 2);

	if (noreply) {
		mc_blank(parser->command);
	} else {
		mc_reply(parser->command, "OK\r\n");
	}

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_version(struct mc_parser *parser)
{
	ENTER();
	
	bool rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;

	mc_reply(parser->command, "VERSION 0.0\r\n");

leave:
	LEAVE();
	return rc;
}

static bool
mc_parse_quit(struct mc_parser *parser)
{
	ENTER();
	
	bool rc = mc_parse_eol(parser);
	if (!rc || parser->error)
		goto leave;

	parser->command->result_type = MC_RESULT_QUIT;

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
	rc = parser->command->desc->parse(parser);

leave:
	LEAVE();
	return rc;
}

/**********************************************************************
 * Transmitting command results.
 **********************************************************************/

static void
mc_transmit_unref(struct mm_buffer *buf __attribute__((unused)),
		  uintptr_t data)
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
	case MC_RESULT_ENTRY_CAS:
		for (uint32_t i = 0; i < command->result.entries.nentries; i++) {
			struct mc_entry *entry = command->result.entries.entries[i];
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

			mm_buffer_append(&state->tbuf, "\r\n", 2);
		}
		mm_buffer_append(&state->tbuf, "END\r\n", 5);
		break;
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

static bool
mc_transmit(struct mc_state *state)
{
	ssize_t n = mm_net_writebuf(state->sock, &state->tbuf);
	if (n < 0 && errno != EINVAL && errno != EAGAIN && errno != EWOULDBLOCK)
		return false;
	return true;
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
mc_clean_read_buffer(struct mc_state *state)
{
	struct mm_chunk *buffer = state->read_head;
	if (buffer != NULL
	    && state->start_ptr == state->clear_ptr
	    && state->start_ptr == buffer->data + buffer->used) {
		state->start_ptr = buffer->data;
		state->clear_ptr = NULL;
		buffer->used = 0;
	}
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

			mc_clean_read_buffer(state);

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
				parser.command->end_ptr = parser.start_ptr;
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
	if (state == NULL)
		goto leave;

	// Buffer all the ready command results.
	struct mc_command *command = state->command_head;
	while (command != NULL && command->result_type != MC_RESULT_NONE) {
		struct mc_command *next = command->next;
		if (next != NULL)
			state->command_head = next;
		else
			state->command_head = state->command_tail = NULL;

		// Free the receive buffers.
		mc_release_buffers(state, command->end_ptr);

		// Put the result into the transmit buffer.
		mc_transmit_buffer(state, command);

		// Free no longer needed command.
		mc_command_destroy(command);
		command = next;
	}

	// Transmit buffered results.
	mc_transmit(state);

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
