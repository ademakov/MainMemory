/*
 * memcache/command.h - MainMemory memcache commands.
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

#ifndef MEMCACHE_COMMAND_H
#define MEMCACHE_COMMAND_H

#include "memcache/memcache.h"
#include "memcache/action.h"

/**********************************************************************
 * Command type declarations.
 **********************************************************************/

enum mc_command_kind {
	/* Entry lookup command. */
	MC_COMMAND_LOOKUP,
	/* Entry storage command. */
	MC_COMMAND_STORAGE,
	/* Entry append/prepend command. */
	MC_COMMAND_CONCAT,
	/* Entry delete command. */
	MC_COMMAND_DELETE,
	/* Entry inc/dec command. */
	MC_COMMAND_DELTA,
	/* Entry touch command. */
	MC_COMMAND_TOUCH,
	/* Table flush command. */
	MC_COMMAND_FLUSH,
	/* Non-entry command. */
	MC_COMMAND_CUSTOM,
	/* Bad command. */
	MC_COMMAND_ERROR,
};

/*
 * Some preprocessor magic to emit command definitions.
 */

#define MC_COMMAND_LIST(_)				\
	_(ascii_get,		MC_COMMAND_LOOKUP)	\
	_(ascii_gets,		MC_COMMAND_LOOKUP)	\
	_(ascii_set,		MC_COMMAND_STORAGE)	\
	_(ascii_add,		MC_COMMAND_STORAGE)	\
	_(ascii_replace,	MC_COMMAND_STORAGE)	\
	_(ascii_append,		MC_COMMAND_CONCAT)	\
	_(ascii_prepend,	MC_COMMAND_CONCAT)	\
	_(ascii_cas,		MC_COMMAND_STORAGE)	\
	_(ascii_incr,		MC_COMMAND_DELTA)	\
	_(ascii_decr,		MC_COMMAND_DELTA)	\
	_(ascii_delete,		MC_COMMAND_DELETE)	\
	_(ascii_touch,		MC_COMMAND_TOUCH)	\
	_(ascii_slabs,		MC_COMMAND_CUSTOM)	\
	_(ascii_stats,		MC_COMMAND_CUSTOM)	\
	_(ascii_flush_all,	MC_COMMAND_FLUSH)	\
	_(ascii_version,	MC_COMMAND_CUSTOM)	\
	_(ascii_verbosity,	MC_COMMAND_CUSTOM)	\
	_(ascii_quit,		MC_COMMAND_CUSTOM)	\
	_(ascii_error,		MC_COMMAND_ERROR)	\
	_(binary_get,		MC_COMMAND_LOOKUP)	\
	_(binary_getq,		MC_COMMAND_LOOKUP)	\
	_(binary_getk,		MC_COMMAND_LOOKUP)	\
	_(binary_getkq,		MC_COMMAND_LOOKUP)	\
	_(binary_set,		MC_COMMAND_STORAGE)	\
	_(binary_setq,		MC_COMMAND_STORAGE)	\
	_(binary_add,		MC_COMMAND_STORAGE)	\
	_(binary_addq,		MC_COMMAND_STORAGE)	\
	_(binary_replace,	MC_COMMAND_STORAGE)	\
	_(binary_replaceq,	MC_COMMAND_STORAGE)	\
	_(binary_append,	MC_COMMAND_CONCAT)	\
	_(binary_appendq,	MC_COMMAND_CONCAT)	\
	_(binary_prepend,	MC_COMMAND_CONCAT)	\
	_(binary_prependq,	MC_COMMAND_CONCAT)	\
	_(binary_increment,	MC_COMMAND_DELTA)	\
	_(binary_incrementq,	MC_COMMAND_DELTA)	\
	_(binary_decrement,	MC_COMMAND_DELTA)	\
	_(binary_decrementq,	MC_COMMAND_DELTA)	\
	_(binary_delete,	MC_COMMAND_DELETE)	\
	_(binary_deleteq,	MC_COMMAND_DELETE)	\
	_(binary_noop,		MC_COMMAND_CUSTOM)	\
	_(binary_quit,		MC_COMMAND_CUSTOM)	\
	_(binary_quitq,		MC_COMMAND_CUSTOM)	\
	_(binary_flush,		MC_COMMAND_FLUSH)	\
	_(binary_flushq,	MC_COMMAND_FLUSH)	\
	_(binary_version,	MC_COMMAND_CUSTOM)	\
	_(binary_stat,		MC_COMMAND_CUSTOM)	\
	_(binary_error,		MC_COMMAND_ERROR)

/*
 * Declare command handling info.
 */

struct mc_state;
struct mc_command;

typedef void (*mc_command_execute_t)(struct mc_state *state,
				     struct mc_command *command);

struct mc_command_type
{
	mc_command_execute_t exec;
	uint32_t kind;
	const char *name;
};

#define MC_COMMAND_TYPE(cmd, value)	\
	extern struct mc_command_type mc_command_##cmd;

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Command data.
 **********************************************************************/

struct mc_command_slabs
{
};

struct mc_command_stats
{
};

struct mc_command
{
	struct mc_command *next;
	struct mc_command_type *type;
	struct mc_action action;

	union
	{
		struct
		{
			bool noreply;
			bool last;
		} ascii;

		struct
		{
			uint32_t opaque;
			uint8_t opcode;
		} binary;
	};

	union
	{
		struct
		{
			uint64_t value;
			uint64_t delta;
		};
		struct mc_command_slabs slabs;
		struct mc_command_stats stats;
	};

	union
	{
		uint32_t exp_time;
		uint32_t nopts;
	};

	/* Action key memory is owned by the command. */
	bool own_key;
	/* Action value memory is owned by the command. */
	bool own_alter_value;
};

/**********************************************************************
 * Command routines.
 **********************************************************************/

struct mc_command * NONNULL(1)
mc_command_create(struct mc_state *state);

void NONNULL(1)
mc_command_destroy(struct mc_command *command);

static inline void NONNULL(1, 2)
mc_command_execute(struct mc_state *state, struct mc_command *command)
{
	(command->type->exec)(state, command);
}

#endif /* MEMCACHE_COMMAND_H */
