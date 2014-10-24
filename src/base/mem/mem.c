/*
 * base/mem/mem.c - MainMemory memory subsystem.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "base/mem/mem.h"
#include "base/mem/alloc.h"
#include "base/mem/cdata.h"
#include "base/mem/space.h"

void
mm_memory_init(mm_chunk_alloc_t alloc, mm_chunk_free_t free)
{
	mm_alloc_init();
	mm_cdata_init();
	mm_common_space_init();
	mm_chunk_set_special_alloc(alloc, free);
}

void
mm_memory_term(void)
{
	mm_common_space_term();
	mm_cdata_term();
}
