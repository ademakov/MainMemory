/*
 * base/fiber/async.h - MainMemory asynchronous operations.
 *
 * Copyright (C) 2015-2019  Aleksey Demakov
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

#ifndef BASE_FIBER_ASYNC_H
#define BASE_FIBER_ASYNC_H

#include "common.h"
#include <sys/types.h>

/* Forward declarations. */
struct iovec;

/**********************************************************************
 * Asynchronous system call routines.
 **********************************************************************/

ssize_t
mm_async_read(int fd, void *buffer, size_t nbytes);

ssize_t
mm_async_readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t
mm_async_write(int fd, const void *buffer, size_t nbytes);

ssize_t
mm_async_writev(int fd, const struct iovec *iov, int iovcnt);

ssize_t
mm_async_close(int fd);

#endif /* BASE_FIBER_ASYNC_H */
