/*
 * core/async.c - MainMemory asynchronous operations.
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

#include "core/async.h"
#include "core/core.h"
#include "core/task.h"
#include "core/value.h"

#include "arch/memory.h"
#include "base/list.h"
#include "base/log/trace.h"
#include "base/thread/domain.h"
#include "base/thread/request.h"

#include <unistd.h>
#include <sys/syscall.h>

/* Asynchronous operation information. */
struct mm_async_node
{
	/* The task that requested the operation. */
	struct mm_task *task;

	/* Link in the per-core list of async operations. */
	struct mm_link link;

	/* Operation status. */
	mm_value_t status;

	/* Operation result. */
	mm_value_t result;

	/* Threading subsystem hook. */
	struct mm_requestor requestor;

	/* Human readable information for debugging. */
	const char *description;
};

static void
mm_async_response(uintptr_t context __mm_unused__,
		  struct mm_requestor *rtor, uintptr_t result)
{
	struct mm_async_node *node = containerof(rtor, struct mm_async_node, requestor);

	// Store the result.
	node->result = result;
	// Ensure its visibility.
	mm_memory_store_fence();
	// Indicate the operation completion.
	node->status = 0;

	// Wake up the requestor task.
	mm_core_run_task(node->task);
}

static void
mm_async_setup(struct mm_async_node *node, const char *description)
{
	// TODO: disable async task cancel

	struct mm_core *core = mm_core_selfptr();
	node->task = core->task;
	node->task->flags |= MM_TASK_WAITING;
	mm_list_append(&core->async, &node->link);

	node->status = MM_RESULT_DEFERRED;
	node->requestor.response = mm_async_response;
	node->description = description;
}

static void
mm_async_wait(struct mm_async_node *node)
{
	// TODO: check for shutdown and handle it gracefully while in loop.

	// Wait for the operation completion.
	while (node->status == MM_RESULT_DEFERRED) {
		mm_task_block();
		mm_compiler_barrier();
	}

	// Ensure result is visible.
	mm_memory_load_fence();

	// Cleanup.
	node->task->flags &= ~MM_TASK_WAITING;
	mm_list_delete(&node->link);
}

ssize_t
mm_async_read(int fd, void *buffer, size_t nbytes)
{
	ENTER();

	// Setup the async node.
	struct mm_async_node node;
	mm_async_setup(&node, __FUNCTION__);

	// Make the async call.
	struct mm_domain *domain = mm_domain_selfptr();
	mm_domain_syscall_3(domain, &node.requestor, SYS_read,
			    fd, (uintptr_t) buffer, nbytes);

	// Wait for completion.
	mm_async_wait(&node);

	LEAVE();
	return node.result;
}

ssize_t
mm_async_readv(int fd, const struct iovec *iov, int iovcnt)
{
	ENTER();

	// Setup the async node.
	struct mm_async_node node;
	mm_async_setup(&node, __FUNCTION__);

	// Make the async call.
	struct mm_domain *domain = mm_domain_selfptr();
	mm_domain_syscall_3(domain, &node.requestor, SYS_writev,
			    fd, (uintptr_t) iov, iovcnt);

	// Wait for completion.
	mm_async_wait(&node);

	LEAVE();
	return node.result;
}

ssize_t
mm_async_write(int fd, const void *buffer, size_t nbytes)
{
	ENTER();

	// Setup the async node.
	struct mm_async_node node;
	mm_async_setup(&node, __FUNCTION__);

	// Make the async call.
	struct mm_domain *domain = mm_domain_selfptr();
	mm_domain_syscall_3(domain, &node.requestor, SYS_write,
			    fd, (uintptr_t) buffer, nbytes);

	// Wait for completion.
	mm_async_wait(&node);

	LEAVE();
	return node.result;
}

ssize_t
mm_async_writev(int fd, const struct iovec *iov, int iovcnt)
{
	ENTER();

	// Setup the async node.
	struct mm_async_node node;
	mm_async_setup(&node, __FUNCTION__);

	// Make the async call.
	struct mm_domain *domain = mm_domain_selfptr();
	mm_domain_syscall_3(domain, &node.requestor, SYS_writev,
			    fd, (uintptr_t) iov, iovcnt);

	// Wait for completion.
	mm_async_wait(&node);

	LEAVE();
	return node.result;
}
