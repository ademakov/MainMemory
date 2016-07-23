/*
 * base/thread/request.c - MainMemory thread requests.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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

void
mm_request_handler(uintptr_t *arguments)
{
	struct mm_request_sender *sender = (struct mm_request_sender *) arguments[0];
	intptr_t result = (*sender->request)(&arguments[1]);
	(*sender->response)(sender, result);
}
