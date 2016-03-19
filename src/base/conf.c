/*
 * base/conf.c - MainMemory configuration file handling.
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

#include "base/conf.h"

#include "base/json.h"
#include "base/report.h"
#include "base/settings.h"
#include "base/stdcall.h"
#include "base/memory/global.h"

static char mm_conf_buffer[1024];


static void
mm_conf_read(int fd, const char *name, struct mm_json_reader *reader)
{
	ssize_t n = mm_read(fd, mm_conf_buffer, sizeof mm_conf_buffer);
	if (n < 0)
		mm_fatal(errno, "configuration file: %s", name);
	if (n == 0)
		mm_fatal(0, "configuration file: %s: invalid data", name);
	mm_json_reader_feed(reader, mm_conf_buffer, n);
}

static mm_json_token_t
mm_conf_next(int fd, const char *name, struct mm_json_reader *reader)
{
	for (;;) {
		mm_json_token_t token = mm_json_reader_next(reader);
		if (token == MM_JSON_INVALID)
			mm_fatal(0, "configuration file: %s: invalid data", name);
		if (token != MM_JSON_PARTIAL && token != MM_JSON_START_DOCUMENT)
			return token;
		mm_conf_read(fd, name, reader);
	}
}

static void
mm_conf_skip(int fd, const char *name, struct mm_json_reader *reader)
{
	for (;;) {
		mm_json_token_t token = mm_json_reader_skip(reader);
		if (token == MM_JSON_INVALID)
			mm_fatal(0, "configuration file: %s: invalid data", name);
		if (token != MM_JSON_PARTIAL)
			return;
		mm_conf_read(fd, name, reader);
	}
}

void
mm_conf_load(const char *name)
{
	bool fatal = true;
	if (name == NULL) {
		fatal = false;
		name = "mmem.json";
	}

	mm_brief("load configuration: %s", name);
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		if (fatal)
			mm_fatal(errno, "configuration file: %s", name);
		mm_error(errno, "configuration file: %s", name);
		return;
	}

	struct mm_json_reader reader;
	mm_json_reader_prepare(&reader, &mm_global_arena);
	mm_json_token_t token = mm_conf_next(fd, name, &reader);
	if (token != MM_JSON_START_OBJECT)
		mm_fatal(0, "configuration file: %s: invalid data", name);

	do {
		token = mm_conf_next(fd, name, &reader);
		if (token == MM_JSON_END_OBJECT)
			break;

		char *key = mm_json_reader_string_strdup(&reader);
		mm_settings_type_t type = mm_settings_gettype(key);
		if (type == MM_SETTINGS_UNKNOWN) {
			mm_conf_skip(fd, key, &reader);
		} else {
			token = mm_conf_next(fd, name, &reader);
			if (token == MM_JSON_TRUE) {
				mm_settings_set(key, "true", false);
			} else if (token == MM_JSON_STRING && type != MM_SETTINGS_TRIVIAL) {
				char *value = mm_json_reader_string_strdup(&reader);
				mm_settings_set(key, value, false);
				mm_arena_free(reader.arena, value);
			} else if (token == MM_JSON_NUMBER && type != MM_SETTINGS_TRIVIAL) {
				char *value = mm_json_reader_strdup(&reader);
				mm_settings_set(key, value, false);
				mm_arena_free(reader.arena, value);
			} else if (token != MM_JSON_FALSE) {
				mm_fatal(0, "configuration file '%s' has invalid '%s' value",
					 name, key);
			}
		}
		mm_arena_free(reader.arena, key);

	} while (token != MM_JSON_END_OBJECT);

	mm_json_reader_cleanup(&reader);
	mm_close(fd);
}
