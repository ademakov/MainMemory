/*
 * base/thread/thread.h - MainMemory threads.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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
#include "base/barrier.h"
#include "base/list.h"
#include "base/mem/space.h"
#include <pthread.h>

/* Forward declarations. */
struct mm_domain;
struct mm_trace_context;

/* Maximum thread name length (including terminating zero). */
#define MM_THREAD_NAME_SIZE	40

/* Thread creation attributes. */
struct mm_thread_attr
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_thread_t domain_number;

	/* Enable private memory space. */
	bool private_space;

	/* The size of queue for memory chunks released by other threads. */
	uint32_t reclaim_queue;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

	/* The thread stack. */
	uint32_t stack_size;
	void *stack_base;

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];
};

/* Thread data. */
struct mm_thread
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_thread_t domain_number;

#if ENABLE_SMP
	/* Private memory space. */
	struct mm_private_space space;
#endif

	/* Memory chunks from other threads with deferred destruction. */
	struct mm_link deferred_chunks;
	size_t deferred_chunks_count;

	/* The log message storage. */
	struct mm_queue log_queue;

	/* Underlying system thread. */
	pthread_t system_thread;

	/* Domain threads start/stop synchronization. */
	struct mm_barrier_local domain_barrier;

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

void __attribute__((nonnull(1)))
mm_thread_attr_init(struct mm_thread_attr *attr);

void __attribute__((nonnull(1, 2)))
mm_thread_attr_setdomain(struct mm_thread_attr *attr,
			 struct mm_domain *domain,
			 mm_thread_t number);

void __attribute__((nonnull(1)))
mm_thread_attr_setspace(struct mm_thread_attr *attr, bool private_space);

void __attribute__((nonnull(1)))
mm_thread_attr_setreclaimqueue(struct mm_thread_attr *attr, uint32_t size);

void __attribute__((nonnull(1)))
mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag);

void __attribute__((nonnull(1)))
mm_thread_attr_setstack(struct mm_thread_attr *attr,
			void *stack_base, uint32_t stack_size);

void __attribute__((nonnull(1)))
mm_thread_attr_setname(struct mm_thread_attr *attr, const char *name);

struct mm_thread * __attribute__((nonnull(2)))
mm_thread_create(struct mm_thread_attr *attr,
		 mm_routine_t start, mm_value_t start_arg);

void __attribute__((nonnull(1)))
mm_thread_release_chunks(struct mm_thread *thread);

void __attribute__((nonnull(1)))
mm_thread_destroy(struct mm_thread *thread);

/**********************************************************************
 * Thread information.
 **********************************************************************/

extern __thread struct mm_thread *__mm_thread_self;

static inline struct mm_thread *
mm_thread_self(void)
{
	return __mm_thread_self;
}

static inline const char * __attribute__((nonnull(1)))
mm_thread_getname(const struct mm_thread *thread)
{
	return thread->name;
}

static inline struct mm_domain * __attribute__((nonnull(1)))
mm_thread_getdomain(const struct mm_thread *thread)
{
	return thread->domain;
}

static inline mm_thread_t __attribute__((nonnull(1)))
mm_thread_getnumber(const struct mm_thread *thread)
{
	return thread->domain_number;
}

#if ENABLE_SMP
static inline struct mm_private_space * __attribute__((nonnull(1)))
mm_thread_getspace(struct mm_thread *thread)
{
	return &thread->space;
}
#endif

static inline struct mm_queue * __attribute__((nonnull(1)))
mm_thread_getlog(struct mm_thread *thread)
{
	return &thread->log_queue;
}

#if ENABLE_TRACE
static inline struct mm_trace_context * __attribute__((nonnull(1)))
mm_thread_gettracecontext(struct mm_thread *thread)
{
	return &thread->trace;
}
#endif

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_thread_cancel(struct mm_thread *thread);

void __attribute__((nonnull(1)))
mm_thread_join(struct mm_thread *thread);

void mm_thread_yield(void);

void mm_thread_domain_barrier(void);

#endif /* BASE_THREAD_THREAD_H */
