/*
 * base/thread/request.c - MainMemory thread requests.
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

#include "base/thread/request.h"

#include "arch/syscall.h"
#include <sys/syscall.h>

/**********************************************************************
 * System call requests.
 **********************************************************************/

uintptr_t
mm_request_syscall_handler(uintptr_t context UNUSED, uintptr_t *arguments)
{
	uintptr_t number = arguments[0];
	uintptr_t arg_1 = arguments[1];
	uintptr_t arg_2 = arguments[2];
	uintptr_t arg_3 = arguments[3];
	uintptr_t arg_4 = arguments[4];

	return mm_syscall_4(number, arg_1, arg_2, arg_3, arg_4);
}
