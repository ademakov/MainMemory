/*
 * base/json.h - JSON pull parser.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef BASE_JSON_H
#define BASE_JSON_H

#include "common.h"
#include "base/memory/arena.h"

typedef enum mm_json_token {

	/* Internal token of initial reader state. */
	MM_JSON_INITIAL = 0,

	/* Input data is incomplete. */
	MM_JSON_PARTIAL,

	/* Input data is invalid. */
	MM_JSON_ERROR,

	MM_JSON_START_DOCUMENT,
	MM_JSON_END_DOCUMENT,

	MM_JSON_START_OBJECT,
	MM_JSON_END_OBJECT,

	MM_JSON_START_ARRAY,
	MM_JSON_END_ARRAY,

	MM_JSON_NAME,

	MM_JSON_STRING,
	MM_JSON_NUMBER,
	MM_JSON_FALSE,
	MM_JSON_TRUE,
	MM_JSON_NULL,

} mm_json_token_t;

typedef enum mm_json_state {

	MM_JSON_STATE_VALUE,

	MM_JSON_STATE_SPACE,

	MM_JSON_STATE_ARRAY,
	MM_JSON_STATE_ARRAY_VALUE_SEP,

	MM_JSON_STATE_OBJECT,
	MM_JSON_STATE_OBJECT_NAME,
	MM_JSON_STATE_OBJECT_NAME_SEP,
	MM_JSON_STATE_OBJECT_VALUE_SEP,

} mm_json_state_t;

struct mm_json_reader
{
	mm_json_token_t token;
	mm_json_state_t state;

	/* A string or numeric value. */
	const char *value;
	const char *value_end;

	/* The current read position. */
	const char *ptr;
	const char *end;

	/* Input data buffer. */
	const char *input;
	const char *input_end;

	/* Internal data buffer. */
	char *buffer;
	char *buffer_end;
	size_t buffer_size;

	union
	{
		struct
		{
			bool fraction;
			bool exponent;
		} number;

		struct
		{
			bool escaped;
			bool highbit;
		} string;
	} extra;

	size_t stack_top;
	size_t stack_max;
	union
	{
		uintptr_t small_stack;
		uintptr_t *large_stack;
	};

	mm_arena_t arena;
};

void __attribute__((nonnull(1)))
mm_json_reader_prepare(struct mm_json_reader *reader, mm_arena_t arena);

void __attribute__((nonnull(1)))
mm_json_reader_cleanup(struct mm_json_reader *reader);

void __attribute__((nonnull(1, 2)))
mm_json_reader_feed(struct mm_json_reader *reader, const void *input, size_t input_size);

mm_json_token_t __attribute__((nonnull(1)))
mm_json_reader_next(struct mm_json_reader *reader);

#endif /* BASE_JSON_H */
