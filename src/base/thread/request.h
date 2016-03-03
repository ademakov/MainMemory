/*
 * base/thread/request.h - MainMemory thread requests.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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
struct mm_request_sender;

#define MM_REQUEST_ONEWAY_N	(6)
#define MM_REQUEST_TWOWAY_N	(5)

/* Request routines. */
typedef void (*mm_request_oneway_t)(uintptr_t arguments[MM_REQUEST_ONEWAY_N]);
typedef uintptr_t (*mm_request_t)(uintptr_t arguments[MM_REQUEST_TWOWAY_N]);

/* Response routine. */
typedef void (*mm_response_t)(struct mm_request_sender *sender, uintptr_t result);

/* The request maker identity. To be used with containerof() macro. */
struct mm_request_sender {
	mm_request_t request;
	mm_response_t response;
};

/* Request representation for dequeueing. */
struct mm_request_data
{
	union
	{
		uintptr_t data[MM_REQUEST_ONEWAY_N + 1];
		struct
		{
			mm_request_oneway_t request;
			uintptr_t arguments[MM_REQUEST_ONEWAY_N];
		};
	};
};

void
mm_request_response_handler(uintptr_t *arguments);

/**********************************************************************
 * Request fetching and execution.
 **********************************************************************/

static inline bool NONNULL(1, 2)
mm_request_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_mpmc_get_n(ring, rdata->data, 7);
}

static inline bool NONNULL(1, 2)
mm_request_relaxed_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_relaxed_get_n(ring, rdata->data, 7);
}

static inline void
mm_request_execute(struct mm_request_data *rdata)
{
	(*rdata->request)(rdata->arguments);
}

/**********************************************************************
 * One-way requests.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_request_post_0(struct mm_ring_mpmc *ring, mm_request_oneway_t req)
{
	uintptr_t data[] = { (uintptr_t) req };
	mm_ring_mpmc_enqueue_n(ring, data, 1);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_0(struct mm_ring_mpmc *ring, mm_request_oneway_t req)
{
	uintptr_t data[] = { (uintptr_t) req };
	return mm_ring_mpmc_put_n(ring, data, 1);
}

static inline void NONNULL(1, 2)
mm_request_post_1(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1)
{
	uintptr_t data[] = { (uintptr_t) req, a1 };
	mm_ring_mpmc_enqueue_n(ring, data, 2);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_1(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1)
{
	uintptr_t data[] = { (uintptr_t) req, a1 };
	return mm_ring_mpmc_put_n(ring, data, 2);
}

static inline void NONNULL(1, 2)
mm_request_post_2(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1, uintptr_t a2)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2 };
	mm_ring_mpmc_enqueue_n(ring, data, 3);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_2(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1, uintptr_t a2)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2 };
	return mm_ring_mpmc_put_n(ring, data, 3);
}

static inline void NONNULL(1, 2)
mm_request_post_3(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3 };
	mm_ring_mpmc_enqueue_n(ring, data, 4);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_3(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3 };
	return mm_ring_mpmc_put_n(ring, data, 4);
}

static inline void NONNULL(1, 2)
mm_request_post_4(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3,
		  uintptr_t a4)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4 };
	mm_ring_mpmc_enqueue_n(ring, data, 5);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_4(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3,
		     uintptr_t a4)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4 };
	return mm_ring_mpmc_get_n(ring, data, 5);
}

static inline void NONNULL(1, 2)
mm_request_post_5(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3,
		  uintptr_t a4, uintptr_t a5)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4, a5 };
	mm_ring_mpmc_enqueue_n(ring, data, 6);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_5(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3,
		     uintptr_t a4, uintptr_t a5)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4, a5 };
	return mm_ring_mpmc_put_n(ring, data, 6);
}

static inline void NONNULL(1, 2)
mm_request_post_6(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3,
		  uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4, a5, a6 };
	mm_ring_mpmc_enqueue_n(ring, data, 7);
}

static inline bool NONNULL(1, 2)
mm_request_trypost_6(struct mm_ring_mpmc *ring, mm_request_oneway_t req,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3,
		     uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	uintptr_t data[] = { (uintptr_t) req, a1, a2, a3, a4, a5, a6 };
	return mm_ring_mpmc_put_n(ring, data, 7);
}

/**********************************************************************
 * Requests with responses.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_request_send_0(struct mm_ring_mpmc *ring, struct mm_request_sender *s)
{
	mm_request_post_1(ring, mm_request_response_handler,
			  (intptr_t) s);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_0(struct mm_ring_mpmc *ring, struct mm_request_sender *s)
{
	return mm_request_trypost_1(ring, mm_request_response_handler,
				    (intptr_t) s);
}

static inline void NONNULL(1, 2)
mm_request_send_1(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		  uintptr_t a1)
{
	mm_request_post_2(ring, mm_request_response_handler,
			  (intptr_t) s, a1);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_1(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		     uintptr_t a1)
{
	return mm_request_trypost_2(ring, mm_request_response_handler,
				    (intptr_t) s, a1);
}

static inline void NONNULL(1, 2)
mm_request_send_2(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		  uintptr_t a1, uintptr_t a2)
{
	mm_request_post_3(ring, mm_request_response_handler,
			  (intptr_t) s, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_2(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		     uintptr_t a1, uintptr_t a2)
{
	return mm_request_trypost_3(ring, mm_request_response_handler,
				    (intptr_t) s, a1, a2);
}

static inline void NONNULL(1, 2)
mm_request_send_3(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	mm_request_post_4(ring, mm_request_response_handler,
			  (intptr_t) s, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_3(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	return mm_request_trypost_4(ring, mm_request_response_handler,
				    (intptr_t) s, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_request_send_4(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	mm_request_post_5(ring, mm_request_response_handler,
			  (intptr_t) s, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_4(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3,
		     uintptr_t a4)
{
	return mm_request_trypost_5(ring, mm_request_response_handler,
				    (intptr_t) s, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_request_send_5(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		  uintptr_t a1, uintptr_t a2, uintptr_t a3,
		  uintptr_t a4, uintptr_t a5)
{
	mm_request_post_6(ring, mm_request_response_handler,
			  (intptr_t) s, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_request_trysend_5(struct mm_ring_mpmc *ring, struct mm_request_sender *s,
		     uintptr_t a1, uintptr_t a2, uintptr_t a3,
		     uintptr_t a4, uintptr_t a5)
{
	return mm_request_trypost_6(ring, mm_request_response_handler,
				    (intptr_t) s, a1, a2, a3, a4, a5);
}

/**********************************************************************
 * Request wrapper generator.
 **********************************************************************/

/**
 * Define a wrapper for request receive routine.
 */
#define MM_REQUEST_RECEIVE_WRAPPER(prefix, container, name)		\
static inline bool NONNULL(1, 2)					\
prefix##_receive(container *p, struct mm_request_data *r)		\
{									\
	return mm_request_receive(p->name, r);				\
}									\
									\
/**
 * Define a wrapper for single-threaded request receive routine.
 */
#define MM_REQUEST_RELAXED_RECEIVE_WRAPPER(prefix, container, name)	\
static inline bool NONNULL(1, 2)					\
prefix##_receive(container *p, struct mm_request_data *r)		\
{									\
	return mm_request_relaxed_receive(p->name, r);		       	\
}

/**
 * Define wrappers for submit and oneway submit routines.
 */
#define MM_REQUEST_SUBMIT_WRAPPERS(prefix, container, name)		\
static inline void NONNULL(1, 2)					\
prefix##_post_0(container *p, mm_request_oneway_t r)			\
{									\
	mm_request_post_0(p->name, r);					\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_0(container *p, mm_request_oneway_t r)			\
{									\
	return mm_request_trypost_0(p->name, r);			\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_1(container *p, mm_request_oneway_t r,			\
		uintptr_t a1)						\
{									\
	mm_request_post_1(p->name, r, a1);				\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_1(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1)					\
{									\
	return mm_request_trypost_1(p->name, r, a1);			\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_2(container *p, mm_request_oneway_t r,			\
		uintptr_t a1, uintptr_t a2)				\
{									\
	mm_request_post_2(p->name, r, a1, a2);				\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_2(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1, uintptr_t a2)				\
{									\
	return mm_request_trypost_2(p->name, r, a1, a2);		\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_3(container *p, mm_request_oneway_t r,			\
		uintptr_t a1, uintptr_t a2, uintptr_t a3)		\
{									\
	mm_request_post_3(p->name, r, a1, a2, a3);			\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_3(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)		\
{									\
	return mm_request_trypost_3(p->name, r, a1, a2, a3);		\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_4(container *p, mm_request_oneway_t r,			\
		uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		uintptr_t a4)						\
{									\
	mm_request_post_4(p->name, r, a1, a2, a3, a4);			\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_4(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		   uintptr_t a4)					\
{									\
	return mm_request_trypost_4(p->name, r, a1, a2, a3, a4);	\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_5(container *p, mm_request_oneway_t r,			\
		uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		uintptr_t a4, uintptr_t a5)				\
{									\
	mm_request_post_5(p->name, r, a1, a2, a3, a4, a5);		\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_5(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		   uintptr_t a4, uintptr_t a5)				\
{									\
	return mm_request_trypost_5(p->name, r, a1, a2, a3, a4, a5);	\
}									\
static inline void NONNULL(1, 2)					\
prefix##_post_6(container *p, mm_request_oneway_t r,			\
		uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		uintptr_t a4, uintptr_t a5, uintptr_t a6)		\
{									\
	mm_request_post_6(p->name, r, a1, a2, a3, a4, a5, a6);		\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trypost_6(container *p, mm_request_oneway_t r,			\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		   uintptr_t a4, uintptr_t a5, uintptr_t a6)		\
{									\
	return mm_request_trypost_6(p->name, r, a1, a2, a3, a4, a5, a6);\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_0(container *p, struct mm_request_sender *s)		\
{									\
	mm_request_send_0(p->name, s);					\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_0(container *p, struct mm_request_sender *s)		\
{									\
	return mm_request_trysend_0(p->name, s);			\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_1(container *p, struct mm_request_sender *s, 		\
		uintptr_t a1)						\
{									\
	mm_request_send_1(p->name, s, a1);				\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_1(container *p, struct mm_request_sender *s, 		\
		   uintptr_t a1)					\
{									\
	return mm_request_trysend_1(p->name, s, a1);			\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_2(container *p, struct mm_request_sender *s,		\
		uintptr_t a1, uintptr_t a2)				\
{									\
	mm_request_send_2(p->name, s, a1, a2);				\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_2(container *p, struct mm_request_sender *s,		\
		  uintptr_t a1, uintptr_t a2)				\
{									\
	return mm_request_trysend_2(p->name, s, a1, a2);		\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_3(container *p, struct mm_request_sender *s,		\
		uintptr_t a1, uintptr_t a2, uintptr_t a3)		\
{									\
	mm_request_send_3(p->name, s, a1, a2, a3);			\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_3(container *p, struct mm_request_sender *s,		\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3)		\
{									\
	return mm_request_trysend_3(p->name, s, a1, a2, a3);		\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_4(container *p, struct mm_request_sender *s,		\
		uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		uintptr_t a4)						\
{									\
	mm_request_send_4(p->name, s, a1, a2, a3, a4);			\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_4(container *p, struct mm_request_sender *s,		\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		   uintptr_t a4)					\
{									\
	return mm_request_trysend_4(p->name, s, a1, a2, a3, a4);	\
}									\
static inline void NONNULL(1, 2)					\
prefix##_send_5(container *p, struct mm_request_sender *s,		\
		uintptr_t a1, uintptr_t a2, uintptr_t a3, 		\
		uintptr_t a4, uintptr_t a5)				\
{									\
	mm_request_send_5(p->name, s, a1, a2, a3, a4, a5);		\
}									\
static inline bool NONNULL(1, 2)					\
prefix##_trysend_5(container *p, struct mm_request_sender *s,		\
		   uintptr_t a1, uintptr_t a2, uintptr_t a3,		\
		   uintptr_t a4, uintptr_t a5)				\
{									\
	return mm_request_trysend_5(p->name, s, a1, a2, a3, a4, a5); 	\
}

#endif /* BASE_THREAD_REQUEST_H */
