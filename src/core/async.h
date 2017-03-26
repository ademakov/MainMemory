/*
 * core/async.h - MainMemory asynchronous operations.
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

#ifndef CORE_ASYNC_H
#define CORE_ASYNC_H

#include "common.h"
#include "base/syscall.h"
#include "base/thread/domain.h"

#include <sys/syscall.h>

/* Forward declarations. */
struct iovec;

/**********************************************************************
 * Asynchronous indirect system calls.
 **********************************************************************/

intptr_t NONNULL(1, 2)
mm_async_syscall_1(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1);

intptr_t NONNULL(1, 2)
mm_async_syscall_2(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2);

intptr_t NONNULL(1, 2)
mm_async_syscall_3(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3);

intptr_t NONNULL(1, 2)
mm_async_syscall_4(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

/**********************************************************************
 * Asynchronous system call routines.
 **********************************************************************/

static inline ssize_t
mm_async_read(int fd, void *buffer, size_t nbytes)
{
	struct mm_domain *domain = mm_domain_selfptr();
	return mm_async_syscall_3(domain, "read", MM_SYSCALL_N(SYS_read),
				  fd, (uintptr_t) buffer, nbytes);
}

static inline ssize_t
mm_async_readv(int fd, const struct iovec *iov, int iovcnt)
{
	struct mm_domain *domain = mm_domain_selfptr();
	return mm_async_syscall_3(domain, "readv", MM_SYSCALL_N(SYS_readv),
				  fd, (uintptr_t) iov, iovcnt);
}

static inline ssize_t
mm_async_write(int fd, const void *buffer, size_t nbytes)
{
	struct mm_domain *domain = mm_domain_selfptr();
	return mm_async_syscall_3(domain, "write", MM_SYSCALL_N(SYS_write),
				  fd, (uintptr_t) buffer, nbytes);
}

static inline ssize_t
mm_async_writev(int fd, const struct iovec *iov, int iovcnt)
{
	struct mm_domain *domain = mm_domain_selfptr();
	return mm_async_syscall_3(domain, "writev", MM_SYSCALL_N(SYS_writev),
				  fd, (uintptr_t) iov, iovcnt);
}

static inline ssize_t
mm_async_close(int fd)
{
	struct mm_domain *domain = mm_domain_selfptr();
	return mm_async_syscall_1(domain, "close", MM_SYSCALL_N(SYS_close), fd);
}

#endif /* CORE_ASYNC_H */
