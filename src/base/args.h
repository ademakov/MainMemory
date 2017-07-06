/*
 * base/args.h - Command line argument handling.
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

#ifndef BASE_ARGS_H
#define BASE_ARGS_H

#include "common.h"

/*
 * Command line arguments are parsed and all the given options are put into
 * the central storage of settings (see the "settings.h" file). The storage
 * is also used for values loaded from the configuration file. If the same
 * option is present both in the command line and in the configuration file
 * then the command line takes precedence.
 *
 * Non-option arguments are put together in the array that is available via
 * mm_args_argc() and mm_args_argv() functions.
 *
 * The accepted options are provided with a table of mm_args_info entries.
 * It is used both to enable option parsing and to define the setting info
 * that is reused during configuration file parsing.
 *
 * In some cases, like for "--help" option, there is no point to have such
 * a setting in the configuration file. So the MM_ARGS_COMMAND param should
 * be used to mark these options.
 *
 * The same argument info table might be used to generate a usage message.
 * So the table contains help strings. Normally a help string describes an
 * option. But there might be purely informational entries with NULL name
 * and zero flag. Such entries are ignored by the options parsing logic and
 * only take part in the usage message. If this is the very first entry in
 * the table then the informational string is printed directly in the first
 * usage line. The following strings are printed on separate lines between
 * the neighbor options.
 */

typedef enum {
	/* An option without parameter. */
	MM_ARGS_TRIVIAL = 0,
	/* An option without parameter and without a configuration file
	   counterpart. */
	MM_ARGS_COMMAND,
	/* A option with optional parameter. */
	MM_ARGS_OPTIONAL,
	/* A option with required parameter. */
	MM_ARGS_REQUIRED,
} mm_args_param_t;

struct mm_args_info
{
	/* The long option name. */
	const char *name;

	/* The short option name. */
	int flag;

	/* The acceptance of an option parameter. */
	mm_args_param_t param;

	/* The option documentation. */
	const char *help;
};

void NONNULL(2)
mm_args_init(int argc, char *argv[], size_t ninfo, const struct mm_args_info *info);

const char *
mm_args_name(void);

int
mm_args_argc(void);

char **
mm_args_argv(void);

void
mm_args_usage(size_t ninfo, const struct mm_args_info *info);

#endif /* BASE_ARGS_H */
