/*
 * memcache/command.h - MainMemory memcache commands.
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

#ifndef MEMCACHE_COMMAND_H
#define MEMCACHE_COMMAND_H

#include "memcache.h"
#include "future.h"

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

#define MC_ASYNC 1

/*
 * Some preprocessor magic to emit command definitions.
 */

#define MC_COMMAND_LIST(_)		\
	_(get,		MC_ASYNC)	\
	_(gets,		MC_ASYNC)	\
	_(set,		MC_ASYNC)	\
	_(add,		MC_ASYNC)	\
	_(replace,	MC_ASYNC)	\
	_(append,	MC_ASYNC)	\
	_(prepend,	MC_ASYNC)	\
	_(cas,		MC_ASYNC)	\
	_(incr,		MC_ASYNC)	\
	_(decr,		MC_ASYNC)	\
	_(delete,	MC_ASYNC)	\
	_(touch,	MC_ASYNC)	\
	_(slabs,	0)		\
	_(stats,	0)		\
	_(flush_all,	0)		\
	_(version,	0)		\
	_(verbosity,	0)		\
	_(quit,		0)

/*
 * Define enumerated type to tag commands.
 */

#define MC_COMMAND_TAG(cmd, value)	mc_command_##cmd,

typedef enum {
	MC_COMMAND_LIST(MC_COMMAND_TAG)
} mc_command_t;

#undef MC_COMMAND_TAG

/*
 * Declare command handling info.
 */

struct mc_command_type
{
	mc_command_t tag;
	mm_routine_t exec;
	uint32_t flags;
};

#define MC_COMMAND_TYPE(cmd, value)	\
	extern struct mc_command_type mc_desc_##cmd;

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Command data.
 **********************************************************************/

typedef enum
{
	MC_RESULT_NONE = 0,
#if ENABLE_SMP && !ENABLE_MEMCACHE_LOCKS
	MC_RESULT_FUTURE,
#endif

	MC_RESULT_BLANK,
	MC_RESULT_OK,
	MC_RESULT_END,
	MC_RESULT_ERROR,
	MC_RESULT_EXISTS,
	MC_RESULT_STORED,
	MC_RESULT_DELETED,
	MC_RESULT_TOUCHED,
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

} mc_command_result_t;

struct mc_command_key
{
	size_t len;
	const char *str;
};

struct mc_command_params_set
{
	struct mm_buffer_segment *seg;
	const char *start;
	uint32_t bytes;

	uint32_t flags;
	uint32_t exptime;
	uint64_t cas;
};

struct mc_command_params_slabs
{
	uint32_t nopts;
};

struct mc_command_params_stats
{
	uint32_t nopts;
};

union mc_command_params
{
	struct mc_command_params_set set;
	struct mc_command_params_slabs slabs;
	struct mc_command_params_stats stats;
	struct mm_net_socket *sock;
	uint64_t val64;
	uint32_t val32;
	bool last;
};

struct mc_command
{
	struct mc_command_type *type;
	struct mc_command_key key;
	union mc_command_params params;
	mc_command_result_t result;
	struct mc_entry *entry;
	uint32_t key_hash;
	bool noreply;
	bool own_key;

#if ENABLE_SMP && !ENABLE_MEMCACHE_LOCKS
	struct mm_future *future;
#endif

	struct mc_command *next;

	char *end_ptr;
};

const char * mc_command_name(mc_command_t tag);

/**********************************************************************
 * Command routines.
 **********************************************************************/

void mc_command_start(void);
void mc_command_stop(void);

struct mc_command * mc_command_create(mm_core_t core);
void mc_command_destroy(mm_core_t core, struct mc_command *command);

void mc_command_execute(struct mc_command *command);

static inline mc_command_result_t
mc_command_result(struct mc_command *command)
{
	mc_command_result_t result = command->result;
#if ENABLE_SMP && !ENABLE_MEMCACHE_LOCKS
	if (result == MC_RESULT_FUTURE) {
		result = mm_future_wait(command->future);
		if (mm_future_is_canceled(command->future))
			result = MC_RESULT_CANCELED;
		command->result = result;
	}
#endif
	return result;
}

#endif /* MEMCACHE_COMMAND_H */
