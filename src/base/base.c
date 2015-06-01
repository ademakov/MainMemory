/*
 * base/base.c - Base library setup.
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

#include "base/base.h"
#include "base/mem/memory.h"
#include "base/sys/clock.h"
#include "base/thread/thread.h"

void
mm_base_init(struct mm_memory_params *params)
{
	mm_memory_init(params);
	mm_thread_init();
	mm_clock_init();
}

void
mm_base_term(void)
{
	mm_memory_term();
}
