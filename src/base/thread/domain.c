/*
 * base/thread/domain.c - MainMemory thread domain.
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

#include "base/thread/domain.h"

#include "base/bitops.h"
#include "base/ring.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/memory/cstack.h"
#include "base/memory/memory.h"
#include "base/thread/local.h"

#include <stdio.h>

#define MM_DOMAIN_QUEUE_MIN_SIZE	16

__thread struct mm_domain *__mm_domain_self = NULL;

/**********************************************************************
 * Domain creation attributes.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_domain_attr_prepare(struct mm_domain_attr *attr)
{
	memset(attr, 0, sizeof *attr);
}

void __attribute__((nonnull(1)))
mm_domain_attr_cleanup(struct mm_domain_attr *attr)
{
	if (attr->threads_attr != NULL)
		mm_global_free(attr->threads_attr);
}

void __attribute__((nonnull(1)))
mm_domain_attr_setnumber(struct mm_domain_attr *attr, mm_thread_t number)
{
	if (attr->nthreads != number) {
		attr->nthreads = number;
		if (attr->threads_attr != NULL) {
			mm_global_free(attr->threads_attr);
			attr->threads_attr = NULL;
		}
	}
}

void __attribute__((nonnull(1)))
mm_domain_attr_setspace(struct mm_domain_attr *attr, bool enable)
{
	attr->private_space = enable;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setdomainnotify(struct mm_domain_attr *attr,
			       mm_domain_notify_t notify)
{
	attr->domain_notify = notify;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setthreadnotify(struct mm_domain_attr *attr,
			       mm_thread_notify_t notify)
{
	attr->thread_notify = notify;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setdomainqueue(struct mm_domain_attr *attr, uint32_t size)
{
	attr->domain_request_queue = size;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setthreadqueue(struct mm_domain_attr *attr, uint32_t size)
{
	attr->thread_request_queue = size;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setstacksize(struct mm_domain_attr *attr, uint32_t size)
{
	attr->stack_size = size;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setguardsize(struct mm_domain_attr *attr, uint32_t size)
{
	attr->guard_size = size;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setname(struct mm_domain_attr *attr, const char *name)
{
	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof attr->name)
			len = sizeof attr->name - 1;
		memcpy(attr->name, name, len);
	}
	attr->name[len] = 0;
}

void __attribute__((nonnull(1)))
mm_domain_attr_setcputag(struct mm_domain_attr *attr, mm_thread_t n,
			 uint32_t cpu_tag)
{
	if (unlikely(n >= attr->nthreads))
		mm_fatal(0, "invalid thread number");

	if (attr->threads_attr == NULL) {
		attr->threads_attr
			= mm_global_calloc(attr->nthreads,
					   sizeof(struct mm_domain_thread_attr));
		for (mm_thread_t i = 0; i < attr->nthreads; i++) {
			attr->threads_attr[i].cpu_tag = MM_THREAD_CPU_ANY;
		}
	}

	attr->threads_attr[n].cpu_tag = cpu_tag;
}

/**********************************************************************
 * Domain creation routines.
 **********************************************************************/

static void
mm_domain_notify_dummy(struct mm_domain *domain __mm_unused__)
{
}

struct mm_domain * __attribute__((nonnull(2)))
mm_domain_create(struct mm_domain_attr *attr, mm_routine_t start)
{
	ENTER();

	// Create a domain object.
	struct mm_domain *domain = mm_global_alloc(sizeof(struct mm_domain));

	// Set basic domain attributes.
	if (attr == NULL) {
		domain->nthreads = 1;
	} else {
		domain->nthreads = attr->nthreads;
		if (domain->nthreads == 0)
			mm_fatal(0, "invalid domain attributes.");
	}

	// Set domain notification routine.
	if (attr != NULL && attr->domain_notify != NULL)
		domain->notify = attr->domain_notify;
	else
		domain->notify = mm_domain_notify_dummy;

	// Create domain request queue if required.
	if (attr != NULL && attr->domain_request_queue) {
		uint32_t sz = mm_upper_pow2(attr->domain_request_queue);
		if (sz < MM_DOMAIN_QUEUE_MIN_SIZE)
			sz = MM_DOMAIN_QUEUE_MIN_SIZE;
		domain->request_queue = mm_ring_mpmc_create(sz);
	} else {
		domain->request_queue = NULL;
	}

	// Set the domain name.
	if (attr != NULL && attr->name[0])
		memcpy(domain->name, attr->name, MM_DOMAIN_NAME_SIZE);
	else
		strcpy(domain->name, "unnamed");

	// Set thread start/stop barrier.
	mm_barrier_init(&domain->barrier, domain->nthreads);

	// Initialize per-thread data.
	mm_thread_local_init(domain);

	// Set thread attributes.
	struct mm_thread_attr thread_attr;
	mm_thread_attr_prepare(&thread_attr);
	uint32_t stack_size = 0;
	uint32_t guard_size = 0;
	if (attr != NULL) {
		mm_thread_attr_setnotify(&thread_attr, attr->thread_notify);
		mm_thread_attr_setspace(&thread_attr, attr->private_space);
		mm_thread_attr_setrequestqueue(&thread_attr,
					       attr->thread_request_queue);

		stack_size = mm_round_up(attr->stack_size, MM_PAGE_SIZE);
		if (stack_size && stack_size < MM_THREAD_STACK_MIN)
			stack_size = MM_THREAD_STACK_MIN;
		guard_size = mm_round_up(attr->guard_size, MM_PAGE_SIZE);
	}

	// Create and start threads.
	domain->threads = mm_global_calloc(domain->nthreads,
					   sizeof(struct mm_thread *));
	for (mm_thread_t i = 0; i < domain->nthreads; i++) {
		mm_thread_attr_setdomain(&thread_attr, domain, i);
		if (attr == NULL || attr->threads_attr == NULL)
			mm_thread_attr_setcputag(&thread_attr,
						 MM_THREAD_CPU_ANY);
		else
			mm_thread_attr_setcputag(&thread_attr,
						 attr->threads_attr[i].cpu_tag);

		if (stack_size) {
			void *stack = mm_cstack_create(stack_size + guard_size,
						       guard_size);
			void *stack_base = ((char *) stack) + guard_size;
			mm_thread_attr_setstack(&thread_attr, stack_base, stack_size);
		} else if (guard_size) {
			mm_thread_attr_setguardsize(&thread_attr, guard_size);
		}

		char thread_name[MM_THREAD_NAME_SIZE];
		snprintf(thread_name, sizeof thread_name, "%s %u", domain->name, i);
		mm_thread_attr_setname(&thread_attr, thread_name);

		domain->threads[i] = mm_thread_create(&thread_attr, start, i);
	}

	LEAVE();
	return domain;
}

void __attribute__((nonnull(1)))
mm_domain_destroy(struct mm_domain *domain)
{
	ENTER();

	// Destroy domain request queue if present.
	if (domain->request_queue != NULL)
		mm_ring_mpmc_destroy(domain->request_queue);

	// Release per-thread data.
	mm_thread_local_term(domain);

	// Release thread data.
	for (mm_thread_t i = 0; i < domain->nthreads; i++) {
		struct mm_thread *thread = domain->threads[i];
		mm_thread_destroy(thread);
	}
	mm_global_free(domain->threads);

	// Release the domain;
	mm_global_free(domain);

	LEAVE();
}

/**********************************************************************
 * Domain control routines.
 **********************************************************************/

void
mm_domain_join(struct mm_domain *domain)
{
	ENTER();

	for (mm_thread_t i = 0; i < domain->nthreads; i++) {
		struct mm_thread *thread = domain->threads[i];
		mm_thread_join(thread);
	}

	LEAVE();
}
