/*
 * base/thread/thread.h - MainMemory threads.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#ifndef BASE_THREAD_THREAD_H
#define BASE_THREAD_THREAD_H

#include "common.h"
#include "base/list.h"
#include "base/report.h"
#include "base/event/event.h"
#include "base/memory/space.h"
#include "base/thread/barrier.h"
#include "base/thread/request.h"

#include <pthread.h>

/* Forward declarations. */
struct mm_domain;
struct mm_thread;
struct mm_trace_context;

#define MM_THREAD_CPU_ANY	((uint32_t) -1)

/* Minimal thread stack size. */
#if defined(PTHREAD_STACK_MIN)
# define MM_THREAD_STACK_MIN	(PTHREAD_STACK_MIN)
#else
# define MM_THREAD_STACK_MIN	(2 * MM_PAGE_SIZE)
#endif

/* Maximum thread name length (including terminating zero). */
#define MM_THREAD_NAME_SIZE	40

/* Thread synchronization backoff routine. */
typedef void (*mm_thread_relax_t)(void);

/* Thread creation attributes. */
struct mm_thread_attr
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_thread_t domain_index;

	/* Enable private memory space. */
	bool private_space;

	/* The size of thread request queue. */
	uint32_t request_queue;

	/* The size of queue for memory chunks released by other threads. */
	uint32_t reclaim_queue;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

	/* The thread stack. */
	uint32_t stack_size;
	uint32_t guard_size;
	void *stack_base;

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];
};

/* Thread run-time data. */
struct mm_thread
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_thread_t domain_index;

	/* The thread identity. Must be unique among threads. */
	mm_thread_t thread_ident;

	/* Thread request queue. */
	struct mm_ring_mpmc *request_queue;

	/* Associated event listener. */
	struct mm_event_listener *event_listener;

#if ENABLE_SMP
	/* Private memory space. */
	struct mm_private_space space;
#endif

	/* Memory chunks from other threads with deferred destruction. */
	struct mm_stack deferred_chunks;
	size_t deferred_chunks_count;

	/* The log message storage. */
	struct mm_queue log_queue;

	/* Underlying system thread. */
	pthread_t system_thread;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

	/* The thread start routine and its argument. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];

#if ENABLE_TRACE
	/* Thread trace context. */
	struct mm_trace_context trace;
#endif
};

/**********************************************************************
 * Thread subsystem initialization and termination.
 **********************************************************************/

void mm_thread_init();

/**********************************************************************
 * Thread creation routines.
 **********************************************************************/

void NONNULL(1)
mm_thread_attr_prepare(struct mm_thread_attr *attr);

void NONNULL(1, 2)
mm_thread_attr_setdomain(struct mm_thread_attr *attr, struct mm_domain *domain, mm_thread_t index);

void NONNULL(1)
mm_thread_attr_setspace(struct mm_thread_attr *attr, bool enable);

void NONNULL(1)
mm_thread_attr_setrequestqueue(struct mm_thread_attr *attr, uint32_t size);

void NONNULL(1)
mm_thread_attr_setreclaimqueue(struct mm_thread_attr *attr, uint32_t size);

void NONNULL(1)
mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag);

void NONNULL(1)
mm_thread_attr_setstacksize(struct mm_thread_attr *attr, uint32_t size);

void NONNULL(1)
mm_thread_attr_setguardsize(struct mm_thread_attr *attr, uint32_t size);

void NONNULL(1)
mm_thread_attr_setstack(struct mm_thread_attr *attr, void *base, uint32_t size);

void NONNULL(1)
mm_thread_attr_setname(struct mm_thread_attr *attr, const char *name);

struct mm_thread * NONNULL(2)
mm_thread_create(struct mm_thread_attr *attr, mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_thread_destroy(struct mm_thread *thread);

/**********************************************************************
 * Thread information.
 **********************************************************************/

extern __thread struct mm_thread *__mm_thread_self;

static inline struct mm_thread *
mm_thread_selfptr(void)
{
	return __mm_thread_self;
}

static inline mm_thread_t NONNULL(1)
mm_thread_ident(const struct mm_thread *thread)
{
	return thread->thread_ident;
}

static inline const char * NONNULL(1)
mm_thread_getname(const struct mm_thread *thread)
{
	return thread->name;
}

static inline struct mm_domain * NONNULL(1)
mm_thread_getdomain(const struct mm_thread *thread)
{
	return thread->domain;
}

static inline mm_thread_t NONNULL(1)
mm_thread_getnumber(const struct mm_thread *thread)
{
	return thread->domain_index;
}

static inline struct mm_event_listener *
mm_thread_getlistener(struct mm_thread *thread)
{
	return thread->event_listener;
}

static inline mm_thread_t
mm_thread_self(void)
{
	return mm_thread_getnumber(mm_thread_selfptr());
}

#if ENABLE_SMP
static inline struct mm_private_space * NONNULL(1)
mm_thread_getspace(struct mm_thread *thread)
{
	return &thread->space;
}
#endif

static inline struct mm_queue * NONNULL(1)
mm_thread_getlog(struct mm_thread *thread)
{
	return &thread->log_queue;
}

#if ENABLE_TRACE
static inline struct mm_trace_context * NONNULL(1)
mm_thread_gettracecontext(struct mm_thread *thread)
{
	return &thread->trace;
}
#endif

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

void NONNULL(1)
mm_thread_cancel(struct mm_thread *thread);

void NONNULL(1)
mm_thread_wakeup(struct mm_thread *thread);

void NONNULL(1)
mm_thread_join(struct mm_thread *thread);

void
mm_thread_yield(void);

/**********************************************************************
 * Thread requests.
 **********************************************************************/

#define MM_THREAD_POST(n, t, ...)					\
	do {								\
		mm_stamp_t stamp;					\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		mm_ring_mpmc_enqueue_sn(t->request_queue, &stamp, v,	\
					MM_POST_ARGC(n));		\
		mm_thread_notify(t, stamp);				\
	} while (0)

#define MM_THREAD_TRYPOST(n, t, ...)					\
	do {								\
		bool res;						\
		mm_stamp_t stamp;					\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		res = mm_ring_mpmc_put_sn(t->request_queue, &stamp, v,	\
					  MM_POST_ARGC(n));		\
		if (res)						\
			mm_thread_notify(t, stamp);			\
		return res;						\
	} while (0)

#define MM_THREAD_SEND(n, t, ...)					\
	do {								\
		mm_stamp_t stamp;					\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		mm_ring_mpmc_enqueue_sn(t->request_queue, &stamp, v,	\
					MM_SEND_ARGC(n));		\
		mm_thread_notify(t, stamp);				\
	} while (0)

#define MM_THREAD_TRYSEND(n, t, ...)					\
	do {								\
		bool res;						\
		mm_stamp_t stamp;					\
		MM_SEND_ARGV(v, __VA_ARGS__);				\
		res = mm_ring_mpmc_put_sn(t->request_queue, &stamp, v,	\
					  MM_SEND_ARGC(n));		\
		if (res)						\
			mm_thread_notify(t, stamp);			\
		return res;						\
	} while (0)

static inline void NONNULL(1)
mm_thread_notify(struct mm_thread *thread, mm_stamp_t stamp)
{
	mm_event_notify(mm_thread_getlistener(thread), stamp);
}

static inline bool NONNULL(1, 2)
mm_thread_receive(struct mm_thread *thread, struct mm_request_data *rdata)
{
	return mm_request_relaxed_receive(thread->request_queue, rdata);
}

static inline void NONNULL(1, 2)
mm_thread_post_0(struct mm_thread *thread, mm_post_routine_t req)
{
	MM_THREAD_POST(0, thread, req);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_0(struct mm_thread *thread, mm_post_routine_t req)
{
	MM_THREAD_TRYPOST(0, thread, req);
}

static inline void NONNULL(1, 2)
mm_thread_post_1(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1)
{
	MM_THREAD_POST(1, thread, req, a1);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_1(struct mm_thread *thread, mm_post_routine_t req,
		    uintptr_t a1)
{
	MM_THREAD_TRYPOST(1, thread, req, a1);
}

static inline void NONNULL(1, 2)
mm_thread_post_2(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2)
{
	MM_THREAD_POST(2, thread, req, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_2(struct mm_thread *thread, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2)
{
	MM_THREAD_TRYPOST(2, thread, req, a1, a2);
}

static inline void NONNULL(1, 2)
mm_thread_post_3(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_THREAD_POST(3, thread, req, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_3(struct mm_thread *thread, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_THREAD_TRYPOST(3, thread, req, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_thread_post_4(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_THREAD_POST(4, thread, req, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_4(struct mm_thread *thread, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_THREAD_TRYPOST(4, thread, req, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_thread_post_5(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_THREAD_POST(5, thread, req, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_5(struct mm_thread *thread, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_THREAD_TRYPOST(5, thread, req, a1, a2, a3, a4, a5);
}

static inline void NONNULL(1, 2)
mm_thread_post_6(struct mm_thread *thread, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_THREAD_POST(6, thread, req, a1, a2, a3, a4, a5, a6);
}

static inline bool NONNULL(1, 2)
mm_thread_trypost_6(struct mm_thread *thread, mm_post_routine_t req,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_THREAD_TRYPOST(6, thread, req, a1, a2, a3, a4, a5, a6);
}

static inline void NONNULL(1, 2)
mm_thread_send_0(struct mm_thread *thread, struct mm_request_sender *sender)
{
	MM_THREAD_SEND(0, thread, sender);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_0(struct mm_thread *thread, struct mm_request_sender *sender)
{
	MM_THREAD_TRYSEND(0, thread, sender);
}

static inline void NONNULL(1, 2)
mm_thread_send_1(struct mm_thread *thread, struct mm_request_sender *sender,
		 uintptr_t a1)
{
	MM_THREAD_SEND(1, thread, sender, a1);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_1(struct mm_thread *thread, struct mm_request_sender *sender,
		    uintptr_t a1)
{
	MM_THREAD_TRYSEND(1, thread, sender, a1);
}

static inline void NONNULL(1, 2)
mm_thread_send_2(struct mm_thread *thread, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2)
{
	MM_THREAD_SEND(2, thread, sender, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_2(struct mm_thread *thread, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2)
{
	MM_THREAD_TRYSEND(2, thread, sender, a1, a2);
}

static inline void NONNULL(1, 2)
mm_thread_send_3(struct mm_thread *thread, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_THREAD_SEND(3, thread, sender, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_3(struct mm_thread *thread, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_THREAD_TRYSEND(3, thread, sender, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_thread_send_4(struct mm_thread *thread, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_THREAD_SEND(4, thread, sender, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_4(struct mm_thread *thread, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_THREAD_TRYSEND(4, thread, sender, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_thread_send_5(struct mm_thread *thread, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_THREAD_SEND(5, thread, sender, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_thread_trysend_5(struct mm_thread *thread, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_THREAD_TRYSEND(5, thread, sender, a1, a2, a3, a4, a5);
}

#endif /* BASE_THREAD_THREAD_H */
