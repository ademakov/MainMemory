/*
 * memcache/parser.c - MainMemory memcache parser.
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

#include "memcache/parser.h"
#include "memcache/binary.h"
#include "memcache/state.h"

#include "base/memory/memory.h"
#include "base/net/netbuf.h"


#define MC_KEY_LEN_MAX		250

// Common states.
enum
{
	S_EOL,
	S_EOL_1,
	S_MATCH,
	S_SPACE,
	S_KEY,
	S_KEY_N,
	S_NUM32,
	S_NUM32_N,
	S_NOREPLY,
	S_ERROR,
	S_ERROR_1,
	S_OTHER_BASE // This must be the last one here.
};

// Lookup command states.
enum {
	S_KEY_EDGE = S_OTHER_BASE,
	S_KEY_COPY,
	S_GET_1,
	S_GET_N,
};

// Storage command states.
enum {
	S_NUM64 = S_OTHER_BASE,
	S_NUM64_N,
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
	S_VALUE,
	S_VALUE_1,
	S_VALUE_2,
};

// Other states.
enum {
	S_DELETE_1 = S_OTHER_BASE,
	S_DELETE_2,
	S_TOUCH_1,
	S_TOUCH_2,
	S_TOUCH_3,
	S_FLUSH_ALL_1,
	S_FLUSH_ALL_2,
	S_VERBOSITY_1,
	S_VERBOSITY_2,
	S_OPT,
	S_OPT_N,
};

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
mc_parser_scan_value(struct mc_state *state, uint32_t kind)
{
	ENTER();
	bool rc = true;

	struct mc_command_storage *command = (struct mc_command_storage *) state->command_last;
	struct mc_action_storage *action = &command->action;

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
	if (kind != MC_COMMAND_CONCAT) {
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
			action->own_alter_value = true;
		}
	}

leave:
	LEAVE();
	return rc;
}

// TODO: Really support some options.
static void
mc_parser_handle_option(struct mc_command_simple *command)
{
	ENTER();

	if (command->base.type == &mc_command_ascii_stats) {
		command->action.ascii_stats++;
	} else if (command->base.type == &mc_command_ascii_slabs) {
		// do nothing
	}

	LEAVE();
}

static bool
mc_parser_lookup_command(struct mc_state *parser, const struct mc_command_type *type,
			 char *s, char *e, int state, int shift)
{
	// The count of scanned chars. Used to check if the client sends too much junk data.
	int count = 0;

	// Parse the rest of the command.
	struct mc_command_simple *command = mc_command_create_simple(parser, type);
	struct mc_command_simple *command_first = command;
	command->action.ascii_get_last = false;

	for (;; s++) {
		while (unlikely(s == e)) {
			count += e - mm_netbuf_rget(&parser->sock);
			if (count > (16 * 1024)) {
				parser->trash = true;
				return false;
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

			if (!mm_netbuf_rnext(&parser->sock))
				return false;

			s = mm_netbuf_rget(&parser->sock);
			e = mm_netbuf_rend(&parser->sock);
		}

		int c = *s;
again:
		switch (state) {
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

		case S_GET_1:
			state = S_KEY;
			shift = S_GET_N;
			goto again;

		case S_GET_N:
			ASSERT(c != ' ');
			mc_action_hash(&command->action);
			if (c == '\r' || c == '\n') {
				state = S_EOL;
				command->action.ascii_get_last = true;
				goto again;
			} else {
				state = S_KEY;
				command = mc_command_create_simple(parser, type);
				command->action.ascii_get_last = false;
				goto again;
			}

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
				return true;
			} else {
				DEBUG("no eol");
				state = S_ERROR;
				break;
			}

		case S_ERROR:
			command = (struct mc_command_simple *) command_first->base.next;
			while (command != NULL) {
				struct mc_command_simple *next = (struct mc_command_simple *) command->base.next;
				mc_command_cleanup(&command->base);
				command = next;
			}

			command_first->base.type = &mc_command_ascii_error;
			command_first->base.next = NULL;
			state = S_ERROR_1;
			/* no break */
		case S_ERROR_1:
			if (c == '\n') {
				mm_netbuf_rset(&parser->sock, s + 1);
				return true;
			} else {
				// Skip char.
				break;
			}
		}
	}
}

static bool
mc_parser_storage_command(struct mc_state *parser, const struct mc_command_type *type,
			  char *s, char *e, int state, int shift, char *match)
{
	// Initialize storage command parameters.
	uint32_t set_flags = 0;
	uint32_t set_exp_time = 0;
	// Temporary storage for numeric parameters.
	uint32_t num32 = 0;
	uint64_t num64 = 0;

	struct mc_command_storage *command = mc_command_create_ascii_storage(parser, type);
	command->action.base.ascii_noreply = false;
	command->action.own_alter_value = false;
	command->action.new_entry = NULL;

	for (;; s++) {
		if (unlikely(s == e)) {
			if ((e - mm_netbuf_rget(&parser->sock)) >= 1024)
				parser->trash = true;
			return false;
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
				command->action.base.key = s;
				break;
			}

		case S_KEY_N:
			if (c == ' ') {
				size_t len = s - command->action.base.key;
				if (unlikely(len > MC_KEY_LEN_MAX)) {
					DEBUG("too long key");
					state = S_ERROR;
				} else {
					state = S_SPACE;
					command->action.base.key_len = len;
				}
				break;
			} else if ((c == '\r' && mc_parser_scan_lf(parser, s, e)) || c == '\n') {
				size_t len = s - command->action.base.key;
				if (len > MC_KEY_LEN_MAX) {
					DEBUG("too long key");
					state = S_ERROR;
				} else {
					state = shift;
					command->action.base.key_len = len;
				}
				goto again;
			} else {
				// Move over to the next char.
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
			mc_action_hash(&command->action.base);
			if (type->kind != MC_COMMAND_CONCAT) {
				mc_action_create(&command->action, num32);
				command->action.new_entry->flags = set_flags;
				command->action.new_entry->exp_time = set_exp_time;
				mc_entry_setkey(command->action.new_entry, command->action.base.key);
			} else {
				command->action.value_len = num32;
			}
			if (command->base.type == &mc_command_ascii_cas) {
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
			command->action.base.ascii_noreply = true;
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
			mc_action_hash(&command->action.base);
			state = S_NUM64;
			shift = S_DELTA_3;
			goto again;

		case S_DELTA_3:
			command->binary_delta = num64;
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
			command->action.base.ascii_noreply = true;
			state = S_EOL;
			goto again;

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
			if (unlikely(!mc_parser_scan_value(parser, type->kind)))
				return false;
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
				return true;
			} else {
				DEBUG("no eol");
				state = S_ERROR;
				break;
			}

		case S_ERROR:
			if (command->action.new_entry != NULL) {
				mc_action_cancel(&command->action);
				command->action.new_entry = NULL;
			}
			if (command->action.own_alter_value)
				mm_private_free((char *) command->action.alter_value);
			mc_command_cleanup(&command->base);

			command->base.type = &mc_command_ascii_error;
			state = S_ERROR_1;
			/* no break */
		case S_ERROR_1:
			if (c == '\n') {
				mm_netbuf_rset(&parser->sock, s + 1);
				return true;
			} else {
				// Skip char.
				break;
			}
		}
	}
}

static bool
mc_parser_other_command(struct mc_state *parser, const struct mc_command_type *type,
			char *s, char *e, int state, int shift, char *match)
{
	// Temporary storage for numeric parameters.
	uint32_t num32 = 0;

	// Parse the rest of the command.
	struct mc_command_simple *command = mc_command_create_simple(parser, type);
	command->action.ascii_noreply = false;
	command->action.ascii_stats = 0;

	for (;; s++) {
		if (unlikely(s == e)) {
			if ((e - mm_netbuf_rget(&parser->sock)) >= 1024)
				parser->trash = true;
			return false;
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
			command->action.ascii_exp_time = mc_entry_fix_exptime(num32);
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
			command->action.ascii_exp_time = num32;
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
			command->action.ascii_level = num32;
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
			command->action.ascii_noreply = true;
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
				return true;
			} else {
				DEBUG("no eol");
				state = S_ERROR;
				break;
			}

		case S_ERROR:
			mc_command_cleanup(&command->base);

			command->base.type = &mc_command_ascii_error;
			state = S_ERROR_1;
			/* no break */
		case S_ERROR_1:
			if (c == '\n') {
				mm_netbuf_rset(&parser->sock, s + 1);
				return true;
			} else {
				// Skip char.
				break;
			}
		}
	}
}

bool NONNULL(1)
mc_parser_parse(struct mc_state *parser)
{
	ENTER();

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
	if ((e - s) < 5) {
		if ((e - mm_netbuf_rget(&parser->sock)) >= 1024) {
			parser->trash = true;
			rc = false;
		} else if ((rc = memchr(s, '\n', e - s) != NULL)) {
			mc_parser_other_command(parser, &mc_command_ascii_error, s, e, S_ERROR, S_ERROR, "");
		}
		goto leave;
	}

#define Cx4(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

	// Identify the command by its first 4 chars.
	uint32_t start = Cx4(s[0], s[1], s[2], s[3]);
	if (start == Cx4('g', 'e', 't', ' ')) {
		rc = mc_parser_lookup_command(parser, &mc_command_ascii_get, s + 4, e, S_SPACE, S_GET_1);
	} else if (start == Cx4('s', 'e', 't', ' ')) {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_set, s + 4, e, S_SPACE, S_SET_1, "");
	} else if (start == Cx4('r', 'e', 'p', 'l') && s[4] == 'a') {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_replace, s + 5, e, S_MATCH, S_SET_1, "ce");
	} else if (start == Cx4('d', 'e', 'l', 'e') && s[4] == 't') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_delete, s + 5, e, S_MATCH, S_DELETE_1, "e");
	} else if (start == Cx4('a', 'd', 'd', ' ')) {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_add, s + 4, e, S_SPACE, S_SET_1, "");
	} else if (start == Cx4('i', 'n', 'c', 'r') && s[4] == ' ') {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_incr, s + 5, e, S_SPACE, S_DELTA_1, "");
	} else if (start == Cx4('d', 'e', 'c', 'r') && s[4] == ' ') {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_decr, s + 5, e, S_SPACE, S_DELTA_1, "");
	} else if (start == Cx4('g', 'e', 't', 's') && s[4] == ' ') {
		rc = mc_parser_lookup_command(parser, &mc_command_ascii_gets, s + 5, e, S_SPACE, S_GET_1);
	} else if (start == Cx4('c', 'a', 's', ' ')) {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_cas, s + 4, e, S_SPACE, S_SET_1, "");
	} else if (start == Cx4('a', 'p', 'p', 'e') && s[4] == 'n') {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_append, s + 5, e, S_MATCH, S_SET_1, "d");
	} else if (start == Cx4('p', 'r', 'e', 'p') && s[4] == 'e') {
		rc = mc_parser_storage_command(parser, &mc_command_ascii_prepend, s + 5, e, S_MATCH, S_SET_1, "nd");
	} else if (start == Cx4('t', 'o', 'u', 'c') && s[4] == 'h') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_touch, s + 5, e, S_MATCH, S_TOUCH_1, "");
	} else if (start == Cx4('s', 'l', 'a', 'b') && s[4] == 's') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_slabs, s + 5, e, S_MATCH, S_OPT, "");
	} else if (start == Cx4('s', 't', 'a', 't') && s[4] == 's') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_stats, s + 5, e, S_MATCH, S_OPT, "");
	} else if (start == Cx4('f', 'l', 'u', 's') && s[4] == 'h') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_flush_all, s + 5, e, S_MATCH, S_FLUSH_ALL_1, "_all");
	} else if (start == Cx4('v', 'e', 'r', 's') && s[4] == 'i') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_version, s + 5, e, S_MATCH, S_EOL, "on");
	} else if (start == Cx4('v', 'e', 'r', 'b') && s[4] == 'o') {
		rc = mc_parser_other_command(parser, &mc_command_ascii_verbosity, s + 5, e, S_MATCH, S_VERBOSITY_1, "sity");
	} else if (start == Cx4('q', 'u', 'i', 't')) {
		rc = mc_parser_other_command(parser, &mc_command_ascii_quit, s + 4, e, S_SPACE, S_EOL, "");
	} else {
		DEBUG("unrecognized command");
		rc = mc_parser_other_command(parser, &mc_command_ascii_error, s, e, S_ERROR, S_ERROR, "");
	}

#undef Cx4

leave:
	LEAVE();
	return rc;
}
