/*
 * base/thread/request.h - MainMemory thread requests.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef BASE_THREAD_REQUEST_H
#define BASE_THREAD_REQUEST_H

#include "common.h"
#include "base/ring.h"
#include "base/thread/domain.h"

/* Request routine. */
typedef void (*mm_request_t)(uintptr_t context, uintptr_t arguments[6]);

/* Request representation for dequeueing. */
struct mm_request_data
{
	union
	{
		uintptr_t data[7];
		struct
		{
			mm_request_t request;
			uintptr_t arguments[6];
		};
	};
};

static inline void
mm_request_execute(uintptr_t context, struct mm_request_data *request)
{
	(*request->request)(context, request->arguments);
}

static inline bool __attribute__((nonnull(1)))
mm_request_receive(struct mm_request_data *request)
{
	struct mm_domain *domain = mm_domain_self();
	return mm_ring_mpmc_get_n(domain->request_queue, request->data, 7);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_0(mm_request_t request)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 1);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_1(mm_request_t request, uintptr_t argument_1)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 2);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_2(mm_request_t request, uintptr_t argument_1,
		    uintptr_t argument_2)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1, argument_2
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 3);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_3(mm_request_t request, uintptr_t argument_1,
		    uintptr_t argument_2, uintptr_t argument_3)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1, argument_2,
		argument_3
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 4);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_4(mm_request_t request, uintptr_t argument_1,
		    uintptr_t argument_2, uintptr_t argument_3,
		    uintptr_t argument_4)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1, argument_2,
		argument_3, argument_4
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 5);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_5(mm_request_t request, uintptr_t argument_1,
		    uintptr_t argument_2, uintptr_t argument_3,
		    uintptr_t argument_4, uintptr_t argument_5)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1, argument_2,
		argument_3, argument_4, argument_5
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 6);
}

static inline void __attribute__((nonnull(1)))
mm_request_submit_6(mm_request_t request, uintptr_t argument_1,
		    uintptr_t argument_2, uintptr_t argument_3,
		    uintptr_t argument_4, uintptr_t argument_5,
		    uintptr_t argument_6)
{
	struct mm_domain *domain = mm_domain_self();
	uintptr_t data[] = {
		(uintptr_t) request, argument_1, argument_2,
		argument_3, argument_4, argument_5, argument_6
	};
	mm_ring_mpmc_enqueue_n(domain->request_queue, data, 7);
}

#endif /* BASE_THREAD_REQUEST_H */
