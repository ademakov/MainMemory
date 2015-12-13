/*
 * base/json.c - JSON pull parser.
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

#include "base/json.h"

#include "base/scan.h"
#include "base/log/debug.h"
#include "base/memory/memory.h"

#define MM_JSON_STACK_UNIT	(sizeof(uintptr_t) * 8)

/**********************************************************************
 * JSON reader initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_json_reader_prepare(struct mm_json_reader *reader, mm_arena_t arena)
{
	memset(reader, 0, sizeof(*reader));
	reader->stack_max = MM_JSON_STACK_UNIT;
	reader->arena = arena;
}

void NONNULL(1)
mm_json_reader_cleanup(struct mm_json_reader *reader)
{
	if (reader->buffer != NULL)
		mm_arena_free(reader->arena, reader->buffer);
	if (reader->stack_max > MM_JSON_STACK_UNIT)
		mm_arena_free(reader->arena, reader->large_stack);
}

/**********************************************************************
 * JSON reader stack maintenance.
 **********************************************************************/

static void
mm_json_reader_stack_push(struct mm_json_reader *reader, bool is_object)
{
	if (reader->stack_top == reader->stack_max) {
		reader->stack_max *= 2;
		size_t nbytes = reader->stack_max / 8;
		if (reader->stack_top > MM_JSON_STACK_UNIT) {
			reader->large_stack = mm_arena_realloc(reader->arena,
							       reader->large_stack,
							       nbytes);
		} else {
			uintptr_t *large_stack = mm_arena_alloc(reader->arena, nbytes);
			large_stack[0] = reader->small_stack;
			reader->large_stack = large_stack;
		}
	}

	size_t bit = reader->stack_top++;
	if (reader->stack_max > MM_JSON_STACK_UNIT) {
		uintptr_t unit = bit / MM_JSON_STACK_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_JSON_STACK_UNIT);
		if (is_object)
			reader->large_stack[unit] |= mask;
		else
			reader->large_stack[unit] &= ~mask;
	} else {
		uintptr_t mask = (uintptr_t) 1 << bit;
		if (is_object)
			reader->small_stack |= mask;
		else
			reader->small_stack &= ~mask;
	}
}

static bool
mm_json_reader_stack_get(struct mm_json_reader *reader)
{
	ASSERT(reader->stack_top > 0);
	size_t bit = reader->stack_top - 1;
	if (reader->stack_max > MM_JSON_STACK_UNIT) {
		uintptr_t unit = bit / MM_JSON_STACK_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_JSON_STACK_UNIT);
		return (reader->large_stack[unit] & mask) != 0;
	} else {
		uintptr_t mask = (uintptr_t) 1 << bit;
		return (reader->small_stack & mask) != 0;
	}
}

static void
mm_json_reader_stack_pop(struct mm_json_reader *reader)
{
	ASSERT(reader->stack_top > 0);
	reader->stack_top--;
}

/**********************************************************************
 * JSON reader input buffer maintenance.
 **********************************************************************/

/*
 * Save some data from the input buffer in the internal buffer.
 * If the internal buffer contains any non-consumed data then
 * the input buffer data is appended to it.
 */
static void NONNULL(1)
mm_json_reader_save_input(struct mm_json_reader *reader, const char *input_limit)
{
	ASSERT(input_limit >= reader->input && input_limit <= reader->input_end);

	const char *input;
	size_t start_size;
	if (reader->end == reader->buffer_end) {
		ASSERT(reader->ptr >= reader->buffer && reader->ptr <= reader->buffer_end);
		input = reader->input;
		start_size = reader->end - reader->ptr;
	} else {
		ASSERT(reader->ptr >= reader->input && reader->ptr <= input_limit);
		input = reader->ptr;
		start_size = 0;
	}

	size_t input_size = input_limit - input;
	size_t total_size = start_size + input_size;
	if (total_size > reader->buffer_size) {
		char *buffer = mm_arena_alloc(reader->arena, total_size);
		if (start_size)
			memcpy(buffer, reader->ptr, start_size);
		if (reader->buffer)
			mm_arena_free(reader->arena, reader->buffer);
		reader->buffer = buffer;
	} else if (start_size && reader->ptr != reader->buffer) {
		memmove(reader->buffer, reader->ptr, start_size);
	}

	if (input_size)
		memcpy(reader->buffer + start_size, input, input_size);
	reader->buffer_end = reader->buffer + total_size;

	reader->ptr = reader->buffer;
	reader->end = reader->buffer_end;
}

/*
 * Make sure that the requested number of bytes are immediately available
 * in the current buffer. Consolidate the data in the internal buffer if
 * necessary.
 */
static inline const char *
mm_json_reader_check_size(struct mm_json_reader *reader, size_t n_min)
{
	size_t n = reader->end - reader->ptr;
	if (unlikely(n < n_min)) {
		if (reader->end == reader->input_end)
			return NULL;

		size_t m = reader->input_end - reader->input;
		if ((n + m) < n_min)
			return NULL;

		const char *value_end = reader->input + n_min - n;
		mm_json_reader_save_input(reader, value_end);
		reader->input = value_end;
	}

	return reader->ptr;
}

void NONNULL(1, 2)
mm_json_reader_feed(struct mm_json_reader *reader, const void *input, size_t input_size)
{
	const char *input_end = (const char *) input + input_size;

	if (reader->ptr == reader->input_end) {
		reader->ptr = input;
		reader->end = input_end;
	} else {
		mm_json_reader_save_input(reader, reader->input_end);
	}

	reader->input = input;
	reader->input_end = input_end;
}

/**********************************************************************
 * JSON reader literal scan routines.
 **********************************************************************/

#define Cx3(a, b, c)		((a) | ((b) << 8) | ((c) << 16))
#define Cx4(a, b, c, d)		((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

static mm_json_token_t
mm_json_reader_scan_false(struct mm_json_reader *reader)
{
	const char *cp = mm_json_reader_check_size(reader, 5);
	if (unlikely(cp == NULL))
		return MM_JSON_PARTIAL;

	ASSERT(cp[0] == 'f');
	if (unlikely(Cx4(cp[1], cp[2], cp[3], cp[4]) != Cx4('a', 'l', 's', 'e')))
		return MM_JSON_INVALID;

	reader->ptr += 5;
	return MM_JSON_FALSE;
}

static mm_json_token_t
mm_json_reader_scan_true(struct mm_json_reader *reader)
{
	const char *cp = mm_json_reader_check_size(reader, 4);
	if (unlikely(cp == NULL))
		return MM_JSON_PARTIAL;

	ASSERT(cp[0] == 't');
	if (unlikely(Cx3(cp[1], cp[2], cp[3]) != Cx3('r', 'u', 'e')))
		return MM_JSON_INVALID;

	reader->ptr += 4;
	return MM_JSON_TRUE;
}

static mm_json_token_t
mm_json_reader_scan_null(struct mm_json_reader *reader)
{
	const char *cp = mm_json_reader_check_size(reader, 4);
	if (unlikely(cp == NULL))
		return MM_JSON_PARTIAL;

	ASSERT(cp[0] == 'n');
	if (unlikely(Cx3(cp[1], cp[2], cp[3]) != Cx3('u', 'l', 'l')))
		return MM_JSON_INVALID;

	reader->ptr += 4;
	return MM_JSON_NULL;
}

/**********************************************************************
 * JSON reader string scan routine.
 **********************************************************************/

typedef enum
{
	MM_JSON_SSTATE_NORMAL,
	MM_JSON_SSTATE_ESCAPE,
	MM_JSON_SSTATE_HEXNUM,

} mm_json_string_state_t;

typedef enum
{
	MM_JSON_SCTYPE_ERROR = 0,

	MM_JSON_SCTYPE_REGULAR,
	MM_JSON_SCTYPE_HIGHBIT,

	MM_JSON_SCTYPE_ESCAPE,
	MM_JSON_SCTYPE_ESC2CH,
	MM_JSON_SCTYPE_ESC6CH,
	MM_JSON_SCTYPE_HEXNUM,

	MM_JSON_SCTYPE_QUOTE,

} mm_json_string_ctype_t;

static uint8_t mm_json_string_table[][256] = {
#define C MM_JSON_SCTYPE_ERROR
#define R MM_JSON_SCTYPE_REGULAR
#define H MM_JSON_SCTYPE_HIGHBIT
#define E MM_JSON_SCTYPE_ESCAPE
#define Q MM_JSON_SCTYPE_QUOTE

	[MM_JSON_SSTATE_NORMAL] = {
		C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
		C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C,
		R, R, Q, R, R, R, R, R, R, R, R, R, R, R, R, R,
		R, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R,
		R, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R,
		R, R, R, R, R, R, R, R, R, R, R, R, E, R, R, R,
		R, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R,
		R, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R,

		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
		H, H, H, H, H, H, H, H, H, H, H, H, H, H, H, H,
	},

#undef C
#undef R
#undef H
#undef E
#undef Q

	[MM_JSON_SSTATE_ESCAPE] = {
		['b'] = MM_JSON_SCTYPE_ESC2CH, ['f'] = MM_JSON_SCTYPE_ESC2CH,
		['n'] = MM_JSON_SCTYPE_ESC2CH, ['r'] = MM_JSON_SCTYPE_ESC2CH,
		['t'] = MM_JSON_SCTYPE_ESC2CH, ['/'] = MM_JSON_SCTYPE_ESC2CH,
		['"'] = MM_JSON_SCTYPE_ESC2CH, ['\\'] = MM_JSON_SCTYPE_ESC2CH,
		['u'] = MM_JSON_SCTYPE_ESC6CH,
	},

	[MM_JSON_SSTATE_HEXNUM] = {
		['0'] = MM_JSON_SCTYPE_HEXNUM, ['1'] = MM_JSON_SCTYPE_HEXNUM,
		['2'] = MM_JSON_SCTYPE_HEXNUM, ['3'] = MM_JSON_SCTYPE_HEXNUM,
		['4'] = MM_JSON_SCTYPE_HEXNUM, ['5'] = MM_JSON_SCTYPE_HEXNUM,
		['6'] = MM_JSON_SCTYPE_HEXNUM, ['7'] = MM_JSON_SCTYPE_HEXNUM,
		['8'] = MM_JSON_SCTYPE_HEXNUM, ['9'] = MM_JSON_SCTYPE_HEXNUM,
		['a'] = MM_JSON_SCTYPE_HEXNUM, ['b'] = MM_JSON_SCTYPE_HEXNUM,
		['c'] = MM_JSON_SCTYPE_HEXNUM, ['d'] = MM_JSON_SCTYPE_HEXNUM,
		['e'] = MM_JSON_SCTYPE_HEXNUM, ['f'] = MM_JSON_SCTYPE_HEXNUM,
		['A'] = MM_JSON_SCTYPE_HEXNUM, ['B'] = MM_JSON_SCTYPE_HEXNUM,
		['C'] = MM_JSON_SCTYPE_HEXNUM, ['D'] = MM_JSON_SCTYPE_HEXNUM,
		['E'] = MM_JSON_SCTYPE_HEXNUM, ['F'] = MM_JSON_SCTYPE_HEXNUM,
	},
};

static mm_json_token_t
mm_json_reader_scan_string(struct mm_json_reader *reader, mm_json_token_t token)
{
	const char *cp = reader->ptr;
	const char *ep = reader->end;
	ASSERT(cp[0] == '"');

	reader->string.escaped = false;
	reader->string.highbit = false;

	bool split = false;
	unsigned count = 0;
	mm_json_string_state_t state = MM_JSON_SSTATE_NORMAL;
	for (;;) {
		if (++cp == ep) {
			if (cp == reader->input_end)
				return MM_JSON_PARTIAL;

			cp = reader->input;
			ep = reader->input_end;
			if (unlikely(cp == ep))
				return MM_JSON_PARTIAL;

			split = true;
		}

		uint8_t c = *cp;
		switch (mm_json_string_table[state][c]) {
		case MM_JSON_SCTYPE_ERROR:
			return MM_JSON_INVALID;

		case MM_JSON_SCTYPE_REGULAR:
			break;

		case MM_JSON_SCTYPE_HIGHBIT:
			reader->string.highbit = true;
			break;

		case MM_JSON_SCTYPE_ESCAPE:
			reader->string.escaped = true;
			state = MM_JSON_SSTATE_ESCAPE;
			break;

		case MM_JSON_SCTYPE_ESC2CH:
			state = MM_JSON_SSTATE_NORMAL;
			break;

		case MM_JSON_SCTYPE_ESC6CH:
			state = MM_JSON_SSTATE_HEXNUM;
			break;

		case MM_JSON_SCTYPE_HEXNUM:
			if (count++ == 3) {
				count = 0;
				state = MM_JSON_SSTATE_NORMAL;
			}
			break;

		case MM_JSON_SCTYPE_QUOTE:
			reader->ptr++;
			if (unlikely(split)) {
				mm_json_reader_save_input(reader, cp);
				reader->input = cp;

				reader->value = reader->buffer;
				reader->value_end = reader->buffer_end;
			} else {
				reader->value = reader->ptr;
				reader->value_end = cp;
			}
			reader->ptr = cp + 1;
			return token;
		}
	}
}

/**********************************************************************
 * JSON reader number scan routine.
 **********************************************************************/

typedef enum
{
	MM_JSON_NSTATE_START,
	MM_JSON_NSTATE_FIRST,
	MM_JSON_NSTATE_NEXT,
	MM_JSON_NSTATE_ONLY,

	MM_JSON_NSTATE_FFIRST,
	MM_JSON_NSTATE_FNEXT,

	MM_JSON_NSTATE_ESTART,
	MM_JSON_NSTATE_EFIRST,
	MM_JSON_NSTATE_ENEXT,

} mm_json_number_state_t;

typedef enum
{
	MM_JSON_NCTYPE_ERROR = 0,

	MM_JSON_NCTYPE_MINUS,
	MM_JSON_NCTYPE_ZERO,
	MM_JSON_NCTYPE_FIRST,
	MM_JSON_NCTYPE_NEXT,

	MM_JSON_NCTYPE_POINT,
	MM_JSON_NCTYPE_FFIRST,

	MM_JSON_NCTYPE_E,
	MM_JSON_NCTYPE_ESIGN,
	MM_JSON_NCTYPE_EFIRST,

	MM_JSON_NCTYPE_DELIM,

} mm_json_number_ctype_t;

static uint8_t mm_json_number_table[][256] = {

	[MM_JSON_NSTATE_START] = {
		['-'] = MM_JSON_NCTYPE_MINUS,

		['0'] = MM_JSON_NCTYPE_ZERO, ['1'] = MM_JSON_NCTYPE_FIRST,
		['2'] = MM_JSON_NCTYPE_FIRST, ['3'] = MM_JSON_NCTYPE_FIRST,
		['4'] = MM_JSON_NCTYPE_FIRST, ['5'] = MM_JSON_NCTYPE_FIRST,
		['6'] = MM_JSON_NCTYPE_FIRST, ['7'] = MM_JSON_NCTYPE_FIRST,
		['8'] = MM_JSON_NCTYPE_FIRST, ['9'] = MM_JSON_NCTYPE_FIRST,
	},

	[MM_JSON_NSTATE_FIRST] = {
		['0'] = MM_JSON_NCTYPE_ZERO, ['1'] = MM_JSON_NCTYPE_FIRST,
		['2'] = MM_JSON_NCTYPE_FIRST, ['3'] = MM_JSON_NCTYPE_FIRST,
		['4'] = MM_JSON_NCTYPE_FIRST, ['5'] = MM_JSON_NCTYPE_FIRST,
		['6'] = MM_JSON_NCTYPE_FIRST, ['7'] = MM_JSON_NCTYPE_FIRST,
		['8'] = MM_JSON_NCTYPE_FIRST, ['9'] = MM_JSON_NCTYPE_FIRST,
	},

	[MM_JSON_NSTATE_NEXT] = {
		['0'] = MM_JSON_NCTYPE_NEXT, ['1'] = MM_JSON_NCTYPE_NEXT,
		['2'] = MM_JSON_NCTYPE_NEXT, ['3'] = MM_JSON_NCTYPE_NEXT,
		['4'] = MM_JSON_NCTYPE_NEXT, ['5'] = MM_JSON_NCTYPE_NEXT,
		['6'] = MM_JSON_NCTYPE_NEXT, ['7'] = MM_JSON_NCTYPE_NEXT,
		['8'] = MM_JSON_NCTYPE_NEXT, ['9'] = MM_JSON_NCTYPE_NEXT,

		['.'] = MM_JSON_NCTYPE_POINT,
		['e'] = MM_JSON_NCTYPE_E, ['E'] = MM_JSON_NCTYPE_E,

		[' '] = MM_JSON_NCTYPE_DELIM, ['\t'] = MM_JSON_NCTYPE_DELIM,
		['\r'] = MM_JSON_NCTYPE_DELIM, ['\n'] = MM_JSON_NCTYPE_DELIM,
		[']'] = MM_JSON_NCTYPE_DELIM, ['}'] = MM_JSON_NCTYPE_DELIM,
		[','] = MM_JSON_NCTYPE_DELIM,
	},

	[MM_JSON_NSTATE_ONLY] = {
		['.'] = MM_JSON_NCTYPE_POINT,
		['e'] = MM_JSON_NCTYPE_E, ['E'] = MM_JSON_NCTYPE_E,

		[' '] = MM_JSON_NCTYPE_DELIM, ['\t'] = MM_JSON_NCTYPE_DELIM,
		['\r'] = MM_JSON_NCTYPE_DELIM, ['\n'] = MM_JSON_NCTYPE_DELIM,
		[']'] = MM_JSON_NCTYPE_DELIM, ['}'] = MM_JSON_NCTYPE_DELIM,
		[','] = MM_JSON_NCTYPE_DELIM,
	},

	[MM_JSON_NSTATE_FFIRST] = {
		['0'] = MM_JSON_NCTYPE_FFIRST, ['1'] = MM_JSON_NCTYPE_FFIRST,
		['2'] = MM_JSON_NCTYPE_FFIRST, ['3'] = MM_JSON_NCTYPE_FFIRST,
		['4'] = MM_JSON_NCTYPE_FFIRST, ['5'] = MM_JSON_NCTYPE_FFIRST,
		['6'] = MM_JSON_NCTYPE_FFIRST, ['7'] = MM_JSON_NCTYPE_FFIRST,
		['8'] = MM_JSON_NCTYPE_FFIRST, ['9'] = MM_JSON_NCTYPE_FFIRST,
	},

	[MM_JSON_NSTATE_FNEXT] = {
		['0'] = MM_JSON_NCTYPE_NEXT, ['1'] = MM_JSON_NCTYPE_NEXT,
		['2'] = MM_JSON_NCTYPE_NEXT, ['3'] = MM_JSON_NCTYPE_NEXT,
		['4'] = MM_JSON_NCTYPE_NEXT, ['5'] = MM_JSON_NCTYPE_NEXT,
		['6'] = MM_JSON_NCTYPE_NEXT, ['7'] = MM_JSON_NCTYPE_NEXT,
		['8'] = MM_JSON_NCTYPE_NEXT, ['9'] = MM_JSON_NCTYPE_NEXT,

		['e'] = MM_JSON_NCTYPE_E, ['E'] = MM_JSON_NCTYPE_E,

		[' '] = MM_JSON_NCTYPE_DELIM, ['\t'] = MM_JSON_NCTYPE_DELIM,
		['\r'] = MM_JSON_NCTYPE_DELIM, ['\n'] = MM_JSON_NCTYPE_DELIM,
		[']'] = MM_JSON_NCTYPE_DELIM, ['}'] = MM_JSON_NCTYPE_DELIM,
		[','] = MM_JSON_NCTYPE_DELIM,
	},

	[MM_JSON_NSTATE_ESTART] = {
		['-'] = MM_JSON_NCTYPE_ESIGN, ['+'] = MM_JSON_NCTYPE_ESIGN,

		['0'] = MM_JSON_NCTYPE_EFIRST, ['1'] = MM_JSON_NCTYPE_EFIRST,
		['2'] = MM_JSON_NCTYPE_EFIRST, ['3'] = MM_JSON_NCTYPE_EFIRST,
		['4'] = MM_JSON_NCTYPE_EFIRST, ['5'] = MM_JSON_NCTYPE_EFIRST,
		['6'] = MM_JSON_NCTYPE_EFIRST, ['7'] = MM_JSON_NCTYPE_EFIRST,
		['8'] = MM_JSON_NCTYPE_EFIRST, ['9'] = MM_JSON_NCTYPE_EFIRST,
	},

	[MM_JSON_NSTATE_EFIRST] = {
		['0'] = MM_JSON_NCTYPE_EFIRST, ['1'] = MM_JSON_NCTYPE_EFIRST,
		['2'] = MM_JSON_NCTYPE_EFIRST, ['3'] = MM_JSON_NCTYPE_EFIRST,
		['4'] = MM_JSON_NCTYPE_EFIRST, ['5'] = MM_JSON_NCTYPE_EFIRST,
		['6'] = MM_JSON_NCTYPE_EFIRST, ['7'] = MM_JSON_NCTYPE_EFIRST,
		['8'] = MM_JSON_NCTYPE_EFIRST, ['9'] = MM_JSON_NCTYPE_EFIRST,
	},

	[MM_JSON_NSTATE_ENEXT] = {
		['0'] = MM_JSON_NCTYPE_NEXT, ['1'] = MM_JSON_NCTYPE_NEXT,
		['2'] = MM_JSON_NCTYPE_NEXT, ['3'] = MM_JSON_NCTYPE_NEXT,
		['4'] = MM_JSON_NCTYPE_NEXT, ['5'] = MM_JSON_NCTYPE_NEXT,
		['6'] = MM_JSON_NCTYPE_NEXT, ['7'] = MM_JSON_NCTYPE_NEXT,
		['8'] = MM_JSON_NCTYPE_NEXT, ['9'] = MM_JSON_NCTYPE_NEXT,

		[' '] = MM_JSON_NCTYPE_DELIM, ['\t'] = MM_JSON_NCTYPE_DELIM,
		['\r'] = MM_JSON_NCTYPE_DELIM, ['\n'] = MM_JSON_NCTYPE_DELIM,
		[']'] = MM_JSON_NCTYPE_DELIM, ['}'] = MM_JSON_NCTYPE_DELIM,
		[','] = MM_JSON_NCTYPE_DELIM,
	},
};

static mm_json_token_t
mm_json_reader_scan_number(struct mm_json_reader *reader)
{
	const char *cp = reader->ptr;
	const char *ep = reader->end;
	ASSERT(cp[0] == '-' || (cp[0] >= '0' && cp[0] <= '9'));

	reader->number.fraction = false;
	reader->number.exponent = false;

	bool split = false;
	mm_json_number_state_t state = MM_JSON_NSTATE_START;
	for (;;) {
		uint8_t c = *cp;
		switch (mm_json_number_table[state][c]) {
		case MM_JSON_NCTYPE_ERROR:
			return MM_JSON_INVALID;

		case MM_JSON_NCTYPE_MINUS:
			state = MM_JSON_NSTATE_FIRST;
			break;

		case MM_JSON_NCTYPE_ZERO:
			state = MM_JSON_NSTATE_ONLY;
			break;

		case MM_JSON_NCTYPE_FIRST:
			state = MM_JSON_NSTATE_NEXT;
			break;

		case MM_JSON_NCTYPE_POINT:
			state = MM_JSON_NSTATE_FFIRST;
			break;

		case MM_JSON_NCTYPE_FFIRST:
			state = MM_JSON_NSTATE_FNEXT;
			break;

		case MM_JSON_NCTYPE_E:
			state = MM_JSON_NSTATE_ESTART;
			break;

		case MM_JSON_NCTYPE_ESIGN:
			state = MM_JSON_NSTATE_EFIRST;
			break;

		case MM_JSON_NCTYPE_EFIRST:
			state = MM_JSON_NSTATE_ENEXT;
			break;

		case MM_JSON_NCTYPE_NEXT:
			break;

		case MM_JSON_NCTYPE_DELIM:
			if (unlikely(split)) {
				mm_json_reader_save_input(reader, cp);
				reader->input = cp;
				reader->value = reader->buffer;
				reader->value_end = reader->buffer_end;
			} else {
				reader->value = reader->ptr;
				reader->value_end = cp;
			}
			reader->ptr = cp;
			return MM_JSON_NUMBER;
		}

		if (++cp == ep) {
			if (cp == reader->input_end)
				return MM_JSON_PARTIAL;

			cp = reader->input;
			ep = reader->input_end;
			if (unlikely(cp == ep))
				return MM_JSON_PARTIAL;

			split = true;
		}
	}
}

/**********************************************************************
 * JSON reader main parser.
 **********************************************************************/

typedef enum
{
	MM_JSON_CTYPE_ERROR = 0,

	MM_JSON_CTYPE_SPACE,

	MM_JSON_CTYPE_OBJECT,
	MM_JSON_CTYPE_OBJECT_VALUE,
	MM_JSON_CTYPE_OBJECT_NEXT,
	MM_JSON_CTYPE_OBJECT_END,

	MM_JSON_CTYPE_ARRAY,
	MM_JSON_CTYPE_ARRAY_NEXT,
	MM_JSON_CTYPE_ARRAY_END,

	MM_JSON_CTYPE_FALSE,
	MM_JSON_CTYPE_TRUE,
	MM_JSON_CTYPE_NULL,

	MM_JSON_CTYPE_NAME,
	MM_JSON_CTYPE_STRING,
	MM_JSON_CTYPE_NUMBER,

} mm_json_ctype_t;

static uint8_t mm_json_text_table[][256] = {

	[MM_JSON_STATE_SPACE] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,
	},

	[MM_JSON_STATE_VALUE] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		['{'] = MM_JSON_CTYPE_OBJECT,
		['['] = MM_JSON_CTYPE_ARRAY,

		['f'] = MM_JSON_CTYPE_FALSE, ['t'] = MM_JSON_CTYPE_TRUE,
		['n'] = MM_JSON_CTYPE_NULL,

		['"'] = MM_JSON_CTYPE_STRING,

		['-'] = MM_JSON_CTYPE_NUMBER,
		['0'] = MM_JSON_CTYPE_NUMBER, ['1'] = MM_JSON_CTYPE_NUMBER,
		['2'] = MM_JSON_CTYPE_NUMBER, ['3'] = MM_JSON_CTYPE_NUMBER,
		['4'] = MM_JSON_CTYPE_NUMBER, ['5'] = MM_JSON_CTYPE_NUMBER,
		['6'] = MM_JSON_CTYPE_NUMBER, ['7'] = MM_JSON_CTYPE_NUMBER,
		['8'] = MM_JSON_CTYPE_NUMBER, ['9'] = MM_JSON_CTYPE_NUMBER,
	},

	[MM_JSON_STATE_OBJECT] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		['"'] = MM_JSON_CTYPE_NAME,

		['}'] = MM_JSON_CTYPE_OBJECT_END,
	},
	[MM_JSON_STATE_OBJECT_NAME] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		['"'] = MM_JSON_CTYPE_NAME,
	},
	[MM_JSON_STATE_OBJECT_NAME_SEP] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		[':'] = MM_JSON_CTYPE_OBJECT_VALUE,
	},
	[MM_JSON_STATE_OBJECT_VALUE_SEP] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		[','] = MM_JSON_CTYPE_OBJECT_NEXT,

		['}'] = MM_JSON_CTYPE_OBJECT_END,
	},

	[MM_JSON_STATE_ARRAY] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		['{'] = MM_JSON_CTYPE_OBJECT,
		['['] = MM_JSON_CTYPE_ARRAY,

		['f'] = MM_JSON_CTYPE_FALSE,
		['t'] = MM_JSON_CTYPE_TRUE,
		['n'] = MM_JSON_CTYPE_NULL,

		['"'] = MM_JSON_CTYPE_STRING,

		['-'] = MM_JSON_CTYPE_NUMBER,
		['0'] = MM_JSON_CTYPE_NUMBER, ['1'] = MM_JSON_CTYPE_NUMBER,
		['2'] = MM_JSON_CTYPE_NUMBER, ['3'] = MM_JSON_CTYPE_NUMBER,
		['4'] = MM_JSON_CTYPE_NUMBER, ['5'] = MM_JSON_CTYPE_NUMBER,
		['6'] = MM_JSON_CTYPE_NUMBER, ['7'] = MM_JSON_CTYPE_NUMBER,
		['8'] = MM_JSON_CTYPE_NUMBER, ['9'] = MM_JSON_CTYPE_NUMBER,

		[']'] = MM_JSON_CTYPE_ARRAY_END,
	},
	[MM_JSON_STATE_ARRAY_VALUE_SEP] = {
		[' '] = MM_JSON_CTYPE_SPACE, ['\t'] = MM_JSON_CTYPE_SPACE,
		['\r'] = MM_JSON_CTYPE_SPACE, ['\n'] = MM_JSON_CTYPE_SPACE,

		[','] = MM_JSON_CTYPE_ARRAY_NEXT,

		[']'] = MM_JSON_CTYPE_ARRAY_END,
	},
};

mm_json_token_t NONNULL(1)
mm_json_reader_next(struct mm_json_reader *reader)
{
	switch (reader->token) {
	case MM_JSON_INITIAL:
		return (reader->token = MM_JSON_START_DOCUMENT);

	case MM_JSON_PARTIAL:
		// Just take up where we left off.
		break;

	case MM_JSON_INVALID:
		// Once an error, always an error.
		return reader->token;

	case MM_JSON_START_DOCUMENT:
		reader->state = MM_JSON_STATE_VALUE;
		break;

	case MM_JSON_START_OBJECT:
		reader->state = MM_JSON_STATE_OBJECT;
		break;

	case MM_JSON_START_ARRAY:
		reader->state = MM_JSON_STATE_ARRAY;
		break;

	case MM_JSON_NAME:
		reader->state = MM_JSON_STATE_OBJECT_NAME_SEP;
		break;

	case MM_JSON_STRING:
	case MM_JSON_NUMBER:
	case MM_JSON_FALSE:
	case MM_JSON_TRUE:
	case MM_JSON_NULL:
	case MM_JSON_END_OBJECT:
	case MM_JSON_END_ARRAY:
		if (reader->stack_top == 0)
			return (reader->token = MM_JSON_END_DOCUMENT);
		if (mm_json_reader_stack_get(reader))
			reader->state = MM_JSON_STATE_OBJECT_VALUE_SEP;
		else
			reader->state = MM_JSON_STATE_ARRAY_VALUE_SEP;
		break;

	case MM_JSON_END_DOCUMENT:
		reader->state = MM_JSON_STATE_SPACE;
		break;

	default:
		ABORT();
	}

	for (;;) {
		if (reader->ptr == reader->end) {
			if (reader->ptr == reader->input_end)
				return (reader->token = MM_JSON_PARTIAL);

			reader->ptr = reader->input;
			reader->end = reader->input_end;
			if (unlikely(reader->ptr == reader->end))
				return (reader->token = MM_JSON_PARTIAL);
		}

		uint8_t c = *reader->ptr;
		switch (mm_json_text_table[reader->state][c]) {
		case MM_JSON_CTYPE_ERROR:
			return (reader->token = MM_JSON_INVALID);

		case MM_JSON_CTYPE_SPACE:
			reader->ptr++;
			break;

		case MM_JSON_CTYPE_ARRAY:
			reader->ptr++;
			mm_json_reader_stack_push(reader, false);
			return (reader->token = MM_JSON_START_ARRAY);

		case MM_JSON_CTYPE_ARRAY_NEXT:
			reader->ptr++;
			reader->state = MM_JSON_STATE_VALUE;
			break;

		case MM_JSON_CTYPE_ARRAY_END:
			reader->ptr++;
			mm_json_reader_stack_pop(reader);
			return (reader->token = MM_JSON_END_ARRAY);

		case MM_JSON_CTYPE_OBJECT:
			reader->ptr++;
			mm_json_reader_stack_push(reader, true);
			return (reader->token = MM_JSON_START_OBJECT);

		case MM_JSON_CTYPE_OBJECT_VALUE:
			reader->ptr++;
			reader->state = MM_JSON_STATE_VALUE;
			break;

		case MM_JSON_CTYPE_OBJECT_NEXT:
			reader->ptr++;
			reader->state = MM_JSON_STATE_OBJECT_NAME;
			break;

		case MM_JSON_CTYPE_OBJECT_END:
			reader->ptr++;
			mm_json_reader_stack_pop(reader);
			return (reader->token = MM_JSON_END_OBJECT);

		case MM_JSON_CTYPE_FALSE:
			return (reader->token = mm_json_reader_scan_false(reader));

		case MM_JSON_CTYPE_TRUE:
			return (reader->token = mm_json_reader_scan_true(reader));

		case MM_JSON_CTYPE_NULL:
			return (reader->token = mm_json_reader_scan_null(reader));

		case MM_JSON_CTYPE_NAME:
			return (reader->token = mm_json_reader_scan_string(reader,
									   MM_JSON_NAME));

		case MM_JSON_CTYPE_STRING:
			return (reader->token = mm_json_reader_scan_string(reader,
									   MM_JSON_STRING));

		case MM_JSON_CTYPE_NUMBER:
			return (reader->token = mm_json_reader_scan_number(reader));
		}
	}
}

mm_json_token_t NONNULL(1)
mm_json_reader_skip(struct mm_json_reader *reader)
{
	for (;;) {
		mm_json_token_t token = mm_json_reader_next(reader);
		switch (token) {
		case MM_JSON_PARTIAL:
		case MM_JSON_INVALID:
			return token;

		case MM_JSON_START_OBJECT:
		case MM_JSON_START_ARRAY:
			reader->skip_level++;
			break;

		case MM_JSON_END_OBJECT:
		case MM_JSON_END_ARRAY:
			if (reader->skip_level == 0 || --reader->skip_level == 0)
				return token;
			break;

		default:
			if (reader->skip_level == 0)
				return token;
			break;
		}
	}
}

/**********************************************************************
 * JSON reader value handling.
 **********************************************************************/

size_t NONNULL(1)
mm_json_reader_length(struct mm_json_reader *reader)
{
	return (reader->value_end - reader->value);
}

char * NONNULL(1)
mm_json_reader_memdup(struct mm_json_reader *reader)
{
	size_t length = mm_json_reader_string_length(reader);
	char *string = mm_arena_alloc(reader->arena, length);
	memcpy(string, reader->value, length);
	return string;
}

char * NONNULL(1)
mm_json_reader_strdup(struct mm_json_reader *reader)
{
	size_t length = mm_json_reader_string_length(reader);
	char *string = mm_arena_alloc(reader->arena, length + 1);
	memcpy(string, reader->value, length);
	string[length] = 0;
	return string;
}

size_t NONNULL(1)
mm_json_reader_string_length(struct mm_json_reader *reader)
{
	ASSERT(reader->token == MM_JSON_STRING);
	if (reader->string.escaped)
		return 0; // TODO

	return mm_json_reader_length(reader);
}

char * NONNULL(1)
mm_json_reader_string_memdup(struct mm_json_reader *reader)
{
	ASSERT(reader->token == MM_JSON_STRING);
	if (reader->string.escaped)
		return NULL; // TODO

	return mm_json_reader_memdup(reader);
}

char * NONNULL(1)
mm_json_reader_string_strdup(struct mm_json_reader *reader)
{
	ASSERT(reader->token == MM_JSON_STRING);
	if (reader->string.escaped)
		return NULL; // TODO

	return mm_json_reader_strdup(reader);
}

bool NONNULL(1, 2)
mm_json_reader_string_equals(struct mm_json_reader *reader, const char *string)
{
	ASSERT(reader->token == MM_JSON_STRING);
	if (reader->string.escaped)
		return false; // TODO

	size_t length = reader->value_end - reader->value;
	if (length != strlen(string))
		return false;

	return memcmp(reader->value, string, length) == 0;
}

int32_t NONNULL(1)
mm_json_reader_number_int32(struct mm_json_reader *reader)
{
	ASSERT(reader->token == MM_JSON_NUMBER);
	int error = 0;
	int32_t value = 0;
	mm_scan_d32(&value, &error, reader->buffer, reader->buffer_end);
	if (error)
		errno = error;
	return value;
}

int64_t NONNULL(1)
mm_json_reader_number_int64(struct mm_json_reader *reader)
{
	ASSERT(reader->token == MM_JSON_NUMBER);
	int error = 0;
	int64_t value = 0;
	mm_scan_d64(&value, &error, reader->buffer, reader->buffer_end);
	if (error)
		errno = error;
	return value;
}
