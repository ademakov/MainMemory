/*
 * core/work.c - MainMemory work items.
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

#include "core/work.h"

// The memory pool for work items.
static struct mm_pool mm_work_pool;

/**********************************************************************
 * Work item module initialization.
 **********************************************************************/

static void
mm_work_start(void)
{
	ENTER();

	mm_pool_prepare_shared(&mm_work_pool, "work", sizeof(struct mm_work));

	LEAVE();
}

static void
mm_work_stop(void)
{
	ENTER();

	mm_pool_cleanup(&mm_work_pool);

	LEAVE();
}

void
mm_work_init(void)
{
	ENTER();

	mm_core_hook_start(mm_work_start);
	mm_core_hook_stop(mm_work_stop);

	LEAVE();
}

/**********************************************************************
 * Work item creation and destruction.
 **********************************************************************/

struct mm_work *
mm_work_create_low(mm_core_t core)
{
	return mm_pool_shared_alloc_low(core, &mm_work_pool);
}

void
mm_work_destroy_low(mm_core_t core, struct mm_work *work)
{
	mm_pool_shared_free_low(core, &mm_work_pool, work);
}

void
mm_work_complete_noop(struct mm_work *work __attribute__((unused)),
		      mm_value_t result __attribute__((unused)))
{
}
