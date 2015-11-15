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

#ifndef BASE_ARGS_H
#define BASE_ARGS_H

#include "common.h"

typedef enum {
	MM_ARGS_PARAM_NONE = 0,
	MM_ARGS_PARAM_OPTIONAL,
	MM_ARGS_PARAM_REQUIRED,
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

void __attribute__((nonnull(2)))
mm_args_init(int argc, char *argv[], size_t ninfo, struct mm_args_info *info);

void
mm_args_term(void);

const char *
mm_args_getname(void);

int
mm_args_getargc(void);

char **
mm_args_getargv(void);

const char * __attribute__((nonnull(1)))
mm_args_getvalue(const char *key);

void __attribute__((nonnull(1)))
mm_args_setvalue(const char *key, const char *value);

void
mm_args_usage(size_t ninfo, struct mm_args_info *info);

#endif /* BASE_ARGS_H */
