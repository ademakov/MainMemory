/*
 * memcache/command.h - MainMemory memcache commands.
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

#ifndef MEMCACHE_COMMAND_H
#define MEMCACHE_COMMAND_H

#include "memcache/memcache.h"
#include "memcache/action.h"

/* Forward declaration. */
struct mc_binary_header;

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

#define MC_COMMAND_LIST(_)					\
	_(ascii,  get,		simple,  MC_COMMAND_LOOKUP)	\
	_(ascii,  gets,		simple,  MC_COMMAND_LOOKUP)	\
	_(ascii,  set,		storage, MC_COMMAND_STORAGE)	\
	_(ascii,  add,		storage, MC_COMMAND_STORAGE)	\
	_(ascii,  replace,	storage, MC_COMMAND_STORAGE)	\
	_(ascii,  append,	storage, MC_COMMAND_CONCAT)	\
	_(ascii,  prepend,	storage, MC_COMMAND_CONCAT)	\
	_(ascii,  cas,		storage, MC_COMMAND_STORAGE)	\
	_(ascii,  incr,		storage, MC_COMMAND_DELTA)	\
	_(ascii,  decr,		storage, MC_COMMAND_DELTA)	\
	_(ascii,  delete,	simple,  MC_COMMAND_DELETE)	\
	_(ascii,  touch,	simple,  MC_COMMAND_TOUCH)	\
	_(ascii,  slabs,	simple,  MC_COMMAND_CUSTOM)	\
	_(ascii,  stats,	simple,  MC_COMMAND_CUSTOM)	\
	_(ascii,  flush_all,	simple,  MC_COMMAND_FLUSH)	\
	_(ascii,  version,	simple,  MC_COMMAND_CUSTOM)	\
	_(ascii,  verbosity,	simple,  MC_COMMAND_CUSTOM)	\
	_(ascii,  quit,		simple,  MC_COMMAND_CUSTOM)	\
	_(ascii,  error,	simple,  MC_COMMAND_ERROR)	\
	_(binary, get,		simple,  MC_COMMAND_LOOKUP)	\
	_(binary, getq,		simple,  MC_COMMAND_LOOKUP)	\
	_(binary, getk,		simple,  MC_COMMAND_LOOKUP)	\
	_(binary, getkq,	simple,  MC_COMMAND_LOOKUP)	\
	_(binary, set,		storage, MC_COMMAND_STORAGE)	\
	_(binary, setq,		storage, MC_COMMAND_STORAGE)	\
	_(binary, add,		storage, MC_COMMAND_STORAGE)	\
	_(binary, addq,		storage, MC_COMMAND_STORAGE)	\
	_(binary, replace,	storage, MC_COMMAND_STORAGE)	\
	_(binary, replaceq,	storage, MC_COMMAND_STORAGE)	\
	_(binary, append,	storage, MC_COMMAND_CONCAT)	\
	_(binary, appendq,	storage, MC_COMMAND_CONCAT)	\
	_(binary, prepend,	storage, MC_COMMAND_CONCAT)	\
	_(binary, prependq,	storage, MC_COMMAND_CONCAT)	\
	_(binary, increment,	storage, MC_COMMAND_DELTA)	\
	_(binary, incrementq,	storage, MC_COMMAND_DELTA)	\
	_(binary, decrement,	storage, MC_COMMAND_DELTA)	\
	_(binary, decrementq,	storage, MC_COMMAND_DELTA)	\
	_(binary, delete,	simple,  MC_COMMAND_DELETE)	\
	_(binary, deleteq,	simple,  MC_COMMAND_DELETE)	\
	_(binary, noop,		simple,  MC_COMMAND_CUSTOM)	\
	_(binary, quit,		simple,  MC_COMMAND_CUSTOM)	\
	_(binary, quitq,	simple,  MC_COMMAND_CUSTOM)	\
	_(binary, flush,	simple,  MC_COMMAND_FLUSH)	\
	_(binary, flushq,	simple,  MC_COMMAND_FLUSH)	\
	_(binary, version,	simple,  MC_COMMAND_CUSTOM)	\
	_(binary, stat,		simple,  MC_COMMAND_CUSTOM)	\
	_(binary, error,	simple,  MC_COMMAND_ERROR)

/*
 * Declare command handling info.
 */

struct mc_state;
struct mc_command_base;

typedef void (*mc_command_execute_t)(struct mc_state *state, struct mc_command_base *command);

struct mc_command_type
{
	mc_command_execute_t exec;
	uint32_t kind;
	const char *name;
};

#define MC_COMMAND_TYPE(proto, cmd, actn_kind, cmd_kind)		\
	extern struct mc_command_type mc_command_##proto##_##cmd;

MC_COMMAND_LIST(MC_COMMAND_TYPE)

#undef MC_COMMAND_TYPE

/**********************************************************************
 * Command data.
 **********************************************************************/

struct mc_command_base
{
	const struct mc_command_type *type;
	struct mc_command_base *next;
};

struct mc_command_simple
{
	struct mc_command_base base;
	struct mc_action action;
};

struct mc_command_storage
{
	struct mc_command_base base;
	struct mc_action_storage action;
	uint64_t binary_value;
	uint64_t binary_delta;
};

/**********************************************************************
 * Command routines.
 **********************************************************************/

struct mc_command_simple * NONNULL(1, 2)
mc_command_create_simple(struct mc_state *state, const struct mc_command_type *type);

struct mc_command_storage * NONNULL(1, 2)
mc_command_create_ascii_storage(struct mc_state *state, const struct mc_command_type *type);

struct mc_command_simple * NONNULL(1, 2, 3)
mc_command_create_binary_simple(struct mc_state *state, const struct mc_command_type *type, const struct mc_binary_header *header);

struct mc_command_storage * NONNULL(1, 2, 3)
mc_command_create_binary_storage(struct mc_state *state, const struct mc_command_type *type, const struct mc_binary_header *header);

static inline void NONNULL(1, 2)
mc_command_execute(struct mc_state *state, struct mc_command_base *command)
{
	(command->type->exec)(state, command);
}

static inline void NONNULL(1)
mc_command_cleanup(struct mc_command_base *command)
{
	mc_action_cleanup(&((struct mc_command_simple *) command)->action);
}

#endif /* MEMCACHE_COMMAND_H */
