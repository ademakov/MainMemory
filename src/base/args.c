/*
 * base/args.c - Command line argument handling.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#include "base/exit.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/settings.h"

#include <stdlib.h>

static uint32_t mm_args_ec;
static uint32_t mm_args_ac;
static char **mm_args_av;

/**********************************************************************
 * Argument parsing.
 **********************************************************************/

static void
mm_args_shift(uint32_t idx)
{
	memmove(&mm_args_av[idx],
		&mm_args_av[idx + 1],
		(mm_args_ac - idx) * sizeof(mm_args_av[0]));
}

static void
mm_args_extra_shift(uint32_t idx, char *arg)
{
	mm_args_ec++;
	mm_args_shift(idx);
	mm_args_av[mm_args_ac - 1] = arg;
}

static void
mm_args_final_shift(uint32_t idx)
{
	if (mm_args_ec == 0) {
		mm_args_ec = mm_args_ac - idx;
	} else {
		while ((idx + mm_args_ec) < mm_args_ac) {
			char *arg = mm_args_av[idx];
			mm_args_extra_shift(idx, arg);
		}
	}
}

static void NORETURN
mm_args_error(size_t ninfo, const struct mm_args_info *info)
{
	mm_args_usage(ninfo, info);
	mm_exit(MM_EXIT_USAGE);
}

static uint32_t
mm_args_parse_name(uint32_t idx, size_t ninfo, const struct mm_args_info *info)
{
	char *arg = &mm_args_av[idx][2];
	size_t len = strlen(arg);

	const char *sep = memchr(arg, '=', len);
	if (sep != NULL)
		len = sep - arg;
	if (unlikely(len == 0))
		mm_args_error(ninfo, info);

	const struct mm_args_info *arginfo = NULL;
	for (size_t i = 0; i < ninfo; i++) {
		const struct mm_args_info *p = &info[i];
		if (p->name == NULL)
			continue;
		if (strlen(p->name) == len && memcmp(p->name, arg, len) == 0) {
			arginfo = p;
			break;
		}
	}

	if (arginfo == NULL)
		mm_args_error(ninfo, info);

	if (arginfo->param == MM_ARGS_TRIVIAL || arginfo->param == MM_ARGS_COMMAND) {
		if (sep != NULL)
			mm_args_error(ninfo, info);
		mm_settings_set(arginfo->name, "true", true);
		return 1;
	}

	const char *value = NULL;
	if (sep != NULL)
		value = sep + 1;
	else if ((++idx + mm_args_ec) < mm_args_ac && mm_args_av[idx][0] != '-')
		value = mm_args_av[idx];
	if (arginfo->param == MM_ARGS_REQUIRED && value == NULL)
		mm_args_error(ninfo, info);
	mm_settings_set(arginfo->name, value, true);
	return sep != NULL ? 1 : 2;
}

static uint32_t
mm_args_parse_flags(uint32_t idx, size_t ninfo, const struct mm_args_info *info)
{
	const char *arg = &mm_args_av[idx][1];
	for (int flag = *arg++; flag; flag = *arg++) {
		const struct mm_args_info *arginfo = NULL;
		for (size_t i = 0; i < ninfo; i++) {
			const struct mm_args_info *p = &info[i];
			if (flag == p->flag) {
				arginfo = p;
				break;
			}
		}

		if (arginfo == NULL)
			mm_args_error(ninfo, info);

		if (arginfo->param == MM_ARGS_TRIVIAL || arginfo->param == MM_ARGS_COMMAND) {
			mm_settings_set(arginfo->name, "", true);
			continue;
		}

		const char *value = NULL;
		if (*arg)
			value = arg;
		else if ((++idx + mm_args_ec) < mm_args_ac && mm_args_av[idx][0] != '-')
			value = mm_args_av[idx];
		if (arginfo->param == MM_ARGS_REQUIRED && value == NULL)
			mm_args_error(ninfo, info);
		mm_settings_set(arginfo->name, value, true);
		return *arg ? 1 : 2;
	}
	return 1;
}

static void
mm_args_parse(size_t ninfo, const struct mm_args_info *info)
{
	uint32_t idx = 1;
	while ((idx + mm_args_ec) < mm_args_ac) {
		char *arg = mm_args_av[idx];
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

void NONNULL(2)
mm_args_init(int ac, char *av[], size_t ninfo, const struct mm_args_info *info)
{
	if (unlikely(ac <= 0))
		mm_fatal(0, "Missing command line arguments");

	mm_args_ec = 0;
	mm_args_ac = ac;
	mm_args_av = av;

	for (size_t i = 0; i < ninfo; i++) {
		const struct mm_args_info *p = &info[i];
		if (p->name != NULL && p->param != MM_ARGS_COMMAND) {
			if (p->param == MM_ARGS_TRIVIAL)
				mm_settings_set_info(p->name, MM_SETTINGS_BOOLEAN);
			else
				mm_settings_set_info(p->name, MM_SETTINGS_REGULAR);
		}
	}

	mm_args_parse(ninfo, info);
}

const char *
mm_args_name(void)
{
	char *slash = strrchr(mm_args_av[0], '/');
	if (slash != NULL)
		return slash + 1;
	return mm_args_av[0];
}

int
mm_args_argc(void)
{
	VERIFY(mm_args_ac != 0 && mm_args_av != NULL);
	return mm_args_ec;
}

char **
mm_args_argv(void)
{
	VERIFY(mm_args_ac != 0 && mm_args_av != NULL);
	return mm_args_av + mm_args_ac - mm_args_ec;
}

/**********************************************************************
 * Argument usage message.
 **********************************************************************/

static const char *mm_args_none[] = { "", "" };
static const char *mm_args_required[] = { " <ARG>", "=<ARG>" };
static const char *mm_args_optional[] = { " [<ARG>]", "=[<ARG>]" };

void
mm_args_usage(size_t ninfo, const struct mm_args_info *info)
{
	size_t index = 0;
	const char *param = NULL;

	if (ninfo != 0 && info->flag == 0 && info->name == NULL) {
		param = info->help;
		index++;
	}

	if (index == ninfo) {
		if (param == NULL)
			mm_log_fmt("Usage: %s\n", mm_args_name());
		else
			mm_log_fmt("Usage: %s %s\n", mm_args_name(), param);
		return;
	}

	if (param == NULL)
		mm_log_fmt("Usage: %s [options]\n", mm_args_name());
	else
		mm_log_fmt("Usage: %s [options] %s\n", mm_args_name(), param);

	mm_log_fmt("Options:\n");
	for (; index < ninfo; index++) {
		const struct mm_args_info *p = &info[index];

		const char **arg;
		if (p->param == MM_ARGS_OPTIONAL)
			arg = mm_args_optional;
		else if (p->param == MM_ARGS_REQUIRED)
			arg = mm_args_required;
		else
			arg = mm_args_none;

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
