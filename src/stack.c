/*
 * stack.c - MainMemory stack support for tasks.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "stack.h"

#include "util.h"

#include <sys/mman.h>
#include <unistd.h>

void *
mm_stack_create(uint32_t size)
{
	ENTER();

	int pagesize = getpagesize();

	ASSERT(pagesize > 0);
	ASSERT((size % pagesize) == 0);

	// Full size includes an additional red-zone page.
	uint32_t fullsize = size + pagesize;

	// Allocate the stack along with its red-zone.
	char *p = mmap(NULL, fullsize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (unlikely(p == MAP_FAILED))
		mm_fatal(errno, "failed to allocate a stack (size = %d)", fullsize);

	// Allow access to the stack memory past the red-zone.
	char *stack = p + pagesize;
	if (unlikely(mprotect(stack, size, PROT_READ | PROT_WRITE) < 0))
		mm_fatal(errno, "failed to setup memory access for a stack");

	LEAVE();
	return stack;
}

void
mm_stack_destroy(void *stack, uint32_t size)
{
	ENTER();

	int pagesize = getpagesize();

	ASSERT(pagesize > 0);
	ASSERT((size % pagesize) == 0);

	// Full size includes an additional red-zone page.
	uint32_t fullsize = size + pagesize;

	char *p = stack - pagesize;
	if (unlikely(munmap(p, fullsize) < 0))
		mm_error(errno, "failed to release a stack");

	LEAVE();
}
