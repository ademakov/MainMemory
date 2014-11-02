/*
 * base/thr/domain.c - MainMemory thread domain.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "base/thr/domain.h"
#include "base/log/trace.h"
#include "base/mem/space.h"

#include <stdio.h>

__thread struct mm_domain *__mm_domain_self = NULL;

void
mm_domain_prepare(struct mm_domain *domain, const char *name, mm_core_t nthreads)
{
	ENTER();

	// Set domain name.
	size_t name_len = 0;
	if (likely(name != NULL)) {
		name_len = strlen(name);
		if (name_len >= sizeof domain->name)
			name_len = sizeof domain->name - 1;
		memcpy(domain->name, name, name_len);
	}
	domain->name[name_len] = 0;

	// Initialize thread data.
	domain->nthreads = nthreads;
	domain->threads = mm_common_calloc(nthreads, sizeof(struct mm_domain_thread));
	for (mm_core_t i = 0; i < nthreads; i++) {
		struct mm_thread_attr *thread_attr = &domain->threads[i].thread_attr;
		mm_thread_attr_init(thread_attr);

		// Derive thread name from the domain name.
		if (name_len) {
			char thread_name[MM_THREAD_NAME_SIZE];
			snprintf(thread_name, sizeof thread_name,
				 "%s %u", name, i);
			mm_thread_attr_setname(thread_attr, thread_name);
		}
	}

	LEAVE();
}

void
mm_domain_cleanup(struct mm_domain *domain)
{
	ENTER();

	for (mm_core_t i = 0; i < domain->nthreads; i++) {
		struct mm_domain_thread *thread = &domain->threads[i];
		mm_thread_destroy(thread->thread);
	}

	mm_common_free(domain->threads);

	LEAVE();
}

void
mm_domain_setcputag(struct mm_domain *domain, mm_core_t n, uint32_t cpu_tag)
{
	ENTER();
	ASSERT(n < domain->nthreads);

	struct mm_domain_thread *thread = &domain->threads[n];
	mm_thread_attr_setcputag(&thread->thread_attr, cpu_tag);

	LEAVE();
}

void
mm_domain_setstack(struct mm_domain *domain, mm_core_t n, void *stack_base, uint32_t stack_size)
{
	ENTER();
	ASSERT(n < domain->nthreads);

	struct mm_domain_thread *thread = &domain->threads[n];
	mm_thread_attr_setstack(&thread->thread_attr, stack_base, stack_size);

	LEAVE();
}

void
mm_domain_start(struct mm_domain *domain, mm_routine_t start)
{
	ENTER();

	// Set thread start barrier.
	mm_barrier_init(&domain->barrier, domain->nthreads);

	// Create and start thread.
	for (mm_core_t i = 0; i < domain->nthreads; i++) {
		struct mm_domain_thread *thread = &domain->threads[i];
		mm_thread_attr_setdomain(&thread->thread_attr, domain, i);
		thread->thread = mm_thread_create(&thread->thread_attr, start, i);
	}

	LEAVE();
}

void
mm_domain_join(struct mm_domain *domain)
{
	ENTER();

	for (mm_core_t i = 0; i < domain->nthreads; i++) {
		struct mm_domain_thread *thread = &domain->threads[i];
		mm_thread_join(thread->thread);
	}

	LEAVE();
}
