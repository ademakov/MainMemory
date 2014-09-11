/*
 * memcache/parser.h - MainMemory memcache parser.
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

#ifndef MEMCACHE_PARSER_H
#define MEMCACHE_PARSER_H

#include "buffer.h"

/* Forward declaration. */
struct mc_state;

struct mc_parser
{
	struct mm_buffer_cursor cursor;
	struct mc_command *command;
	struct mc_state *state;
};

void mc_parser_start(struct mc_parser *parser, struct mc_state *state)
	__attribute__((nonnull(1, 2)));

bool mc_parser_parse(struct mc_parser *parser)
	__attribute__((nonnull(1)));

#endif /* MEMCACHE_PARSER_H */
