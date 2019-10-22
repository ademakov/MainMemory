/*
 * base/thread/domain.c - MainMemory thread domain.
 *
 * Copyright (C) 2014-2019  Aleksey Demakov
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
#include "base/cstack.h"
#include "base/logger.h"
#include "base/memory/global.h"
#include "base/thread/ident.h"
#include "base/thread/local.h"

#include <stdio.h>

/* Individual thread creation attributes for domain. */
struct mm_domain_thread_attr
{
	/* The argument of the thread routine. */
	mm_value_t arg;
	/* CPU affinity tag. */
	uint32_t cpu_tag;
};

__thread struct mm_domain *__mm_domain_self = NULL;


/**********************************************************************
 * Thread creation attributes.
 **********************************************************************/

static void
mm_domain_attr_ensure_threads_attr(struct mm_domain_attr *attr, mm_thread_t n)
{
	if (unlikely(attr->nthreads == 0))
		mm_fatal(0, "the number of threads is not set");
	if (unlikely(n >= attr->nthreads))
		mm_fatal(0, "invalid thread number: %d (max is %d)", n, attr->nthreads - 1);

	if (attr->threads_attr == NULL) {
		attr->threads_attr = mm_global_calloc(attr->nthreads, sizeof(struct mm_domain_thread_attr));
		for (mm_thread_t i = 0; i < attr->nthreads; i++) {
			attr->threads_attr[i].arg = 0;
			attr->threads_attr[i].cpu_tag = MM_THREAD_CPU_ANY;
		}
	}
}

/**********************************************************************
 * Domain creation attributes.
 **********************************************************************/

void NONNULL(1)
mm_domain_attr_prepare(struct mm_domain_attr *attr)
{
	memset(attr, 0, sizeof *attr);
}

void NONNULL(1)
mm_domain_attr_cleanup(struct mm_domain_attr *attr)
{
	if (attr->threads_attr != NULL)
		mm_global_free(attr->threads_attr);
}

void NONNULL(1)
mm_domain_attr_setsize(struct mm_domain_attr *attr, mm_thread_t size)
{
	VERIFY(size > 0);

	attr->nthreads = size;
	if (attr->threads_attr != NULL) {
		mm_global_free(attr->threads_attr);
		attr->threads_attr = NULL;
	}
}

void NONNULL(1)
mm_domain_attr_setarg(struct mm_domain_attr *attr, mm_thread_t n, mm_value_t arg)
{
	mm_domain_attr_ensure_threads_attr(attr, n);
	attr->threads_attr[n].arg = arg;
}

void NONNULL(1)
mm_domain_attr_setcputag(struct mm_domain_attr *attr, mm_thread_t n, uint32_t cpu_tag)
{
	mm_domain_attr_ensure_threads_attr(attr, n);
	attr->threads_attr[n].cpu_tag = cpu_tag;
}

void NONNULL(1)
mm_domain_attr_setspace(struct mm_domain_attr *attr, bool enable)
{
	attr->private_space = enable;
}

void NONNULL(1)
mm_domain_attr_setstacksize(struct mm_domain_attr *attr, uint32_t size)
{
	attr->stack_size = size;
}

void NONNULL(1)
mm_domain_attr_setguardsize(struct mm_domain_attr *attr, uint32_t size)
{
	attr->guard_size = size;
}

void NONNULL(1)
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

/**********************************************************************
 * Domain creation routines.
 **********************************************************************/

struct mm_domain * NONNULL(2)
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
	struct mm_thread_ident_pair id_pair = mm_thread_ident_alloc(1, domain->nthreads);
	VERIFY(id_pair.domain != MM_THREAD_NONE && id_pair.thread != MM_THREAD_NONE);
	domain->domain_ident = id_pair.domain;
	domain->thread_ident_base = id_pair.thread;

	// Set the domain name.
	if (attr != NULL && attr->name[0])
		memcpy(domain->name, attr->name, MM_DOMAIN_NAME_SIZE);
	else
		strcpy(domain->name, "unnamed");

	// Set thread start/stop barrier.
	mm_thread_barrier_prepare(&domain->barrier, domain->nthreads);

	// Initialize per-thread data.
	mm_thread_local_init(domain);
	MM_THREAD_LOCAL_ALLOC(domain, "domain barrier slot", domain->barrier_local);
	for (mm_thread_t i = 0; i < domain->nthreads; i++) {
		struct mm_thread_barrier_local *barrier_local = MM_THREAD_LOCAL_DEREF(i, domain->barrier_local);
		mm_thread_barrier_local_prepare(barrier_local);
	}

	// Set thread attributes.
	struct mm_thread_attr thread_attr;
	mm_thread_attr_prepare(&thread_attr);
	uint32_t stack_size = 0;
	uint32_t guard_size = 0;
	if (attr != NULL) {
		mm_thread_attr_setspace(&thread_attr, attr->private_space);

		stack_size = mm_round_up(attr->stack_size, MM_PAGE_SIZE);
		if (stack_size && stack_size < MM_THREAD_STACK_MIN)
			stack_size = MM_THREAD_STACK_MIN;
		guard_size = mm_round_up(attr->guard_size, MM_PAGE_SIZE);
	}

	// Create and start threads.
	domain->threads = mm_global_calloc(domain->nthreads, sizeof(struct mm_thread *));
	for (mm_thread_t i = 0; i < domain->nthreads; i++) {
		mm_thread_attr_setdomain(&thread_attr, domain, i);
		if (attr == NULL || attr->threads_attr == NULL)
			mm_thread_attr_setcputag(&thread_attr, MM_THREAD_CPU_ANY);
		else
			mm_thread_attr_setcputag(&thread_attr, attr->threads_attr[i].cpu_tag);

		if (stack_size) {
			void *stack = mm_cstack_create(stack_size + guard_size, guard_size);
			void *stack_base = ((char *) stack) + guard_size;
			mm_thread_attr_setstack(&thread_attr, stack_base, stack_size);
		} else if (guard_size) {
			mm_thread_attr_setguardsize(&thread_attr, guard_size);
		}

#if __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
		char thread_name[MM_THREAD_NAME_SIZE];
		snprintf(thread_name, sizeof thread_name, "%s %u", domain->name, i);
		mm_thread_attr_setname(&thread_attr, thread_name);
#if __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif

		domain->threads[i] = mm_thread_create(&thread_attr, start, attr->threads_attr[i].arg);
	}

	LEAVE();
	return domain;
}

void NONNULL(1)
mm_domain_destroy(struct mm_domain *domain)
{
	ENTER();

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


void
mm_domain_barrier(void)
{
	ENTER();

	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_domain *domain = mm_thread_getdomain(thread);
	if (domain != NULL) {
		mm_log_relay();

		mm_thread_t n = mm_thread_getnumber(thread);
		struct mm_thread_barrier_local *barrier_local = MM_THREAD_LOCAL_DEREF(n, domain->barrier_local);
		mm_thread_barrier_wait(&domain->barrier, barrier_local);
	}

	LEAVE();
}
