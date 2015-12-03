/*
 * base/util/exit.h - MainMemory exit handling.
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

#ifndef BASE_UTIL_EXIT_H
#define BASE_UTIL_EXIT_H

#include "common.h"
#include "arch/memory.h"

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

void
mm_atexit(void (*func)(void));

void NORETURN
mm_exit(int status);

/**********************************************************************
 * Abnormal Termination.
 **********************************************************************/

void NORETURN
mm_abort(void);

#endif /* BASE_UTIL_EXIT_H */
