/*
 * base/exit.c - MainMemory exit handling.
 *
 * Copyright (C) 2013-2016  Aleksey Demakov
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

#include "base/exit.h"

#include "base/list.h"
#include "base/log/log.h"
#include "base/util/hook.h"

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

static void
mm_do_atexit(void)
{
	mm_hook_call(&mm_exit_hook, true);
	mm_log_relay();
	mm_log_flush();
}

void
mm_exit_init(void)
{
	atexit(mm_do_atexit);
}

void
mm_atexit(void (*func)(void))
{
	mm_hook_head_proc(&mm_exit_hook, func);
}

void
mm_exit(int status)
{
	exit(status);
}

/**********************************************************************
 * Abnormal Exit Handling.
 **********************************************************************/

void
mm_abort(void)
{
	mm_log_str("\naborting...\n");
	mm_do_atexit();
	abort();
}
