/*
 * exit.h - MainMemory exit handling.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef EXIT_H
#define EXIT_H

#include "common.h"

/**********************************************************************
 * Exit Signal Handling.
 **********************************************************************/

extern int mm_exit_flag;

static inline void
mm_exit_set(void)
{
	mm_memory_store(mm_exit_flag, 1);
}

static inline bool
mm_exit_test(void)
{
	return mm_memory_load(mm_exit_flag) != 0;
}

/**********************************************************************
 * Exit Handling.
 **********************************************************************/

void mm_atexit(void (*func)(void));

void mm_exit(int status)
	__attribute__((noreturn));

/**********************************************************************
 * Abnormal Termination.
 **********************************************************************/

#define ABORT() mm_abort(__FILE__, __LINE__, __FUNCTION__, "ABORT")

void mm_abort(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((nonnull(4)))
	__attribute__((noreturn));

#endif /* EXIT_H */
