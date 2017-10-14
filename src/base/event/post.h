/*
 * base/event/post.h - MainMemory cross-thread procedure calls.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#ifndef BASE_EVENT_POST_H
#define BASE_EVENT_POST_H

#include "common.h"
#include "base/ring.h"

/* The maximum number of arguments for post requests. */
#define MM_POST_MAX		(MM_RING_MPMC_DATA_SIZE - 1)

/**********************************************************************
 * Request construction.
 **********************************************************************/

/* The size of ring data for a given number of post arguments. */
#define MM_POST_ARGC(c) 	((c) + 1)
/* Define ring data for a post request together with its arguments. */
#define MM_POST_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) __VA_ARGS__ }

/* Post a request to a cross-thread request ring. */
#define MM_POST(n, ring, notify, target, ...)				\
	do {								\
		mm_stamp_t s;						\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		mm_ring_mpmc_enqueue_sn(ring, &s, v, MM_POST_ARGC(n));	\
		notify(target, s);					\
	} while (0)

/* Try to post a request to a cross-thread request ring. */
#define MM_TRYPOST(n, ring, notify, target, ...)				\
	do {								\
		bool rc;							\
		mm_stamp_t s;						\
		MM_POST_ARGV(v, __VA_ARGS__);				\
		rc = mm_ring_mpmc_put_sn(ring, &s, v, MM_POST_ARGC(n));	\
		if (rc)	{ notify(target, s); }				\
		return rc;						\
	} while (0)

/* Request routines. */
typedef void (*mm_post_routine_t)(uintptr_t arguments[MM_POST_MAX]);

/**********************************************************************
 * Request fetching and execution.
 **********************************************************************/

struct mm_request_data
{
	union
	{
		uintptr_t data[MM_POST_MAX + 1];
		struct
		{
			mm_post_routine_t request;
			uintptr_t arguments[MM_POST_MAX];
		};
	};
};

static inline bool NONNULL(1, 2)
mm_request_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_mpmc_get_n(ring, rdata->data, MM_RING_MPMC_DATA_SIZE);
}

static inline bool NONNULL(1, 2)
mm_request_relaxed_receive(struct mm_ring_mpmc *ring, struct mm_request_data *rdata)
{
	return mm_ring_mpsc_get_n(ring, rdata->data, MM_RING_MPMC_DATA_SIZE);
}

static inline void
mm_request_execute(struct mm_request_data *rdata)
{
	(*rdata->request)(rdata->arguments);
}

#endif /* BASE_EVENT_POST_H */
