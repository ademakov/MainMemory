/*
 * event/nonblock.h - MainMemory non-blocking file descriptor utility.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "event/nonblock.h"

#include "base/log/error.h"

#include <fcntl.h>

void
mm_set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		mm_fatal(errno, "fcntl(..., F_GETFL, ...)");

	flags |= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) < 0)
		mm_fatal(errno, "fcntl(..., F_SETFL, ...)");
}
