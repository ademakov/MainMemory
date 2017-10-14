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

#include <pthread.h>

/* Forward declarations. */
struct mm_domain;
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

/* Thread creation attributes. */
struct mm_thread_attr
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_thread_t domain_index;

	/* Enable private memory space. */
	bool private_space;

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
mm_thread_join(struct mm_thread *thread);

void
mm_thread_yield(void);

#endif /* BASE_THREAD_THREAD_H */
