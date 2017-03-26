/*
 * base/stdcall.h - MainMemory standard system calls.
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

#ifndef BASE_STDCALL_H
#define BASE_STDCALL_H

#include "common.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>

/*
 * This header provides wrappers for common system calls. Such wrappers
 * are normally defined in the <unistd.h> header and libc library. But
 * the wrappers defined here might be a bit cheaper to execute as they
 * bypass the thread cancellation mechanism often implemented by libc
 * or libpthread versions of the wrappers.
 */

#if ENABLE_INLINE_SYSCALLS

#include "base/syscall.h"
#include <sys/syscall.h>

#if defined(SYS_socketcall) && !defined(SYS_accept)
# include <linux/net.h>
# define LINUX_SOCKETCALL	1
#else
# undef LINUX_SOCKETCALL
#endif

static inline ssize_t
mm_read(int fd, void *buf, size_t cnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_read), fd, (uintptr_t) buf, cnt);
}

static inline ssize_t
mm_write(int fd, const void *buf, size_t cnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_write), fd, (uintptr_t) buf, cnt);
}

static inline ssize_t
mm_readv(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_readv), fd, (uintptr_t) iov, iovcnt);
}

static inline ssize_t
mm_writev(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_syscall_3(MM_SYSCALL_N(SYS_writev), fd, (uintptr_t) iov, iovcnt);
}

static inline int
mm_close(int fd)
{
	return mm_syscall_1(MM_SYSCALL_N(SYS_close), fd);
}

static inline int
mm_socket(int domain, int type, int protocol)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { domain, type, protocol };
	return mm_syscall_2(SYS_socketcall, SYS_SOCKET, (uintptr_t) args);
#else
	return mm_syscall_3(MM_SYSCALL_N(SYS_socket), domain, type, protocol);
#endif
}

static inline int
mm_connect(int sock, const struct sockaddr *addr, socklen_t addr_len)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { sock, (uintptr_t) addr, addr_len };
	return mm_syscall_2(SYS_socketcall, SYS_CONNECT, (uintptr_t) args);
#else
	return mm_syscall_3(MM_SYSCALL_N(SYS_connect), sock, (uintptr_t) addr, addr_len);
#endif
}

static inline int
mm_bind(int sock, const struct sockaddr *addr, socklen_t addr_len)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { sock, (uintptr_t) addr, addr_len };
	return mm_syscall_2(SYS_socketcall, SYS_BIND, (uintptr_t) args);
#else
	return mm_syscall_3(MM_SYSCALL_N(SYS_bind), sock, (uintptr_t) addr, addr_len);
#endif
}

static inline int
mm_listen(int socket, int backlog)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { socket, backlog };
	return mm_syscall_2(SYS_socketcall, SYS_LISTEN, (uintptr_t) args);
#else
	return mm_syscall_2(MM_SYSCALL_N(SYS_listen), socket, backlog);
#endif
}

static inline int
mm_accept(int sock, struct sockaddr *restrict addr, socklen_t *restrict addr_len)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { sock, (uintptr_t) addr, (uintptr_t) addr_len };
	return mm_syscall_2(SYS_socketcall, SYS_ACCEPT, (uintptr_t) args);
#else
	return mm_syscall_3(MM_SYSCALL_N(SYS_accept), sock, (uintptr_t) addr, (uintptr_t) addr_len);
#endif
}

static inline int
mm_shutdown(int sock, int how)
{
#if LINUX_SOCKETCALL
	uintptr_t args[] = { sock, how };
	return mm_syscall_2(SYS_socketcall, SYS_SHUTDOWN, (uintptr_t) args);
#else
	return mm_syscall_2(MM_SYSCALL_N(SYS_shutdown), sock, how);
#endif
}

#else /* !ENABLE_INLINE_SYSCALLS */

#include <unistd.h>

#define mm_read		read
#define mm_write	write
#define mm_readv	readv
#define mm_writev	writev
#define mm_close	close
#define mm_socket	socket
#define mm_connect	connect
#define mm_bind		bind
#define mm_listen	listen
#define mm_accept	accept
#define mm_shutdown	shutdown

#endif /* !ENABLE_INLINE_SYSCALLS */

#endif /* BASE_STDCALL_H */
