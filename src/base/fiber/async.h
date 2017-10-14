/*
 * base/fiber/async.h - MainMemory asynchronous operations.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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
#include "base/syscall.h"
#include "base/event/dispatch.h"
#include "base/fiber/strand.h"

#include <sys/syscall.h>

/* Forward declarations. */
struct iovec;

/**********************************************************************
 * Asynchronous indirect system calls.
 **********************************************************************/

intptr_t NONNULL(1, 2)
mm_async_syscall_1(struct mm_event_dispatch *dispatch, const char *name, int n,
		   uintptr_t a1);

intptr_t NONNULL(1, 2)
mm_async_syscall_2(struct mm_event_dispatch *dispatch, const char *name, int n,
		   uintptr_t a1, uintptr_t a2);

intptr_t NONNULL(1, 2)
mm_async_syscall_3(struct mm_event_dispatch *dispatch, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3);

intptr_t NONNULL(1, 2)
mm_async_syscall_4(struct mm_event_dispatch *dispatch, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

/**********************************************************************
 * Asynchronous system call routines.
 **********************************************************************/

static inline ssize_t
mm_async_read(int fd, void *buffer, size_t nbytes)
{
	struct mm_strand *strand = mm_strand_selfptr();
	return mm_async_syscall_3(strand->dispatch, "read", MM_SYSCALL_N(SYS_read),
				  fd, (uintptr_t) buffer, nbytes);
}

static inline ssize_t
mm_async_readv(int fd, const struct iovec *iov, int iovcnt)
{
	struct mm_strand *strand = mm_strand_selfptr();
	return mm_async_syscall_3(strand->dispatch, "readv", MM_SYSCALL_N(SYS_readv),
				  fd, (uintptr_t) iov, iovcnt);
}

static inline ssize_t
mm_async_write(int fd, const void *buffer, size_t nbytes)
{
	struct mm_strand *strand = mm_strand_selfptr();
	return mm_async_syscall_3(strand->dispatch, "write", MM_SYSCALL_N(SYS_write),
				  fd, (uintptr_t) buffer, nbytes);
}

static inline ssize_t
mm_async_writev(int fd, const struct iovec *iov, int iovcnt)
{
	struct mm_strand *strand = mm_strand_selfptr();
	return mm_async_syscall_3(strand->dispatch, "writev", MM_SYSCALL_N(SYS_writev),
				  fd, (uintptr_t) iov, iovcnt);
}

static inline ssize_t
mm_async_close(int fd)
{
	struct mm_strand *strand = mm_strand_selfptr();
	return mm_async_syscall_1(strand->dispatch, "close", MM_SYSCALL_N(SYS_close), fd);
}

#endif /* BASE_FIBER_ASYNC_H */
