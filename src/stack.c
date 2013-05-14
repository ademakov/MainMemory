/*
 * stack.c - MainMemory stack support for tasks.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#include "stack.h"

#include "arch.h"
#include "util.h"

#include <sys/mman.h>
#
void *
mm_stack_create(uint32_t size)
{
	ENTER();

	// Full size includes an additional red-zone page.
	uint32_t fullsize = size + MM_PAGE_SIZE;

	// Allocate the stack along with its red-zone.
	char *p = mmap(NULL, fullsize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (unlikely(p == MAP_FAILED))
		mm_fatal(errno, "failed to allocate a stack (size = %d)", fullsize);

	// Allow access to the stack memory past the red-zone.
	char *stack = p + MM_PAGE_SIZE;
	if (unlikely(mprotect(stack, size, PROT_READ | PROT_WRITE) < 0))
		mm_fatal(errno, "failed to setup memory access for a stack");

	LEAVE();
	return stack;
}

void
mm_stack_destroy(void *stack, uint32_t size)
{
	ENTER();

	// Full size includes an additional red-zone page.
	uint32_t fullsize = size + MM_PAGE_SIZE;

	char *p = stack - MM_PAGE_SIZE;
	if (unlikely(munmap(p, fullsize) < 0))
		mm_error(errno, "failed to release a stack");

	LEAVE();
}
