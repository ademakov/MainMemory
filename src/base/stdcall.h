/*
 * base/stdcall.h - MainMemory standard system calls.
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

#ifndef BASE_STDCALL_H
#define BASE_STDCALL_H

#include "common.h"

#define ENABLE_SYSCALL_WRAPPERS	1

/*
 * This header provides wrappers for common system calls. Such wrappers
 * are normally defined in the <unistd.h> header and libc library. But
 * the wrappers defined here bypass thread cancellation mechanism that
 * is often present in libc/libpthread versions of the wrappers.
 */

#if ENABLE_SYSCALL_WRAPPERS

#include "arch/syscall.h"
#include <sys/syscall.h>

static inline ssize_t
mm_read(int fd, void *buf, size_t cnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_read), fd, (intptr_t) buf, cnt);
}

static inline ssize_t
mm_write(int fd, const void *buf, size_t cnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_write), fd, (intptr_t) buf, cnt);
}

static inline ssize_t
mm_readv(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_readv), fd, (intptr_t) iov, iovcnt);
}

static inline ssize_t
mm_writev(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_writev), fd, (intptr_t) iov, iovcnt);
}

#else

#include <unistd.h>

#define mm_read		read
#define mm_write	write
#define mm_readv	readv
#define mm_writev	writev

#endif

#endif /* BASE_STDCALL_H */
