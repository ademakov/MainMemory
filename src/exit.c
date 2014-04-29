/*
 * exit.c - MainMemory exit handling.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#include "exit.h"

#include "hook.h"
#include "log.h"
#include "core.h"
#include "trace.h"

#include <stdlib.h>
#include <unistd.h>

/**********************************************************************
 * Exit Signal Handling.
 **********************************************************************/

int mm_exit_flag = 0;

/**********************************************************************
 * Exit Handling.
 **********************************************************************/

static struct mm_queue MM_QUEUE_INIT(mm_exit_hook);

void
mm_atexit(void (*func)(void))
{
	mm_hook_head_proc(&mm_exit_hook, func);
}

static void
mm_do_atexit(void)
{
	mm_hook_call(&mm_exit_hook, true);
	mm_log_relay();
	mm_log_flush();
}

void
mm_exit(int status)
{
	mm_log_str("exiting...\n");
	mm_do_atexit();
	exit(status);
}

/**********************************************************************
 * Abnormal Exit Handling.
 **********************************************************************/

void
mm_abort(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	mm_where(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\naborting...\n");
	mm_do_atexit();
	abort();
}
