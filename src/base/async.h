/*
 * base/async.h - MainMemory asynchronous operations.
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

#ifndef BASE_ASYNC_H
#define BASE_ASYNC_H

#include "common.h"
#include "base/context.h"
#include <sys/types.h>

/* Forward declarations. */
struct iovec;

/**********************************************************************
 * Asynchronous procedure call basic declarations.
 **********************************************************************/

/* The maximum number of arguments for async requests.
   It must be equal to (MM_RING_MPMC_DATA_SIZE - 1). */
#define MM_ASYNC_MAX	(6)

/* Request routines. */
typedef void (*mm_async_routine_t)(struct mm_context *context, uintptr_t arguments[MM_ASYNC_MAX]);

/**********************************************************************
 * Asynchronous procedure call execution.
 **********************************************************************/

void NONNULL(1)
mm_async_handle_calls(struct mm_context *context);

/**********************************************************************
 * Asynchronous procedure calls targeting a single context.
 **********************************************************************/

void NONNULL(1, 2)
mm_async_call_0(struct mm_context *context, mm_async_routine_t r);

bool NONNULL(1, 2)
mm_async_trycall_0(struct mm_context *context, mm_async_routine_t r);

void NONNULL(1, 2)
mm_async_call_1(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1);

bool NONNULL(1, 2)
mm_async_trycall_1(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1);

void NONNULL(1, 2)
mm_async_call_2(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2);

bool NONNULL(1, 2)
mm_async_trycall_2(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2);

void NONNULL(1, 2)
mm_async_call_3(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3);

bool NONNULL(1, 2)
mm_async_trycall_3(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3);

void NONNULL(1, 2)
mm_async_call_4(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

bool NONNULL(1, 2)
mm_async_trycall_4(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

void NONNULL(1, 2)
mm_async_call_5(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

bool NONNULL(1, 2)
mm_async_trycall_5(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

void NONNULL(1, 2)
mm_async_call_6(struct mm_context *context, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

bool NONNULL(1, 2)
mm_async_trycall_6(struct mm_context *context, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

/**********************************************************************
 * Asynchronous procedure calls targeting any random context.
 **********************************************************************/

void NONNULL(1)
mm_async_post_0(mm_async_routine_t r);

void NONNULL(1)
mm_async_post_1(mm_async_routine_t r, uintptr_t a1);

void NONNULL(1)
mm_async_post_2(mm_async_routine_t r, uintptr_t a1, uintptr_t a2);

void NONNULL(1)
mm_async_post_3(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3);

void NONNULL(1)
mm_async_post_4(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4);

void NONNULL(1)
mm_async_post_5(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5);

void NONNULL(1)
mm_async_post_6(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

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

#endif /* BASE_ASYNC_H */
