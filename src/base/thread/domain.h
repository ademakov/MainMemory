/*
 * base/thread/domain.h - MainMemory thread domain.
 *
 * Copyright (C) 2014-2017  Aleksey Demakov
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
#include "base/report.h"
#include "base/thread/barrier.h"
#include "base/thread/local.h"
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

	/* Associated event dispatcher. */
	struct mm_event_dispatch *event_dispatch;

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
	/* The domain identity. Must be unique among domains. */
	mm_thread_t domain_ident;
	/* The thread identity base value. The threads in the domain are
	   created with identity values ranging from thread_ident_base to
	   thread_ident_base + nthreads. The range must not contain other
	   thread identities. */
	mm_thread_t thread_ident_base;

	/* Domain threads. */
	mm_thread_t nthreads;
	struct mm_thread **threads;

	/* Domain request queue. */
	struct mm_ring_mpmc *request_queue;

	/* Associated event dispatcher. */
	struct mm_event_dispatch *event_dispatch;

	/* Per-thread data. */
	struct mm_queue per_thread_chunk_list;
	struct mm_queue per_thread_entry_list;
	mm_lock_t per_thread_lock;

	/* Thread start/stop barrier. */
	struct mm_thread_barrier barrier;
	MM_THREAD_LOCAL(struct mm_thread_barrier_local, barrier_local);

	/* Domain name. */
	char name[MM_DOMAIN_NAME_SIZE];
};

/**********************************************************************
 * Domain creation routines.
 **********************************************************************/

void NONNULL(1)
mm_domain_attr_prepare(struct mm_domain_attr *attr);

void NONNULL(1)
mm_domain_attr_cleanup(struct mm_domain_attr *attr);

void NONNULL(1)
mm_domain_attr_setsize(struct mm_domain_attr *attr, mm_thread_t size);

void NONNULL(1)
mm_domain_attr_setspace(struct mm_domain_attr *attr, bool enable);

void NONNULL(1)
mm_domain_attr_setdomainqueue(struct mm_domain_attr *attr, uint32_t size);

void NONNULL(1)
mm_domain_attr_setthreadqueue(struct mm_domain_attr *attr, uint32_t size);

void NONNULL(1)
mm_domain_attr_setdispatch(struct mm_domain_attr *attr, struct mm_event_dispatch *dispatch);

void NONNULL(1)
mm_domain_attr_setstacksize(struct mm_domain_attr *attr, uint32_t size);

void NONNULL(1)
mm_domain_attr_setguardsize(struct mm_domain_attr *attr, uint32_t size);

void NONNULL(1)
mm_domain_attr_setname(struct mm_domain_attr *attr, const char *name);

void NONNULL(1)
mm_domain_attr_setcputag(struct mm_domain_attr *attr, mm_thread_t n,
			 uint32_t cpu_tag);

struct mm_domain * NONNULL(2)
mm_domain_create(struct mm_domain_attr *attr, mm_routine_t start);

void NONNULL(1)
mm_domain_destroy(struct mm_domain *domain);

/**********************************************************************
 * Domain information.
 **********************************************************************/

extern __thread struct mm_domain *__mm_domain_self;

static inline struct mm_domain *
mm_domain_selfptr(void)
{
	return __mm_domain_self;
}

static inline mm_thread_t NONNULL(1)
mm_domain_ident(const struct mm_domain *domain)
{
	return domain->domain_ident;
}

static inline mm_thread_t NONNULL(1)
mm_domain_first_thread_ident(const struct mm_domain *domain)
{
	return domain->thread_ident_base;
}

static inline mm_thread_t NONNULL(1)
mm_domain_getsize(const struct mm_domain *domain)
{
	return domain->nthreads;
}

static inline struct mm_thread * NONNULL(1)
mm_domain_getthread(struct mm_domain *domain, mm_thread_t n)
{
	ASSERT(n < domain->nthreads);
	return domain->threads[n];
}

static inline struct mm_event_dispatch * NONNULL(1)
mm_domain_getdispatch(struct mm_domain *domain)
{
	return domain->event_dispatch;
}

/**********************************************************************
 * Domain control routines.
 **********************************************************************/

void NONNULL(1)
mm_domain_join(struct mm_domain *domain);

void
mm_domain_barrier(void);

/**********************************************************************
 * Domain requests.
 **********************************************************************/

static inline void NONNULL(1)
mm_domain_notify(struct mm_domain *domain, mm_stamp_t stamp UNUSED)
{
	mm_event_notify_any(mm_domain_getdispatch(domain));
}

static inline bool NONNULL(1, 2)
mm_domain_receive(struct mm_domain *domain, struct mm_request_data *rdata)
{
	return mm_request_receive(domain->request_queue, rdata);
}

static inline void NONNULL(1, 2)
mm_domain_post_0(struct mm_domain *domain, mm_post_routine_t req)
{
	MM_POST(0, domain->request_queue, mm_domain_notify, domain, req);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_0(struct mm_domain *domain, mm_post_routine_t req)
{
	MM_TRYPOST(0, domain->request_queue, mm_domain_notify, domain, req);
}

static inline void NONNULL(1, 2)
mm_domain_post_1(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1)
{
	MM_POST(1, domain->request_queue, mm_domain_notify, domain, req, a1);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_1(struct mm_domain *domain, mm_post_routine_t req,
		    uintptr_t a1)
{
	MM_TRYPOST(1, domain->request_queue, mm_domain_notify, domain, req, a1);
}

static inline void NONNULL(1, 2)
mm_domain_post_2(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2)
{
	MM_POST(2, domain->request_queue, mm_domain_notify, domain, req, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_2(struct mm_domain *domain, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2)
{
	MM_TRYPOST(2, domain->request_queue, mm_domain_notify, domain, req, a1, a2);
}

static inline void NONNULL(1, 2)
mm_domain_post_3(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_POST(3, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_3(struct mm_domain *domain, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYPOST(3, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_domain_post_4(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_POST(4, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_4(struct mm_domain *domain, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYPOST(4, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_domain_post_5(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_POST(5, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_5(struct mm_domain *domain, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYPOST(5, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4, a5);
}

static inline void NONNULL(1, 2)
mm_domain_post_6(struct mm_domain *domain, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_POST(6, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4, a5, a6);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_6(struct mm_domain *domain, mm_post_routine_t req,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYPOST(6, domain->request_queue, mm_domain_notify, domain, req, a1, a2, a3, a4, a5, a6);
}

static inline void NONNULL(1, 2)
mm_domain_send_0(struct mm_domain *domain, struct mm_request_sender *sender)
{
	MM_SEND(0, domain->request_queue, mm_domain_notify, domain, sender);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_0(struct mm_domain *domain, struct mm_request_sender *sender)
{
	MM_TRYSEND(0, domain->request_queue, mm_domain_notify, domain, sender);
}

static inline void NONNULL(1, 2)
mm_domain_send_1(struct mm_domain *domain, struct mm_request_sender *sender,
		 uintptr_t a1)
{
	MM_SEND(1, domain->request_queue, mm_domain_notify, domain, sender, a1);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_1(struct mm_domain *domain, struct mm_request_sender *sender,
		    uintptr_t a1)
{
	MM_TRYSEND(1, domain->request_queue, mm_domain_notify, domain, sender, a1);
}

static inline void NONNULL(1, 2)
mm_domain_send_2(struct mm_domain *domain, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2)
{
	MM_SEND(2, domain->request_queue, mm_domain_notify, domain, sender, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_2(struct mm_domain *domain, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2)
{
	MM_TRYSEND(2, domain->request_queue, mm_domain_notify, domain, sender, a1, a2);
}

static inline void NONNULL(1, 2)
mm_domain_send_3(struct mm_domain *domain, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_SEND(3, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_3(struct mm_domain *domain, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYSEND(3, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_domain_send_4(struct mm_domain *domain, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_SEND(4, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_4(struct mm_domain *domain, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYSEND(4, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_domain_send_5(struct mm_domain *domain, struct mm_request_sender *sender,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_SEND(5, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_domain_trysend_5(struct mm_domain *domain, struct mm_request_sender *sender,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYSEND(5, domain->request_queue, mm_domain_notify, domain, sender, a1, a2, a3, a4, a5);
}

#endif /* BASE_THREAD_DOMAIN_H */
