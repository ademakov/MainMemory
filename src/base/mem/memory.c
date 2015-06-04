/*
 * base/memory/memory.c - MainMemory memory subsystem.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#include "base/mem/memory.h"
#include "base/log/log.h"

/**********************************************************************
 * Common Memory Space.
 **********************************************************************/

struct mm_shared_space mm_common_space = {  .space = { NULL } };

static void
mm_common_space_init(void)
{
	mm_shared_space_prepare(&mm_common_space);
}

static void
mm_common_space_term(void)
{
	mm_shared_space_cleanup(&mm_common_space);
	mm_common_space_reset();
}

/**********************************************************************
 * Thread-Shared Memory Initialization and Termination.
 **********************************************************************/

#if ENABLE_SMP
struct mm_shared_space mm_regular_space = { .space = { NULL } };
#else
struct mm_private_space mm_regular_space = { .space = { NULL } };
#endif

static void
mm_regular_space_init(void)
{
#if ENABLE_SMP
	mm_shared_space_prepare(&mm_regular_space);
#else
	mm_private_space_prepare(&mm_regular_space, 16);
#endif
}

static void
mm_regular_space_term(void)
{
#if ENABLE_SMP
	mm_shared_space_cleanup(&mm_regular_space);
#else
	mm_private_space_cleanup(&mm_regular_space);
#endif
}

/**********************************************************************
 * Memory Subsystem Initialization and Termination.
 **********************************************************************/

void
mm_memory_init(void)
{
	mm_alloc_init();
	mm_common_space_init();
	mm_regular_space_init();
}

void
mm_memory_term(void)
{
	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	mm_regular_space_term();
	mm_common_space_term();
}

