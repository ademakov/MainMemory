/*
 * base/thr/thread.h - MainMemory threads.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef BASE_THR_THREAD_H
#define BASE_THR_THREAD_H

#include "common.h"

/* Forward declarations. */
struct mm_domain;
struct mm_thread;
#if ENABLE_TRACE
struct mm_trace_context;
#endif

/* Maximum thread name length (including terminating zero). */
#define MM_THREAD_NAME_SIZE	40

/* Thread creation attributes. */
struct mm_thread_attr
{
	/* Thread domain. */
	struct mm_domain *domain;
	mm_core_t domain_index;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

	/* The thread stack. */
	uint32_t stack_size;
	void *stack_base;

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];
};

/**********************************************************************
 * Thread subsystem initialization and termination.
 **********************************************************************/

void mm_thread_init();

/**********************************************************************
 * Thread creation routines.
 **********************************************************************/

void mm_thread_attr_init(struct mm_thread_attr *attr)
	__attribute__((nonnull(1)));

void mm_thread_attr_setdomain(struct mm_thread_attr *attr,
			      struct mm_domain *domain,
			      mm_core_t domain_index)
	__attribute__((nonnull(1)));

void mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag)
	__attribute__((nonnull(1)));

void mm_thread_attr_setstack(struct mm_thread_attr *attr,
			     void *stack_base, uint32_t stack_size)
	__attribute__((nonnull(1)));

void mm_thread_attr_setname(struct mm_thread_attr *attr, const char *name)
	__attribute__((nonnull(1)));

struct mm_thread * mm_thread_create(struct mm_thread_attr *attr,
				    mm_routine_t start, mm_value_t start_arg)
	__attribute__((nonnull(2)));

void mm_thread_destroy(struct mm_thread *thread)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Thread information.
 **********************************************************************/

extern __thread struct mm_thread *__mm_thread_self;

static inline struct mm_thread *
mm_thread_self(void)
{
	return __mm_thread_self;
}

const char * mm_thread_getname(const struct mm_thread *thread)
	__attribute__((nonnull(1)));

struct mm_domain * mm_thread_getdomain(const struct mm_thread *thread)
	__attribute__((nonnull(1)));

mm_core_t mm_thread_getdomainindex(const struct mm_thread *thread)
	__attribute__((nonnull(1)));

struct mm_queue * mm_thread_getlog(struct mm_thread *thread)
	__attribute__((nonnull(1)));

#if ENABLE_TRACE
struct mm_trace_context * mm_thread_gettracecontext(struct mm_thread *thread)
	__attribute__((nonnull(1)));
#endif

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

void mm_thread_cancel(struct mm_thread *thread)
	__attribute__((nonnull(1)));

void mm_thread_join(struct mm_thread *thread)
	__attribute__((nonnull(1)));

void mm_thread_yield(void);

#endif /* BASE_THR_THREAD_H */
