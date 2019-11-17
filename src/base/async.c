/*
 * base/async.c - MainMemory asynchronous operations.
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

#include "base/async.h"

#include "base/list.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/syscall.h"
#include "base/event/event.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"
#include "base/fiber/strand.h"

#include <sys/syscall.h>
#include <sys/uio.h>

/**********************************************************************
 * Asynchronous procedure call execution.
 **********************************************************************/

struct mm_async_pack
{
	union
	{
		uintptr_t data[MM_ASYNC_MAX + 1];
		struct
		{
			mm_async_routine_t routine;
			uintptr_t arguments[MM_ASYNC_MAX];
		};
	};
};

static inline void
mm_async_execute(struct mm_context *context, struct mm_async_pack *pack)
{
	(*pack->routine)(context, pack->arguments);
}

static inline bool NONNULL(1, 2)
mm_async_receive(struct mm_context *context, struct mm_async_pack *pack)
{
	return mm_ring_mpsc_get_n(context->async_queue, pack->data, (MM_ASYNC_MAX + 1));
}

void NONNULL(1)
mm_async_handle_calls(struct mm_context *context)
{
	ENTER();

	// Execute requests.
	struct mm_async_pack pack;
	if (mm_async_receive(context, &pack)) {
		// Enter the state that forbids a recursive fiber switch.
		struct mm_strand *const strand = context->strand;
		const mm_strand_state_t state = strand->state;
		strand->state = MM_STRAND_CSWITCH;

		do {
			mm_async_execute(context, &pack);
#if ENABLE_EVENT_STATS
			context->stats.dequeued_async_calls++;
#endif
		} while (mm_async_receive(context, &pack));

		// Restore normal running state.
		strand->state = state;
	}

	LEAVE();
}

/**********************************************************************
 * Asynchronous procedure call construction.
 **********************************************************************/

// The size of ring data for a given number of post arguments.
#define MM_SEND_ARGC(c)		((c) + 1)
// Define ring data for a post request together with its arguments.
#define MM_SEND_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) __VA_ARGS__ }

// Send a request to a cross-thread request ring.
#define MM_SEND(n, stat, peer, ...)					\
	do {								\
		mm_stamp_t s;						\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		struct mm_ring_mpmc *ring = peer->async_queue;		\
		mm_ring_mpmc_enqueue_sn(ring, &s, v, MM_SEND_ARGC(n));	\
		mm_event_notify(peer, s);				\
		stat(mm_context_selfptr());				\
	} while (0)

// Try to send a request to a cross-thread request ring.
#define MM_TRYSEND(n, stat, peer, ...)					\
	do {								\
		bool rc;						\
		mm_stamp_t s;						\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		struct mm_ring_mpmc *ring = peer->async_queue;		\
		rc = mm_ring_mpmc_put_sn(ring, &s, v, MM_SEND_ARGC(n));	\
		if (rc) {						\
			mm_event_notify(peer, s);			\
			stat(mm_context_selfptr());			\
		}							\
		return rc;						\
	} while (0)

// Make a direct call instead of async one
#define MM_DIRECTCALL_0(r)						\
	do {								\
		struct mm_context *self = mm_context_selfptr();		\
		MM_SEND_ARGV(v, 0);					\
		r(self, v);						\
		mm_async_direct_call_stat(self);			\
	} while (0)
#define MM_DIRECTCALL(r, ...)						\
	do {								\
		struct mm_context *self = mm_context_selfptr();		\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		r(self, v);						\
		mm_async_direct_call_stat(self);			\
	} while (0)

static inline void
mm_async_call_stat(struct mm_context *context UNUSED)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	if (likely(context != NULL))
		context->stats.enqueued_async_calls++;
#endif
}

static inline void
mm_async_post_stat(struct mm_context *context UNUSED)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	if (likely(context != NULL))
		context->stats.enqueued_async_posts++;
#endif
}

static inline void
mm_async_direct_call_stat(struct mm_context *context UNUSED)
{
#if ENABLE_EVENT_STATS
	// Update statistics.
	if (likely(context != NULL))
		context->stats.direct_calls++;
#endif
}

static struct mm_context *
mm_async_find_peer(void)
{
#if ENABLE_SMP
	const mm_thread_t n = mm_number_of_regular_threads();
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *const listener =  mm_thread_ident_to_event_listener(i);
		uintptr_t state = mm_memory_load(listener->state);
		if (state != MM_EVENT_LISTENER_RUNNING)
			return listener->context;
	}
#endif
	return NULL;
}

/**********************************************************************
 * Asynchronous procedure calls targeting a single listener.
 **********************************************************************/

void NONNULL(1, 2)
mm_async_call_0(struct mm_context *const peer, mm_async_routine_t r)
{
	MM_SEND(0, mm_async_call_stat, peer, r);
}

bool NONNULL(1, 2)
mm_async_trycall_0(struct mm_context *const peer, mm_async_routine_t r)
{
	MM_TRYSEND(0, mm_async_call_stat, peer, r);
}

void NONNULL(1, 2)
mm_async_call_1(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1)
{
	MM_SEND(1, mm_async_call_stat, peer, r, a1);
}

bool NONNULL(1, 2)
mm_async_trycall_1(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1)
{
	MM_TRYSEND(1, mm_async_call_stat, peer, r, a1);
}

void NONNULL(1, 2)
mm_async_call_2(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2)
{
	MM_SEND(2, mm_async_call_stat, peer, r, a1, a2);
}

bool NONNULL(1, 2)
mm_async_trycall_2(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2)
{
	MM_TRYSEND(2, mm_async_call_stat, peer, r, a1, a2);
}

void NONNULL(1, 2)
mm_async_call_3(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_SEND(3, mm_async_call_stat, peer, r, a1, a2, a3);
}

bool NONNULL(1, 2)
mm_async_trycall_3(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYSEND(3, mm_async_call_stat, peer, r, a1, a2, a3);
}

void NONNULL(1, 2)
mm_async_call_4(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_SEND(4, mm_async_call_stat, peer, r, a1, a2, a3, a4);
}

bool NONNULL(1, 2)
mm_async_trycall_4(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYSEND(4, mm_async_call_stat, peer, r, a1, a2, a3, a4);
}

void NONNULL(1, 2)
mm_async_call_5(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_SEND(5, mm_async_call_stat, peer, r, a1, a2, a3, a4, a5);
}

bool NONNULL(1, 2)
mm_async_trycall_5(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYSEND(5, mm_async_call_stat, peer, r, a1, a2, a3, a4, a5);
}

void NONNULL(1, 2)
mm_async_call_6(struct mm_context *const peer, mm_async_routine_t r,
		uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_SEND(6, mm_async_call_stat, peer, r, a1, a2, a3, a4, a5, a6);
}

bool NONNULL(1, 2)
mm_async_trycall_6(struct mm_context *const peer, mm_async_routine_t r,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYSEND(6, mm_async_call_stat, peer, r, a1, a2, a3, a4, a5, a6);
}

/**********************************************************************
 * Asynchronous procedure calls targeting any listener of a dispatcher.
 **********************************************************************/

void NONNULL(1)
mm_async_post_0(mm_async_routine_t r)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL_0(r);
		return;
	}
	MM_SEND(0, mm_async_post_stat, peer, r);
}

void NONNULL(1)
mm_async_post_1(mm_async_routine_t r, uintptr_t a1)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1);
		return;
	}
	MM_SEND(1, mm_async_post_stat, peer, r, a1);
}

void NONNULL(1)
mm_async_post_2(mm_async_routine_t r, uintptr_t a1, uintptr_t a2)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1, a2);
		return;
	}
	MM_SEND(2, mm_async_post_stat, peer, r, a1, a2);
}

void NONNULL(1)
mm_async_post_3(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3);
		return;
	}
	MM_SEND(3, mm_async_post_stat, peer, r, a1, a2, a3);
}

void NONNULL(1)
mm_async_post_4(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4);
		return;
	}
	MM_SEND(4, mm_async_post_stat, peer, r, a1, a2, a3, a4);
}

void NONNULL(1)
mm_async_post_5(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4, a5);
		return;
	}
	MM_SEND(5, mm_async_post_stat, peer, r, a1, a2, a3, a4, a5);
}

void NONNULL(1)
mm_async_post_6(mm_async_routine_t r, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	struct mm_context *const peer = mm_async_find_peer();
	if (peer == NULL) {
		MM_DIRECTCALL(r, a1, a2, a3, a4, a5, a6);
		return;
	}
	MM_SEND(6, mm_async_post_stat, peer, r, a1, a2, a3, a4, a5, a6);
}

/**********************************************************************
 * Asynchronous system call handlers.
 **********************************************************************/

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
mm_async_syscall_1_handler(struct mm_context *context UNUSED, uintptr_t *arguments)
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
mm_async_syscall_2_handler(struct mm_context *context UNUSED, uintptr_t *arguments)
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
mm_async_syscall_3_handler(struct mm_context *context UNUSED, uintptr_t *arguments)
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
mm_async_syscall_4_handler(struct mm_context *context UNUSED, uintptr_t *arguments)
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
mm_async_setup(struct mm_async_node *node, struct mm_context *context, const char *desc)
{
	// TODO: disable async fiber cancel

	// Initialize the debugging info.
	node->description = desc;

	// Register as a waiting fiber.
	node->fiber = context->fiber;
	node->fiber->flags |= MM_FIBER_WAITING;
	mm_list_append(&context->strand->async, &node->link);

	// Initialize the result.
	node->status = MM_RESULT_DEFERRED;
	node->error = 0;
}

static intptr_t
mm_async_wait(struct mm_async_node *node, struct mm_context *context)
{
	// TODO: check for shutdown and handle it gracefully while in loop.

	// Wait for the operation completion.
	while (mm_memory_load(node->status) == MM_RESULT_DEFERRED)
		mm_fiber_block(context);

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

	// Get the execution context.
	struct mm_context *context = mm_context_selfptr();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, context, name);

	// Make an asynchronous request to execute the call.
	mm_async_post_3(mm_async_syscall_1_handler, (uintptr_t) &node, n, a1);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node, context);

	LEAVE();
	return result;
}

#if 0
static intptr_t NONNULL(1)
mm_async_syscall_2(const char *name, int n, uintptr_t a1, uintptr_t a2)
{
	ENTER();

	// Get the execution context.
	struct mm_context *context = mm_context_selfptr();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, context, name);

	// Make an asynchronous request to execute the call.
	mm_async_post_4(mm_async_syscall_2_handler, (uintptr_t) &node, n, a1, a2);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node, context);

	LEAVE();
	return result;
}
#endif

static intptr_t NONNULL(1)
mm_async_syscall_3(const char *name, int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	ENTER();

	// Get the execution context.
	struct mm_context *context = mm_context_selfptr();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, context, name);

	// Make an asynchronous request to execute the call.
	mm_async_post_5(mm_async_syscall_3_handler, (uintptr_t) &node, n, a1, a2, a3);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node, context);

	LEAVE();
	return result;
}

#if 0
static intptr_t NONNULL(1)
mm_async_syscall_4(const char *name, int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	ENTER();

	// Get the execution context.
	struct mm_context *context = mm_context_selfptr();

	// Setup the call node.
	struct mm_async_node node;
	mm_async_setup(&node, context, name);

	// Make an asynchronous request to execute the call.
	mm_async_post_6(mm_async_syscall_4_handler, (uintptr_t) &node, n, a1, a2, a3, a4);

	// Wait for its result.
	intptr_t result = mm_async_wait(&node, context);

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
