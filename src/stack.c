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

#include "log.h"
#include "trace.h"

#include <sys/mman.h>

void *
mm_stack_create(uint32_t stack_size, uint32_t guard_size)
{
	ENTER();
	ASSERT((stack_size % MM_PAGE_SIZE) == 0);
	ASSERT((guard_size % MM_PAGE_SIZE) == 0);
	ASSERT(guard_size < stack_size);

	// Allocate a stack area along with the red-zone.
	char *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (unlikely(stack == MAP_FAILED)) {
		mm_fatal(errno, "failed to allocate a stack (size = %d)", stack_size);
	}

	// Disable access to the red-zone.
	if (likely(guard_size)) {
		if (unlikely(mprotect(stack, guard_size, PROT_NONE) < 0)) {
			mm_fatal(errno, "failed to setup stack red-zone");
		}
	}

	LEAVE();
	return stack;
}

void
mm_stack_destroy(void *stack, uint32_t stack_size)
{
	ENTER();
	ASSERT((stack_size % MM_PAGE_SIZE) == 0);

	if (unlikely(munmap(stack, stack_size) < 0)) {
		mm_error(errno, "failed to release a stack");
	}

	LEAVE();
}
