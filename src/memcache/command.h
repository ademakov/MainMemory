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

#include "memcache/memcache.h"
#include "memcache/action.h"
#include "memcache/result.h"
#include "core/future.h"

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

/* A non-table command. */
#define MC_COMMAND_CUSTOM	0
/* A table lookup command. */
#define MC_COMMAND_LOOKUP	1
/* A table storage command. */
#define MC_COMMAND_STORAGE	2
/* A table update command. */
#define MC_COMMAND_UPDATE	3
/* A table update command. */
#define MC_COMMAND_DELETE	4

/*
 * Some preprocessor magic to emit command definitions.
 */

#define MC_COMMAND_LIST(_)			\
	_(get,		MC_COMMAND_LOOKUP)	\
	_(gets,		MC_COMMAND_LOOKUP)	\
	_(set,		MC_COMMAND_STORAGE)	\
	_(add,		MC_COMMAND_STORAGE)	\
	_(replace,	MC_COMMAND_STORAGE)	\
	_(append,	MC_COMMAND_STORAGE)	\
	_(prepend,	MC_COMMAND_STORAGE)	\
	_(cas,		MC_COMMAND_STORAGE)	\
	_(incr,		MC_COMMAND_UPDATE)	\
	_(decr,		MC_COMMAND_UPDATE)	\
	_(delete,	MC_COMMAND_DELETE)	\
	_(touch,	MC_COMMAND_UPDATE)	\
	_(slabs,	MC_COMMAND_CUSTOM)	\
	_(stats,	MC_COMMAND_CUSTOM)	\
	_(flush_all,	MC_COMMAND_CUSTOM)	\
	_(version,	MC_COMMAND_CUSTOM)	\
	_(verbosity,	MC_COMMAND_CUSTOM)	\
	_(quit,		MC_COMMAND_CUSTOM)

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
	uint32_t kind;
};

#define MC_COMMAND_TYPE(cmd, value)	\
	extern struct mc_command_type mc_desc_##cmd;

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Command data.
 **********************************************************************/

struct mc_command_params_slabs
{
	uint32_t nopts;
};

struct mc_command_params_stats
{
	uint32_t nopts;
};

struct mc_command_params_binary
{
	uint32_t opaque;
	uint8_t opcode;
};

union mc_command_params
{
	struct mc_command_params_slabs slabs;
	struct mc_command_params_stats stats;
	struct mc_command_params_binary binary;
	struct mm_net_socket *sock;
	uint64_t val64;
	uint32_t val32;
	bool last;
};

struct mc_command
{
	struct mc_command_type *type;
	struct mc_action action;
	union mc_command_params params;
	mc_result_t result;
	bool noreply;
	bool own_key;

#if ENABLE_MEMCACHE_DELEGATE
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

static inline mc_result_t
mc_command_result(struct mc_command *command)
{
	mc_result_t result = command->result;
#if ENABLE_MEMCACHE_DELEGATE
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
