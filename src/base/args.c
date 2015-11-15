/*
 * base/args.h - Command line argument handling.
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

#include "base/args.h"

#include "base/hashmap.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/memory/alloc.h"
#include "base/memory/memory.h"
#include "base/util/exit.h"

struct mm_args_entry
{
	struct mm_hashmap_entry entry;
	const char *value;
};

static uint32_t mm_args_extc;
static uint32_t mm_args_argc;
static char **mm_args_argv;

static const char *mm_args_name;

static struct mm_hashmap mm_args_table;

static const char *mm_args_empty = "";

static const char *
mm_args_copy_value(const char *value)
{
	if (value == NULL || *value == 0)
		return mm_args_empty;
	return mm_global_strdup(value);
}

static void
mm_args_free_value(const char *value)
{
	if (unlikely(value == NULL))
		return;
	if (value == mm_args_empty)
		return;
	mm_global_free((char *) value);
}

static void
mm_args_free_entry(struct mm_hashmap *map __mm_unused__, struct mm_hashmap_entry *hent)
{
	struct mm_args_entry *aent = containerof(hent, struct mm_args_entry, entry);
	mm_args_free_value(aent->value);
	mm_global_free(aent);
}

/**********************************************************************
 * Argument parsing.
 **********************************************************************/

static void
mm_args_shift(uint32_t idx)
{
	memmove(&mm_args_argv[idx],
		&mm_args_argv[idx + 1],
		(mm_args_argc - idx) * sizeof(mm_args_argv[0]));
}

static void
mm_args_extra_shift(uint32_t idx, char *arg)
{
	mm_args_extc++;
	mm_args_shift(idx);
	mm_args_argv[mm_args_argc - 1] = arg;
}

static void
mm_args_final_shift(uint32_t idx)
{
	if (mm_args_extc == 0) {
		mm_args_extc = mm_args_argc - idx;
	} else {
		while ((idx + mm_args_extc) < mm_args_argc) {
			char *arg = mm_args_argv[idx];
			mm_args_extra_shift(idx, arg);
		}
	}
}

static void __attribute__((noreturn))
mm_args_error(size_t ninfo, struct mm_args_info *info)
{
	mm_args_usage(ninfo, info);
	mm_exit(EXIT_FAILURE);
}

static uint32_t
mm_args_parse_name(uint32_t idx, size_t ninfo, struct mm_args_info *info)
{
	char *arg = &mm_args_argv[idx][2];
	size_t len = strlen(arg);

	const char *sep = memchr(arg, '=', len);
	if (sep != NULL)
		len = sep - arg;
	if (unlikely(len == 0))
		mm_args_error(ninfo, info);

	struct mm_args_info *arginfo = NULL;
	for (size_t i = 0; i < ninfo; i++) {
		struct mm_args_info *p = &info[i];
		if (strlen(p->name) == len && memcmp(p->name, arg, len) == 0) {
			arginfo = p;
			break;
		}
	}

	if (arginfo == NULL)
		mm_args_error(ninfo, info);

	if (arginfo->param == MM_ARGS_PARAM_NONE) {
		if (sep != NULL)
			mm_args_error(ninfo, info);
		mm_args_setvalue(arginfo->name, NULL);
		return 1;
	}

	const char *value = NULL;
	if (sep != NULL)
		value = sep + 1;
	else if ((++idx + mm_args_extc) < mm_args_argc && mm_args_argv[idx][0] != '-')
		value = mm_args_argv[idx];
	if (arginfo->param == MM_ARGS_PARAM_REQUIRED && value == NULL)
		mm_args_error(ninfo, info);
	mm_args_setvalue(arginfo->name, value);
	return sep != NULL ? 1 : 2;
}

static uint32_t
mm_args_parse_flags(uint32_t idx, size_t ninfo, struct mm_args_info *info)
{
	const char *arg = &mm_args_argv[idx][1];
	for (int flag = *arg++; flag; flag = *arg++) {
		struct mm_args_info *arginfo = NULL;
		for (size_t i = 0; i < ninfo; i++) {
			struct mm_args_info *p = &info[i];
			if (flag == p->flag) {
				arginfo = p;
				break;
			}
		}

		if (arginfo == NULL)
			mm_args_error(ninfo, info);

		if (arginfo->param == MM_ARGS_PARAM_NONE) {
			mm_args_setvalue(arginfo->name, NULL);
			continue;
		}

		const char *value = NULL;
		if (*arg)
			value = arg;
		else if ((++idx + mm_args_extc) < mm_args_argc && mm_args_argv[idx][0] != '-')
			value = mm_args_argv[idx];
		if (arginfo->param == MM_ARGS_PARAM_REQUIRED && value == NULL)
			mm_args_error(ninfo, info);
		mm_args_setvalue(arginfo->name, value);
		return *arg ? 1 : 2;
	}
	return 1;
}

static void
mm_args_parse(size_t ninfo, struct mm_args_info *info)
{
	uint32_t idx = 1;
	while ((idx + mm_args_extc) < mm_args_argc) {
		char *arg = mm_args_argv[idx];
		if (arg[0] != '-') {
			// A non-option argument.
			mm_args_extra_shift(idx, arg);
		} else if (arg[1] != '-') {
			if (arg[1] == 0) {
				// Handle a special case ("-").
				mm_args_extra_shift(idx, arg);
			} else {
				// Handle a short option (e.g. "-o").
				idx += mm_args_parse_flags(idx, ninfo, info);
			}
		} else {
			if (arg[2] == 0) {
				// Handle another special case ("--").
				mm_args_final_shift(++idx);
			} else {
				// Handle a long option argument (e.g. "--long-option").
				idx += mm_args_parse_name(idx, ninfo, info);
			}
		}
	}
}

/**********************************************************************
 * Argument handling.
 **********************************************************************/

void __attribute__((nonnull(2)))
mm_args_init(int argc, char *argv[], size_t ninfo, struct mm_args_info *info)
{
	if (unlikely(argc <= 0))
		mm_fatal(0, "Missing command line arguments");

	mm_args_extc = 0;
	mm_args_argc = argc;
	mm_args_argv = argv;
	mm_hashmap_prepare(&mm_args_table, &mm_global_arena);

	char *slash = strrchr(argv[0], '/');
	if (slash != NULL)
		mm_args_name = slash + 1;
	else
		mm_args_name = argv[0];

	mm_args_parse(ninfo, info);
}

void
mm_args_term(void)
{
	mm_hashmap_cleanup(&mm_args_table, mm_args_free_entry);
}

const char *
mm_args_getname(void)
{
	return mm_args_name;
}

int
mm_args_getargc(void)
{
	if (mm_args_argc == 0 || mm_args_argv == NULL)
		mm_abort();
	return mm_args_extc;
}

char **
mm_args_getargv(void)
{
	if (mm_args_argc == 0 || mm_args_argv == NULL)
		mm_abort();
	return mm_args_argv + mm_args_argc - mm_args_extc;
}

const char * __attribute__((nonnull(1)))
mm_args_getvalue(const char *key)
{
	size_t len = strlen(key);
	if (unlikely(len > UINT32_MAX))
		return NULL;

	struct mm_hashmap_entry *hent = mm_hashmap_lookup(&mm_args_table, key, len);
	if (hent == NULL)
		return NULL;

	struct mm_args_entry *aent = containerof(hent, struct mm_args_entry, entry);
	return aent->value;
}

void __attribute__((nonnull(1)))
mm_args_setvalue(const char *key, const char *value)
{
	size_t len = strlen(key);
	if (unlikely(len > UINT32_MAX))
		mm_fatal(0, "too long arg name");

	struct mm_hashmap_entry *hent = mm_hashmap_lookup(&mm_args_table, key, len);
	if (hent == NULL) {
		struct mm_args_entry *aent = mm_global_alloc(sizeof(struct mm_args_entry));
		key = mm_global_memdup(key, len);
		mm_hashmap_setkey(&aent->entry, key, len);
		aent->value = mm_args_copy_value(value);
		mm_hashmap_insert(&mm_args_table, &aent->entry);
	} else {
		struct mm_args_entry *aent = containerof(hent, struct mm_args_entry, entry);
		mm_args_free_value(aent->value);
		aent->value = mm_args_copy_value(value);
	}
}

/**********************************************************************
 * Argument usage message.
 **********************************************************************/

static const char *mm_args_none[] = { "", "" };
static const char *mm_args_required[] = { " <ARG>", "=<ARG>" };
static const char *mm_args_optional[] = { " [<ARG>]", "=[<ARG>]" };

void
mm_args_usage(size_t ninfo, struct mm_args_info *info)
{
	mm_log_fmt("Usage: %s [options]\n", mm_args_getname());
	mm_log_fmt("Options:\n");
	for (size_t i = 0; i < ninfo; i++) {
		struct mm_args_info *p = &info[i];

		const char **arg = mm_args_none;
		if (p->param != MM_ARGS_PARAM_NONE) {
			if (p->param == MM_ARGS_PARAM_OPTIONAL)
				arg = mm_args_optional;
			else
				arg = mm_args_required;
		}

		mm_log_fmt("  ");
		if (p->flag)
			mm_log_fmt("-%c%s", p->flag, arg[0]);
		if (p->name != NULL) {
			if (p->flag)
				mm_log_fmt(", ");
			mm_log_fmt("--%s%s", p->name, arg[1]);
		}

		if (p->help != NULL)
			mm_log_fmt("%s", p->help);
		mm_log_fmt("\n");
	}
}
