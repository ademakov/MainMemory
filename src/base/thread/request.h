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
#include "base/log/error.h"

/* Forward declarations. */
struct mm_requestor;

#define MM_REQUEST_ONEWAY	(((uintptr_t) 1) << (sizeof(uintptr_t) * 8 - 1))

/* Request routines. */
typedef void (*mm_request_oneway_t)(uintptr_t context, uintptr_t *arguments);
typedef uintptr_t (*mm_request_t)(uintptr_t context, uintptr_t *arguments);

/* Response routine. */
typedef void (*mm_response_t)(uintptr_t context, struct mm_requestor *rtor,
			      uintptr_t result);

/* The request maker id. To be used with containerof() macro. */
struct mm_requestor {
	mm_response_t response;
};

/* Request representation for dequeueing. */
struct mm_request_data
{
	union
	{
		uintptr_t data[7];
		struct
		{
			mm_request_oneway_t oneway_request;
			uintptr_t oneway_arguments[6];
		};
		struct
		{
			mm_request_t request;
			struct mm_requestor *requestor;
			uintptr_t arguments[5];
		};
	};
};

static inline void
mm_request_verify_address(mm_request_t req)
{
	uintptr_t x = (uintptr_t) req;
	if (unlikely(x & MM_REQUEST_ONEWAY))
		mm_fatal(0, "Thread request routines must have addresses with clear most-significant bit.");
}

/**********************************************************************
 * Request fetching and execution.
 **********************************************************************/

static inline bool __attribute__((nonnull(1, 2)))
mm_request_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_mpmc_get_n(ring, rdata->data, 7);
}

static inline bool __attribute__((nonnull(1, 2)))
mm_request_relaxed_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_relaxed_get_n(ring, rdata->data, 7);
}

static inline void
mm_request_execute(uintptr_t context, struct mm_request_data *rdata)
{
	if ((rdata->data[0] & MM_REQUEST_ONEWAY) != 0) {
		rdata->data[0] &= ~MM_REQUEST_ONEWAY;
		(*rdata->oneway_request)(context, rdata->oneway_arguments);
	} else {
		uintptr_t result = (*rdata->request)(context, rdata->arguments);
		(*rdata->requestor->response)(context, rdata->requestor, result);
	}
}

/**********************************************************************
 * One-way requests.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_0(struct mm_ring_mpmc *ring, mm_request_t req)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req
	};
	mm_ring_mpmc_enqueue_n(ring, data, 1);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_1(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1
	};
	mm_ring_mpmc_enqueue_n(ring, data, 2);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_2(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1, uintptr_t arg2)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1, arg2
	};
	mm_ring_mpmc_enqueue_n(ring, data, 3);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_3(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1, uintptr_t arg2, uintptr_t arg3)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1, arg2, arg3
	};
	mm_ring_mpmc_enqueue_n(ring, data, 4);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_4(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
			   uintptr_t arg4)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1, arg2, arg3, arg4
	};
	mm_ring_mpmc_enqueue_n(ring, data, 5);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_5(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
			   uintptr_t arg4, uintptr_t arg5)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1, arg2, arg3, arg4, arg5
	};
	mm_ring_mpmc_enqueue_n(ring, data, 6);
}

static inline void __attribute__((nonnull(1, 2)))
mm_request_submit_oneway_6(struct mm_ring_mpmc *ring, mm_request_t req,
			   uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
			   uintptr_t arg4, uintptr_t arg5, uintptr_t arg6)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		MM_REQUEST_ONEWAY | (uintptr_t) req, arg1, arg2, arg3, arg4, arg5, arg6
	};
	mm_ring_mpmc_enqueue_n(ring, data, 7);
}

/**********************************************************************
 * Requests with responses.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_0(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor
	};
	mm_ring_mpmc_enqueue_n(ring, data, 2);
}

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_1(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req, uintptr_t arg1)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor, arg1
	};
	mm_ring_mpmc_enqueue_n(ring, data, 3);
}

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_2(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req, uintptr_t arg1, uintptr_t arg2)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor, arg1, arg2
	};
	mm_ring_mpmc_enqueue_n(ring, data, 4);
}

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_3(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req, uintptr_t arg1, uintptr_t arg2,
		    uintptr_t arg3)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor, arg1, arg2, arg3
	};
	mm_ring_mpmc_enqueue_n(ring, data, 5);
}

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_4(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req, uintptr_t arg1, uintptr_t arg2,
		    uintptr_t arg3, uintptr_t arg4)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor, arg1, arg2, arg3, arg4
	};
	mm_ring_mpmc_enqueue_n(ring, data, 6);
}

static inline void __attribute__((nonnull(1, 2, 3)))
mm_request_submit_5(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		    mm_request_t req, uintptr_t arg1, uintptr_t arg2,
		    uintptr_t arg3, uintptr_t arg4, uintptr_t arg5)
{
	mm_request_verify_address(req);
	uintptr_t data[] = {
		(uintptr_t) req, (intptr_t) rtor, arg1, arg2, arg3, arg4, arg5
	};
	mm_ring_mpmc_enqueue_n(ring, data, 7);
}

/**********************************************************************
 * System call requests.
 **********************************************************************/

uintptr_t mm_request_syscall_handler(uintptr_t context, uintptr_t *arguments);

static inline void
mm_request_syscall_0(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		     int number)
{
	mm_request_submit_1(ring, rtor, mm_request_syscall_handler, number);
}

static inline void
mm_request_syscall_1(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		     int number, uintptr_t arg1)
{
	mm_request_submit_2(ring, rtor, mm_request_syscall_handler, number,
			    arg1);
}

static inline void
mm_request_syscall_2(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		     int number, uintptr_t arg1, uintptr_t arg2)
{
	mm_request_submit_3(ring, rtor, mm_request_syscall_handler, number,
			    arg1, arg2);
}

static inline void
mm_request_syscall_3(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		     int number, uintptr_t arg1, uintptr_t arg2,
		     uintptr_t arg3)
{
	mm_request_submit_4(ring, rtor, mm_request_syscall_handler, number,
			    arg1, arg2, arg3);
}

static inline void
mm_request_syscall_4(struct mm_ring_mpmc *ring, struct mm_requestor *rtor,
		     int number, uintptr_t arg1, uintptr_t arg2,
		     uintptr_t arg3, uintptr_t arg4)
{
	mm_request_submit_5(ring, rtor, mm_request_syscall_handler, number,
			    arg1, arg2, arg3, arg4);
}

/**********************************************************************
 * Request wrapper generator.
 **********************************************************************/

/**
 * Define a wrapper for request receive routine.
 */
#define MM_REQUEST_RECEIVE_WRAPPER(prefix, container, name)		\
static inline bool __attribute__((nonnull(1, 2)))			\
prefix##_receive(container *p, struct mm_request_data *r)		\
{									\
	return mm_request_receive(p->name, r);				\
}									\
									\
/**
 * Define a wrapper for single-threaded request receive routine.
 */
#define MM_REQUEST_RELAXED_RECEIVE_WRAPPER(prefix, container, name)	\
static inline bool __attribute__((nonnull(1, 2)))			\
prefix##_receive(container *p, struct mm_request_data *r)		\
{									\
	return mm_request_relaxed_receive(p->name, r);		       	\
}

/**
 * Define wrappers for submit and oneway submit routines.
 */
#define MM_REQUEST_SUBMIT_WRAPPERS(prefix, container, name)		\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_0(container *p, mm_request_t r)			\
{									\
	mm_request_submit_oneway_0(p->name, r);				\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_1(container *p, mm_request_t r,			\
			 uintptr_t a1)					\
{									\
	mm_request_submit_oneway_1(p->name, r, a1);			\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_2(container *p, mm_request_t r,			\
			  uintptr_t a1, uintptr_t a2)			\
{									\
	mm_request_submit_oneway_2(p->name, r, a1, a2);			\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_3(container *p, mm_request_t r,			\
			  uintptr_t a1, uintptr_t a2, uintptr_t a3)	\
{									\
	mm_request_submit_oneway_3(p->name, r, a1, a2, a3);		\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_4(container *p, mm_request_t r,			\
			 uintptr_t a1, uintptr_t a2, uintptr_t a3,	\
			 uintptr_t a4)					\
{									\
	mm_request_submit_oneway_4(p->name, r, a1, a2, a3, a4);		\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_5(container *p, mm_request_t r,			\
			 uintptr_t a1, uintptr_t a2, uintptr_t a3,	\
			 uintptr_t a4, uintptr_t a5)			\
{									\
	mm_request_submit_oneway_5(p->name, r, a1, a2, a3, a4, a5);	\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_submit_oneway_6(container *p, mm_request_t r,			\
			 uintptr_t a1, uintptr_t a2, uintptr_t a3,	\
			 uintptr_t a4, uintptr_t a5, uintptr_t a6)	\
{									\
	mm_request_submit_oneway_6(p->name, r, a1, a2, a3, a4, a5, a6);	\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_0(container *p, struct mm_requestor *rtor,		\
		  mm_request_t r)					\
{									\
	mm_request_submit_0(p->name, rtor, r);				\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_1(container *p, struct mm_requestor *rtor, 		\
		  mm_request_t r, uintptr_t a1)				\
{									\
	mm_request_submit_1(p->name, rtor, r, a1);			\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_2(container *p, struct mm_requestor *rtor,		\
		  mm_request_t r, uintptr_t a1, uintptr_t a2)		\
{									\
	mm_request_submit_2(p->name, rtor, r, a1, a2);			\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_3(container *p, struct mm_requestor *rtor,		\
		  mm_request_t r, uintptr_t a1, uintptr_t a2,		\
		  uintptr_t a3)						\
{									\
	mm_request_submit_3(p->name, rtor, r, a1, a2, a3);		\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_4(container *p, struct mm_requestor *rtor,		\
		  mm_request_t r, uintptr_t a1, uintptr_t a2, 		\
		  uintptr_t a3,	uintptr_t a4)				\
{									\
	mm_request_submit_4(p->name, rtor, r, a1, a2, a3, a4);		\
}									\
static inline void __attribute__((nonnull(1, 2, 3)))			\
prefix##_submit_5(container *p, struct mm_requestor *rtor,		\
		  mm_request_t r, uintptr_t a1, uintptr_t a2,		\
		  uintptr_t a3, uintptr_t a4, uintptr_t a5)		\
{									\
	mm_request_submit_5(p->name, rtor, r, a1, a2, a3, a4, a5);	\
}

/**
 * Define wrappers for syscall request routines.
 */
#define MM_REQUEST_SYSCALL_WRAPPERS(prefix, container, name)		\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_syscall_0(container *p, struct mm_requestor *rtor,		\
		   int n)						\
{									\
	mm_request_syscall_0(p->name, rtor, n);				\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_syscall_1(container *p, struct mm_requestor *rtor, 		\
		   int n, uintptr_t a1)					\
{									\
	mm_request_syscall_1(p->name, rtor, n, a1);			\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_syscall_2(container *p, struct mm_requestor *rtor,		\
		   int n, uintptr_t a1, uintptr_t a2)			\
{									\
	mm_request_syscall_2(p->name, rtor, n, a1, a2);			\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_syscall_3(container *p, struct mm_requestor *rtor,		\
		   int n, uintptr_t a1, uintptr_t a2,			\
		   uintptr_t a3)					\
{									\
	mm_request_syscall_3(p->name, rtor, n, a1, a2, a3);		\
}									\
static inline void __attribute__((nonnull(1, 2)))			\
prefix##_syscall_4(container *p, struct mm_requestor *rtor,		\
		   int n, uintptr_t a1, uintptr_t a2,			\
		   uintptr_t a3, uintptr_t a4)				\
{									\
	mm_request_syscall_4(p->name, rtor, n, a1, a2, a3, a4);		\
}

#endif /* BASE_THREAD_REQUEST_H */
