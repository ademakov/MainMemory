/*
 * base/fiber/async.c - MainMemory asynchronous operations.
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

#include "base/fiber/async.h"

#include "base/list.h"
#include "base/report.h"
#include "base/fiber/core.h"
#include "base/fiber/fiber.h"
#include "base/thread/request.h"

#include <sys/uio.h>

/* Asynchronous operation information. */
struct mm_async_node
{
	/* Link in the per-core list of async operations. */
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
	mm_core_run_fiber(node->fiber);
}

static void
mm_async_syscall_1_handler(uintptr_t *arguments)
{
	// Make the system call.
	uintptr_t num = arguments[1];
	uintptr_t arg_1 = arguments[2];
	intptr_t result = mm_syscall_1(num, arg_1);

	// Handle the result.
	struct mm_async_node *node = (struct mm_async_node *) arguments[0];
	mm_async_syscall_result(node, result);
}

static void
mm_async_syscall_2_handler(uintptr_t *arguments)
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

static void
mm_async_syscall_3_handler(uintptr_t *arguments)
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

static void
mm_async_syscall_4_handler(uintptr_t *arguments)
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
	struct mm_core *core = mm_core_selfptr();
	node->fiber = core->fiber;
	node->fiber->flags |= MM_FIBER_WAITING;
	mm_list_append(&core->async, &node->link);

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

intptr_t NONNULL(1, 2)
mm_async_syscall_1(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_domain_post_3(domain, mm_async_syscall_1_handler, (uintptr_t) &node, n, a1);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}

intptr_t NONNULL(1, 2)
mm_async_syscall_2(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_domain_post_4(domain, mm_async_syscall_2_handler, (uintptr_t) &node, n, a1, a2);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}

intptr_t NONNULL(1, 2)
mm_async_syscall_3(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_domain_post_5(domain, mm_async_syscall_3_handler, (uintptr_t) &node, n, a1, a2, a3);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}

intptr_t NONNULL(1, 2)
mm_async_syscall_4(struct mm_domain *domain, const char *name, int n,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	ENTER();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, name);

	// Make an asynchronous request to execute the call.
	mm_domain_post_6(domain, mm_async_syscall_4_handler, (uintptr_t) &node, n, a1, a2, a3, a4);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node);

	LEAVE();
	return result;
}
