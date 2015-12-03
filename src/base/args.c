/*
 * base/args.c - Command line argument handling.
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

#include "base/settings.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/util/exit.h"

static uint32_t mm_args_extc;
static uint32_t mm_args_argc;
static char **mm_args_argv;

static const char *mm_args_name;

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

	if (arginfo->param == MM_ARGS_TRIVIAL || arginfo->param == MM_ARGS_SPECIAL) {
		if (sep != NULL)
			mm_args_error(ninfo, info);
		mm_settings_set(arginfo->name, "", true);
		return 1;
	}

	const char *value = NULL;
	if (sep != NULL)
		value = sep + 1;
	else if ((++idx + mm_args_extc) < mm_args_argc && mm_args_argv[idx][0] != '-')
		value = mm_args_argv[idx];
	if (arginfo->param == MM_ARGS_REQUIRED && value == NULL)
		mm_args_error(ninfo, info);
	mm_settings_set(arginfo->name, value, true);
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

		if (arginfo->param == MM_ARGS_TRIVIAL || arginfo->param == MM_ARGS_SPECIAL) {
			mm_settings_set(arginfo->name, "", true);
			continue;
		}

		const char *value = NULL;
		if (*arg)
			value = arg;
		else if ((++idx + mm_args_extc) < mm_args_argc && mm_args_argv[idx][0] != '-')
			value = mm_args_argv[idx];
		if (arginfo->param == MM_ARGS_REQUIRED && value == NULL)
			mm_args_error(ninfo, info);
		mm_settings_set(arginfo->name, value, true);
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

	char *slash = strrchr(argv[0], '/');
	if (slash != NULL)
		mm_args_name = slash + 1;
	else
		mm_args_name = argv[0];

	for (size_t i = 0; i < ninfo; i++) {
		struct mm_args_info *p = &info[i];
		if (p->name != NULL && p->param != MM_ARGS_SPECIAL) {
			if (p->param == MM_ARGS_TRIVIAL)
				mm_settings_settype(p->name, MM_SETTINGS_TRIVIAL);
			else
				mm_settings_settype(p->name, MM_SETTINGS_REGULAR);
		}
	}

	mm_args_parse(ninfo, info);
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
		if (p->param != MM_ARGS_TRIVIAL || p->param == MM_ARGS_SPECIAL) {
			if (p->param == MM_ARGS_OPTIONAL)
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
