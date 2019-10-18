/*
 * base/fiber/async.c - MainMemory asynchronous operations.
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

#include "base/fiber/async.h"

#include "base/list.h"
#include "base/report.h"
#include "base/syscall.h"
#include "base/event/event.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"
#include "base/fiber/strand.h"


#include <sys/syscall.h>
#include <sys/uio.h>

/* Asynchronous operation information. */
struct mm_async_node
{
	/* Link in the per-thread list of async operations. */
	struct mm_link link;

	/* The fiber that requested the operation. */
	struct mm_fiber *fiber;

	/* Operation status. */
	mm_value_t status;

	/* Operation result. */
	mm_value_t result;
	int error;

	/* Human readable information for debugging. */
	const char *description;
};

/**********************************************************************
 * Asynchronous system call handlers.
 **********************************************************************/

static void
mm_async_syscall_result(struct mm_async_node *node, intptr_t result)
{
	// Store the result.
	node->result = result;
	if (result < 0)
		node->error = errno;

	// Ensure its visibility.
	mm_memory_store_fence();
	// Indicate the operation completion.
	mm_memory_store(node->status, 0);

	// Notify the caller.
	mm_strand_run_fiber(node->fiber);
}

static void
mm_async_syscall_1_handler(struct mm_event_listener *listener UNUSED, uintptr_t *arguments)
{
	// Make the system call.
	uintptr_t num = arguments[1];
	uintptr_t arg_1 = arguments[2];
	intptr_t result = mm_syscall_1(num, arg_1);

	// Handle the result.
	struct mm_async_node *node = (struct mm_async_node *) arguments[0];
	mm_async_syscall_result(node, result);
}

#if 0
static void
mm_async_syscall_2_handler(struct mm_event_listener *listener UNUSED, uintptr_t *arguments)
{
	// Make the system call.
	uintptr_t num = arguments[1];
	uintptr_t arg_1 = arguments[2];
	uintptr_t arg_2 = arguments[3];
	intptr_t result = mm_syscall_2(num, arg_1, arg_2);

	// Handle the result.
	struct mm_async_node *node = (struct mm_async_node *) arguments[0];
	mm_async_syscall_result(node, result);
}
#endif

static void
mm_async_syscall_3_handler(struct mm_event_listener *listener UNUSED, uintptr_t *arguments)
{
	// Make the system call.
	uintptr_t num = arguments[1];
	uintptr_t arg_1 = arguments[2];
	uintptr_t arg_2 = arguments[3];
	uintptr_t arg_3 = arguments[4];
	intptr_t result = mm_syscall_3(num, arg_1, arg_2, arg_3);

	// Handle the result.
	struct mm_async_node *node = (struct mm_async_node *) arguments[0];
	mm_async_syscall_result(node, result);
}

#if 0
static void
mm_async_syscall_4_handler(struct mm_event_listener *listener UNUSED, uintptr_t *arguments)
{
	// Make the system call.
	uintptr_t num = arguments[1];
	uintptr_t arg_1 = arguments[2];
	uintptr_t arg_2 = arguments[3];
	uintptr_t arg_3 = arguments[4];
	uintptr_t arg_4 = arguments[5];
	intptr_t result = mm_syscall_4(num, arg_1, arg_2, arg_3, arg_4);

	// Handle the result.
	struct mm_async_node *node = (struct mm_async_node *) arguments[0];
	mm_async_syscall_result(node, result);
}
#endif

/**********************************************************************
 * Asynchronous call helpers.
 **********************************************************************/

static void
mm_async_setup(struct mm_async_node *node, const char *desc)
{
	// TODO: disable async fiber cancel

	// Initialize the debugging info.
	node->description = desc;

	// Register as a waiting fiber.
	struct mm_strand *strand = mm_strand_selfptr();
	node->fiber = strand->fiber;
	node->fiber->flags |= MM_FIBER_WAITING;
	mm_list_append(&strand->async, &node->link);

	// Initialize the result.
	node->status = MM_RESULT_DEFERRED;
	node->error = 0;
}

static intptr_t
mm_async_wait(struct mm_async_node *node)
{
	// TODO: check for shutdown and handle it gracefully while in loop.

	// Wait for the operation completion.
	while (mm_memory_load(node->status) == MM_RESULT_DEFERRED)
		mm_fiber_block();

	// Ensure the result is visible.
	mm_memory_load_fence();

	// Obtain the result.
	intptr_t result = node->result;
	if (node->error)
		errno = node->error;

	// Cleanup.
	node->fiber->flags &= ~MM_FIBER_WAITING;
	mm_list_delete(&node->link);

	return result;
}

/**********************************************************************
 * Asynchronous system call requests.
 **********************************************************************/

static intptr_t NONNULL(1)
mm_async_syscall_1(const char *name, int n, uintptr_t a1)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_event_post_3(mm_async_syscall_1_handler, (uintptr_t) &node, n, a1);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}

#if 0
static intptr_t NONNULL(1)
mm_async_syscall_2(const char *name, int n, uintptr_t a1, uintptr_t a2)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_event_post_4(mm_async_syscall_2_handler, (uintptr_t) &node, n, a1, a2);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}
#endif

static intptr_t NONNULL(1)
mm_async_syscall_3(const char *name, int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_event_post_5(mm_async_syscall_3_handler, (uintptr_t) &node, n, a1, a2, a3);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}

#if 0
static intptr_t NONNULL(1)
mm_async_syscall_4(const char *name, int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_event_post_6(mm_async_syscall_4_handler, (uintptr_t) &node, n, a1, a2, a3, a4);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}
#endif

/**********************************************************************
 * Asynchronous system call routines.
 **********************************************************************/

inline ssize_t
mm_async_read(int fd, void *buffer, size_t nbytes)
{
	return mm_async_syscall_3("read", MM_SYSCALL_N(SYS_read), fd, (uintptr_t) buffer, nbytes);
}

inline ssize_t
mm_async_readv(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_async_syscall_3("readv", MM_SYSCALL_N(SYS_readv), fd, (uintptr_t) iov, iovcnt);
}

inline ssize_t
mm_async_write(int fd, const void *buffer, size_t nbytes)
{
	return mm_async_syscall_3("write", MM_SYSCALL_N(SYS_write), fd, (uintptr_t) buffer, nbytes);
}

inline ssize_t
mm_async_writev(int fd, const struct iovec *iov, int iovcnt)
{
	return mm_async_syscall_3("writev", MM_SYSCALL_N(SYS_writev), fd, (uintptr_t) iov, iovcnt);
}

inline ssize_t
mm_async_close(int fd)
{
	return mm_async_syscall_1("close", MM_SYSCALL_N(SYS_close), fd);
}
