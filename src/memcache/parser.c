/*
 * memcache/parser.c - MainMemory memcache parser.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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
#include "memcache/binary.h"
#include "memcache/state.h"

#include "base/memory/memory.h"

#include "net/netbuf.h"


#define MC_KEY_LEN_MAX		250


static bool
mc_parser_scan_lf(struct mc_state *state, const char *s, const char *e)
{
	bool rc = false;
	if ((s + 1) < e) {
		rc = *(s + 1) == '\n';
	} else {
		struct mm_buffer *buf = &state->sock.rxbuf;
		struct mm_buffer_reader reader = buf->head;
		if (mm_buffer_reader_next(&reader, buf))
			rc = *reader.ptr == '\n';
	}
	DEBUG("nl=%d", rc);
	return rc;
}

static bool
mc_parser_scan_value(struct mc_state *state)
{
	ENTER();
	bool rc = true;

	struct mc_command *command = state->command;
	struct mc_action *action = &command->action;

	// Try to read the value and required LF and optional CR.
	uint32_t required = action->value_len + 1;
	uint32_t available = mm_netbuf_size(&state->sock);
	while (required > available) {
		ssize_t n = mm_netbuf_fill(&state->sock, required - available + 1);
		if (n <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != ETIMEDOUT))
				state->error = true;
			rc = false;
			goto leave;
		}
		available += n;
	}

	// Read the entry value.
	if (action->alter_type == MC_ACTION_ALTER_OTHER) {
		struct mc_entry *entry = action->new_entry;
		char *value = mc_entry_getvalue(entry);
		mm_netbuf_read(&state->sock, value, action->value_len);
	} else {
		char *end = mm_netbuf_rend(&state->sock);
		if (unlikely(mm_netbuf_rget(&state->sock) == end)) {
			mm_netbuf_rnext(&state->sock);
			end = mm_netbuf_rend(&state->sock);
		}

		struct mm_buffer_reader *iter = &state->sock.rxbuf.head;
		if (iter->ptr + action->value_len <= end) {
			action->alter_value = iter->ptr;
			iter->ptr += action->value_len;
		} else {
			char *value = mm_private_alloc(action->value_len);
			mm_netbuf_read(&state->sock, value, action->value_len);
			action->alter_value = value;
			command->own_alter_value = true;
		}
	}

leave:
	LEAVE();
	return rc;
}

// TODO: Really support some options.
static void
mc_parser_handle_option(struct mc_command *command)
{
	ENTER();

	if (command->type == &mc_command_ascii_stats) {
		command->nopts++;
	} else if (command->type == &mc_command_ascii_slabs) {
		command->nopts++;
	}

	LEAVE();
}

bool NONNULL(1)
mc_parser_parse(struct mc_state *parser)
{
	ENTER();

	enum parser_state {
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
		S_DELTA_1,
		S_DELTA_2,
		S_DELTA_3,
		S_DELETE_1,
		S_DELETE_2,
		S_TOUCH_1,
		S_TOUCH_2,
		S_TOUCH_3,
		S_FLUSH_ALL_1,
		S_FLUSH_ALL_2,
		S_VERBOSITY_1,
		S_VERBOSITY_2,
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
	};

	// Initialize the result.
	bool rc = true;

	// Have enough contiguous space to identify any command.
	if (!mm_netbuf_span(&parser->sock, 1024))
		ABORT();

	// Get the input buffer position.
	char *s = mm_netbuf_rget(&parser->sock);
	char *e = mm_netbuf_rend(&parser->sock);
	DEBUG("%d %.*s", (int) (e - s), (int) (e - s), s);

	// Skip any leading whitespace.
	while (s < e && *s == ' ')
		s++;

	// Check if the input is sane.
	if ((e - s) < 4) {
noinput:
		if ((e - mm_netbuf_rget(&parser->sock)) >= 1024)
			parser->trash = true;
		rc = false;
		goto leave;
	}

	// Allocate a command.
	struct mc_command *command = mc_command_create(parser);
	parser->command = command;

	// Initialize the scanner state.
	enum parser_state state = S_ERROR;
	enum parser_state shift = S_ERROR;
	uint32_t num32 = 0;
	uint64_t num64 = 0;
	char *match = "";

	// Initialize storage command parameters.
	uint32_t set_flags = 0;
	uint32_t set_exp_time = 0;

	// The count of scanned chars. Used to check if the client sends
	// too much junk data.
	int count = 0;

#define Cx4(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

	// Identify the command by its first 4 chars.
	uint32_t start = Cx4(s[0], s[1], s[2], s[3]);
	s += 4;
	if (start == Cx4('g', 'e', 't', ' ')) {
		command->type = &mc_command_ascii_get;
		state = S_SPACE;
		shift = S_GET_1;
	} else if (start == Cx4('s', 'e', 't', ' ')) {
		command->type = &mc_command_ascii_set;
		state = S_SPACE;
		shift = S_SET_1;
	} else if (start == Cx4('r', 'e', 'p', 'l')) {
		command->type = &mc_command_ascii_replace;
		state = S_MATCH;
		match = "ace";
		shift = S_SET_1;
	} else if (start == Cx4('d', 'e', 'l', 'e')) {
		command->type = &mc_command_ascii_delete;
		state = S_MATCH;
		match = "te";
		shift = S_DELETE_1;
	} else if (start == Cx4('a', 'd', 'd', ' ')) {
		command->type = &mc_command_ascii_add;
		state = S_SPACE;
		shift = S_SET_1;
	} else if (start == Cx4('i', 'n', 'c', 'r')) {
		command->type = &mc_command_ascii_incr;
		state = S_MATCH;
		//match = "";
		shift = S_DELTA_1;
	} else if (start == Cx4('d', 'e', 'c', 'r')) {
		command->type = &mc_command_ascii_decr;
		state = S_MATCH;
		//match = "";
		shift = S_DELTA_1;
	} else if (start == Cx4('g', 'e', 't', 's')) {
		command->type = &mc_command_ascii_gets;
		state = S_MATCH;
		//match = "";
		shift = S_GET_1;
	} else if (start == Cx4('c', 'a', 's', ' ')) {
		command->type = &mc_command_ascii_cas;
		state = S_SPACE;
		shift = S_SET_1;
	} else if (start == Cx4('a', 'p', 'p', 'e')) {
		command->type = &mc_command_ascii_append;
		command->action.alter_type = MC_ACTION_ALTER_APPEND;
		state = S_MATCH;
		match = "nd";
		shift = S_SET_1;
	} else if (start == Cx4('p', 'r', 'e', 'p')) {
		command->type = &mc_command_ascii_prepend;
		command->action.alter_type = MC_ACTION_ALTER_PREPEND;
		state = S_MATCH;
		match = "end";
		shift = S_SET_1;
	} else if (start == Cx4('t', 'o', 'u', 'c')) {
		command->type = &mc_command_ascii_touch;
		state = S_MATCH;
		match = "h";
		shift = S_TOUCH_1;
	} else if (start == Cx4('s', 'l', 'a', 'b')) {
		command->type = &mc_command_ascii_slabs;
		state = S_MATCH;
		match = "s";
		shift = S_OPT;
	} else if (start == Cx4('s', 't', 'a', 't')) {
		command->type = &mc_command_ascii_stats;
		state = S_MATCH;
		match = "s";
		shift = S_OPT;
	} else if (start == Cx4('f', 'l', 'u', 's')) {
		command->type = &mc_command_ascii_flush_all;
		state = S_MATCH;
		match = "h_all";
		shift = S_FLUSH_ALL_1;
	} else if (start == Cx4('v', 'e', 'r', 's')) {
		command->type = &mc_command_ascii_version;
		state = S_MATCH;
		match = "ion";
		shift = S_EOL;
	} else if (start == Cx4('v', 'e', 'r', 'b')) {
		command->type = &mc_command_ascii_verbosity;
		state = S_MATCH;
		match = "osity";
		shift = S_VERBOSITY_1;
	} else if (start == Cx4('q', 'u', 'i', 't')) {
		command->type = &mc_command_ascii_quit;
		state = S_SPACE;
		shift = S_EOL;
	} else {
		DEBUG("unrecognized command");
		// Need to check the first 4 chars one by one again for a
		// possible '\n' among them.
		s -= 4;
	}

#undef Cx4

	// Parse the rest of the command.
	for (;; s++) {
		while (unlikely(s == e)) {
			if (command->type->kind != MC_COMMAND_LOOKUP)
				goto noinput;

			count += e - mm_netbuf_rget(&parser->sock);
			if (count > (16 * 1024)) {
				parser->trash = true;
				rc = false;
				goto leave;
			}

			if (state == S_KEY_N) {
				DEBUG("split key");
				size_t len = e - command->action.key;
				if (len > MC_KEY_LEN_MAX) {
					DEBUG("too long key");
					state = S_ERROR;
				} else if (len == MC_KEY_LEN_MAX) {
					state = S_KEY_EDGE;
				} else {
					state = S_KEY_COPY;

					char *str = mm_buffer_embed(&parser->sock.txbuf, MC_KEY_LEN_MAX);
					memcpy(str, command->action.key, len);
					command->action.key_len = len;
					command->action.key = str;
				}
			}

			if (!(rc = mm_netbuf_rnext(&parser->sock)))
				goto leave;

			s = mm_netbuf_rget(&parser->sock);
			e = mm_netbuf_rend(&parser->sock);
		}

		int c = *s;
again:
		switch (state) {
		case S_MATCH:
			if (c == *match) {
				// So far so good.
				if (unlikely(c == 0)) {
					// Hmm, a zero byte in the input.
					state = S_ERROR;
					break;
				}
				match++;
				break;
			} else if (unlikely(*match)) {
				DEBUG("unexpected char before the end");
				state = S_ERROR;
				goto again;
			} else if (c == ' ') {
				DEBUG("match");
				state = S_SPACE;
				break;
			} else if (c == '\r' || c == '\n') {
				DEBUG("match");
				state = shift;
				goto again;
			} else {
				DEBUG("unexpected char after the end");
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
			if ((unlikely(c == '\r') && mc_parser_scan_lf(parser, s, e)) || unlikely(c == '\n')) {
				DEBUG("missing key");
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
					DEBUG("too long key");
					state = S_ERROR;
				} else {
					state = S_SPACE;
					command->action.key_len = len;
				}
				break;
			} else if ((c == '\r' && mc_parser_scan_lf(parser, s, e)) || c == '\n') {
				size_t len = s - command->action.key;
				if (len > MC_KEY_LEN_MAX) {
					DEBUG("too long key");
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
			} else if ((c == '\r' && mc_parser_scan_lf(parser, s, e)) || c == '\n') {
				state = shift;
				command->action.key_len = MC_KEY_LEN_MAX;
				goto again;
			} else {
				DEBUG("too long key");
				state = S_ERROR;
				break;
			}

		case S_KEY_COPY:
			if (c == ' ') {
				state = S_SPACE;
				break;
			} else if ((c == '\r' && mc_parser_scan_lf(parser, s, e)) || c == '\n') {
				state = shift;
				goto again;
			} else {
				struct mc_action *action = &command->action;
				if (action->key_len == MC_KEY_LEN_MAX) {
					DEBUG("too long key");
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
				command->ascii.last = true;
				goto again;
			} else {
				state = S_KEY;
				command->next = mc_command_create(parser);
				command->next->type = command->type;
				command = command->next;
				goto again;
			}

		case S_SET_1:
			state = S_KEY;
			shift = S_SET_2;
			goto again;

		case S_SET_2:
			state = S_NUM32;
			shift = S_SET_3;
			goto again;

		case S_SET_3:
			set_flags = num32;
			state = S_NUM32;
			shift = S_SET_4;
			goto again;

		case S_SET_4:
			set_exp_time = mc_entry_fix_exptime(num32);
			state = S_NUM32;
			shift = S_SET_5;
			goto again;

		case S_SET_5:
			mc_action_hash(&command->action);
			if (command->action.alter_type == MC_ACTION_ALTER_OTHER) {
				mc_action_create(&command->action, num32);
				command->action.new_entry->flags = set_flags;
				command->action.new_entry->exp_time = set_exp_time;
				mc_entry_setkey(command->action.new_entry,
						command->action.key);
			} else {
				command->action.value_len = num32;
			}
			if (command->type == &mc_command_ascii_cas) {
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
			command->ascii.noreply = true;
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

		case S_DELTA_1:
			state = S_KEY;
			shift = S_DELTA_2;
			goto again;

		case S_DELTA_2:
			mc_action_hash(&command->action);
			state = S_NUM64;
			shift = S_DELTA_3;
			goto again;

		case S_DELTA_3:
			command->delta = num64;
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
			command->exp_time = mc_entry_fix_exptime(num32);
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
				shift = S_FLUSH_ALL_2;
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

		case S_FLUSH_ALL_2:
			command->exp_time = num32;
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

		case S_VERBOSITY_1:
			ASSERT(c != ' ');
			if (c >= '0' && c <= '9') {
				state = S_NUM32;
				shift = S_VERBOSITY_2;
				goto again;
			} else {
				state = S_ERROR;
				goto again;
			}

		case S_VERBOSITY_2:
			command->value = num32;
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
			command->ascii.noreply = true;
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
			/* no break */
		case S_VALUE_1:
			if (likely(c == '\n')) {
				state = S_VALUE_2;
				break;
			} else {
				state = S_ERROR;
				break;
			}

		case S_VALUE_2:
			mm_netbuf_rset(&parser->sock, s);
			rc = mc_parser_scan_value(parser);
			if (unlikely(!rc))
				goto leave;
			s = mm_netbuf_rget(&parser->sock);
			e = mm_netbuf_rend(&parser->sock);
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
			/* no break */
		case S_EOL_1:
			if (likely(c == '\n')) {
				mm_netbuf_rset(&parser->sock, s + 1);
				goto leave;
			} else {
				DEBUG("no eol");
				state = S_ERROR;
				break;
			}

		case S_ERROR:
			// If it was a SET command then it is required
			// to free a newly created entry.
			if (command->action.new_entry != NULL) {
				mc_action_cancel(&command->action);
				command->action.new_entry = NULL;
			}
			// If it was a GET command then it is required
			// to destroy all commands past the first one.
			// The first one is for the error response.
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
			command->type = &mc_command_ascii_error;
			state = S_ERROR_1;
			/* no break */
		case S_ERROR_1:
			if (c == '\n') {
				mm_netbuf_rset(&parser->sock, s + 1);
				goto leave;
			} else {
				// Skip char.
				break;
			}
		}
	}

leave:
	LEAVE();
	return rc;
}
