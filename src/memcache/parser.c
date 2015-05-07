/*
 * memcache/parser.c - MainMemory memcache parser.
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

#include "memcache/parser.h"
#include "memcache/state.h"

#include "base/mem/alloc.h"
#include "net/netbuf.h"


#define MC_KEY_LEN_MAX		250


static inline bool
mc_cursor_contains(struct mm_buffer_cursor *cur, const char *ptr)
{
	return ptr >= cur->ptr && ptr < cur->end;
}

static uint32_t
mc_parser_exptime(uint32_t exptime)
{
	if (exptime != 0 && exptime <= (60 * 60 * 24 * 30))
		exptime += mm_core->time_manager.real_time / 1000000;
	return exptime;
}

/*
 * Prepare for parsing a command.
 */
void
mc_parser_start(struct mc_parser *parser, struct mc_state *state)
{
	ENTER();
	DEBUG("Start parser.");

	mm_netbuf_read_first(&state->sock, &parser->cursor);
	if (state->start_ptr != NULL) {
		while (!mc_cursor_contains(&parser->cursor, state->start_ptr)) {
			mm_netbuf_read_next(&state->sock, &parser->cursor);
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
mc_parser_scan_lf(struct mc_parser *parser, char *s)
{
	ASSERT(mc_cursor_contains(&parser->cursor, s));

	if ((s + 1) < parser->cursor.end)
		return *(s + 1) == '\n';

	struct mm_buffer *buf = &parser->state->sock.rbuf;
	struct mm_buffer_segment *seg = parser->cursor.seg;
	if (seg != buf->tail_seg) {
		seg = seg->next;
		if (seg != buf->tail_seg || buf->tail_off)
			return seg->data[0] == '\n';
	}

	return false;
}

static bool
mc_parser_scan_value(struct mc_parser *parser)
{
	ENTER();

	bool rc = true;

	// Store the start position.
  	parser->command->params.set.seg = parser->cursor.seg;
	parser->command->params.set.start = parser->cursor.ptr;

	// Move over the required number of bytes.
	uint32_t bytes = parser->command->params.set.bytes;
	for (;;) {
		uint32_t avail = parser->cursor.end - parser->cursor.ptr;
		DEBUG("parse data: avail = %ld, bytes = %ld", (long) avail, (long) bytes);
		if (avail > bytes) {
			parser->cursor.ptr += bytes;
			break;
		}

		parser->cursor.ptr += avail;
		bytes -= avail;

		if (!mm_netbuf_read_next(&parser->state->sock, &parser->cursor)) {
			// Try to read the value and required LF and optional CR.
			mm_netbuf_demand(&parser->state->sock, bytes + 2);
			ssize_t n = mm_netbuf_fill(&parser->state->sock);
			if (n <= 0) {
				if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
					parser->state->error = true;
				rc = false;
				break;
			}

			mm_netbuf_read_more(&parser->state->sock, &parser->cursor);
		}
	}

	LEAVE();
	return rc;
}

// TODO: Really support some options.
static void
mc_parser_handle_option(struct mc_command *command)
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

bool
mc_parser_parse(struct mc_parser *parser)
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
		S_TOUCH_3,
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

	mm_core_t core = mm_netbuf_core(&parser->state->sock);

	// The current command.
	struct mc_command *command = mc_command_create(core);
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
				} else if (unlikely(c == '\n')) {
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
					shift = S_TOUCH_1;
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
					command->params.sock = &parser->state->sock.sock;
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
				if ((unlikely(c == '\r') && mc_parser_scan_lf(parser, s)) || unlikely(c == '\n')) {
					DEBUG("Missing key.");
					state = S_ERROR;
					goto again;
				} else {
					state = S_KEY_N;
					command->action.key = s;
					break;
				}

			case S_KEY_N:
				if (c == ' ') {
					size_t len = s - command->action.key;
					if (unlikely(len > MC_KEY_LEN_MAX)) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						state = S_SPACE;
						command->action.key_len = len;
					}
					break;
				} else if ((c == '\r' && mc_parser_scan_lf(parser, s)) || c == '\n') {
					size_t len = s - command->action.key;
					if (len > MC_KEY_LEN_MAX) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						state = shift;
						command->action.key_len = len;
					}
					goto again;
				} else {
					// Move over to the next char.
					break;
				}

			case S_KEY_EDGE:
				if (c == ' ') {
					state = S_SPACE;
					command->action.key_len = MC_KEY_LEN_MAX;
					break;
				} else if ((c == '\r' && mc_parser_scan_lf(parser, s)) || c == '\n') {
					state = shift;
					command->action.key_len = MC_KEY_LEN_MAX;
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
				} else if ((c == '\r' && mc_parser_scan_lf(parser, s)) || c == '\n') {
					state = shift;
					goto again;
				} else {
					struct mc_action *action = &command->action;
					if (action->key_len == MC_KEY_LEN_MAX) {
						DEBUG("Too long key.");
						state = S_ERROR;
					} else {
						char *str = (char *) action->key;
						str[action->key_len++] = c;
					}
					break;
				}

			case S_NUM32:
				ASSERT(c != ' ');
				if (likely(c >= '0') && likely(c <= '9')) {
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
				if (likely(c >= '0') && likely(c <= '9')) {
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
				mc_action_hash(&command->action);
				if (c == '\r' || c == '\n') {
					state = S_EOL;
					command->params.last = true;
					goto again;
				} else {
					state = S_KEY;
					command->end_ptr = s;
					command->next = mc_command_create(core);
					command->next->type = command->type;
					command = command->next;
					goto again;
				}

			case S_SET_1:
				state = S_KEY;
				shift = S_SET_2;
				goto again;

			case S_SET_2:
				mc_action_hash(&command->action);
				mc_action_create(&command->action);
				state = S_NUM32;
				shift = S_SET_3;
				goto again;

			case S_SET_3:
				command->params.set.flags = num32;
				state = S_NUM32;
				shift = S_SET_4;
				goto again;

			case S_SET_4:
				command->params.set.exptime = mc_parser_exptime(num32);
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
				command->action.stamp = num64;
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
				mc_action_hash(&command->action);
				state = S_NUM64;
				shift = S_VAL64;
				goto again;

			case S_DELETE_1:
				state = S_KEY;
				shift = S_DELETE_2;
				goto again;

			case S_DELETE_2:
				ASSERT(c != ' ');
				mc_action_hash(&command->action);
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
				mc_action_hash(&command->action);
				state = S_NUM32;
				shift = S_TOUCH_3;
				goto again;

			case S_TOUCH_3:
				command->params.val32 = mc_parser_exptime(num32);
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
					mc_parser_handle_option(command);
					state = S_SPACE;
					break;
				} else if (c == '\r' || c == '\n') {
					mc_parser_handle_option(command);
					state = S_EOL;
					goto again;
				} else {
					// TODO: add c to the option value
					break;
				}

			case S_VALUE:
				ASSERT(c != ' ');
				if (likely(c == '\r')) {
					state = S_VALUE_1;
					break;
				}
				// FALLTHRU
			case S_VALUE_1:
				if (likely(c == '\n')) {
					state = S_VALUE_2;
					break;
				} else {
					state = S_ERROR;
					break;
				}

			case S_VALUE_2:
				parser->cursor.ptr = s;
				rc = mc_parser_scan_value(parser);
				if (unlikely(!rc))
					goto leave;
				s = parser->cursor.ptr;
				e = parser->cursor.end;
				state = S_EOL;
				ASSERT(s < e);
				c = *s;
				goto again;

			case S_EOL:
				ASSERT(c != ' ');
				if (likely(c == '\r')) {
					state = S_EOL_1;
					break;
				}
				// FALLTHRU
			case S_EOL_1:
				if (likely(c == '\n')) {
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
						mc_command_destroy(core, tmp);
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
					command->result = MC_RESULT_ERROR;
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
			    && command->type->kind == MC_COMMAND_LOOKUP
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

			size_t len = e - command->action.key;
			if (len > MC_KEY_LEN_MAX) {
				DEBUG("Too long key.");
				state = S_ERROR;
			} else if (len == MC_KEY_LEN_MAX) {
				state = S_KEY_EDGE;
			} else {
				state = S_KEY_COPY;

				char *str = mm_local_alloc(MC_KEY_LEN_MAX);
				memcpy(str, command->action.key, len);
				command->action.key_len = len;
				command->action.key = str;
				command->own_key = true;
			}
		}

		rc = mm_netbuf_read_next(&parser->state->sock, &parser->cursor);

	} while (rc);

leave:
	LEAVE();
	return rc;
}

