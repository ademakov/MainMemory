/*
 * base/thread/domain.h - MainMemory thread domain.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#ifndef BASE_THREAD_DOMAIN_H
#define BASE_THREAD_DOMAIN_H

#include "common.h"
#include "base/list.h"
#include "base/lock.h"
#include "base/barrier.h"
#include "base/log/debug.h"
#include "base/thread/request.h"
#include "base/thread/thread.h"

/* Forward declarations. */
struct mm_ring_mpmc;

/* Maximum domain name length (including terminating zero). */
#define MM_DOMAIN_NAME_SIZE	32

/* Individual thread creation attributes for domain. */
struct mm_domain_thread_attr
{
	/* CPU affinity tag. */
	uint32_t cpu_tag;
};

/* Domain creation attributes. */
struct mm_domain_attr
{
	/* The number of threads. */
	mm_thread_t nthreads;

	/* Enable private memory space for domain's threads. */
	bool private_space;

	/* Sizes of request queue for domain and domain's threads. */
	uint32_t domain_request_queue;
	uint32_t thread_request_queue;

	/* Common notification routine for domain's threads. */
	mm_thread_notify_t notify;

	/* Common stack parameters for domain's threads. */
	uint32_t stack_size;
	uint32_t guard_size;

	/* Individual thread creation attributes. */
	struct mm_domain_thread_attr *threads_attr;

	/* The domain name. */
	char name[MM_DOMAIN_NAME_SIZE];
};

/* Domain run-time data. */
struct mm_domain
{
	/* Domain threads. */
	mm_thread_t nthreads;
	struct mm_thread **threads;

	/* Domain request queue. */
	struct mm_ring_mpmc *request_queue;

	/* Per-thread data. */
	struct mm_queue per_thread_chunk_list;
	struct mm_queue per_thread_entry_list;
	mm_lock_t per_thread_lock;

	/* Thread start/stop barrier. */
	struct mm_barrier barrier;

	/* Domain name. */
	char name[MM_DOMAIN_NAME_SIZE];
};

extern __thread struct mm_domain *__mm_domain_self;

/**********************************************************************
 * Domain creation routines.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_domain_attr_prepare(struct mm_domain_attr *attr);

void __attribute__((nonnull(1)))
mm_domain_attr_cleanup(struct mm_domain_attr *attr);

void __attribute__((nonnull(1)))
mm_domain_attr_setnumber(struct mm_domain_attr *attr, mm_thread_t number);

void __attribute__((nonnull(1)))
mm_domain_attr_setnotify(struct mm_domain_attr *attr, mm_thread_notify_t notify);

void __attribute__((nonnull(1)))
mm_domain_attr_setspace(struct mm_domain_attr *attr, bool enable);

void __attribute__((nonnull(1)))
mm_domain_attr_setdomainqueue(struct mm_domain_attr *attr, uint32_t size);

void __attribute__((nonnull(1)))
mm_domain_attr_setthreadqueue(struct mm_domain_attr *attr, uint32_t size);

void __attribute__((nonnull(1)))
mm_domain_attr_setstacksize(struct mm_domain_attr *attr, uint32_t size);

void __attribute__((nonnull(1)))
mm_domain_attr_setguardsize(struct mm_domain_attr *attr, uint32_t size);

void __attribute__((nonnull(1)))
mm_domain_attr_setname(struct mm_domain_attr *attr, const char *name);

void __attribute__((nonnull(1)))
mm_domain_attr_setcputag(struct mm_domain_attr *attr, mm_thread_t n,
			 uint32_t cpu_tag);

struct mm_domain * __attribute__((nonnull(2)))
mm_domain_create(struct mm_domain_attr *attr, mm_routine_t start);

void __attribute__((nonnull(1)))
mm_domain_destroy(struct mm_domain *domain);

/**********************************************************************
 * Domain information.
 **********************************************************************/

static inline struct mm_domain *
mm_domain_self(void)
{
	return __mm_domain_self;
}

static inline mm_thread_t
mm_domain_getnumber(const struct mm_domain *domain)
{
	return domain->nthreads;
}

static inline struct mm_thread *
mm_domain_getthread(struct mm_domain *domain, mm_thread_t n)
{
	ASSERT(n < domain->nthreads);
	return domain->threads[n];
}

/**********************************************************************
 * Domain control routines.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_domain_join(struct mm_domain *domain);

/**********************************************************************
 * Domain requests.
 **********************************************************************/

MM_REQUEST_RECEIVE_WRAPPER(mm_domain, struct mm_domain, request_queue)

MM_REQUEST_SUBMIT_WRAPPERS(mm_domain, struct mm_domain, request_queue)

MM_REQUEST_SYSCALL_WRAPPERS(mm_domain, struct mm_domain, request_queue)

#endif /* BASE_THREAD_DOMAIN_H */
