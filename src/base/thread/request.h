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
#include "base/report.h"
#include "base/ring.h"

/* Forward declarations. */
struct mm_request_sender;

/* The maximum number of arguments for post requests. */
#define MM_POST_MAX		(MM_RING_MPMC_DATA_SIZE - 1)
/* The maximum number of arguments for send requests. */
#define MM_SEND_MAX		(MM_RING_MPMC_DATA_SIZE - 2)

/**********************************************************************
 * Request construction.
 **********************************************************************/

/* The size of ring data for a given number of post arguments. */
#define MM_POST_ARGC(c) 	((c) + 1)
/* Define ring data for a post request together with its arguments. */
#define MM_POST_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) __VA_ARGS__ }

/* The size of ring data for a given number of send arguments. */
#define MM_SEND_ARGC(c)		((c) + 2)
/* Define ring data for a send request together with its arguments. */
#define MM_SEND_ARGV(v, ...)	uintptr_t v[] = { (uintptr_t) mm_request_handler, (uintptr_t) __VA_ARGS__ }

/* Request routines. */
typedef void (*mm_post_routine_t)(uintptr_t arguments[MM_POST_MAX]);
typedef uintptr_t (*mm_request_routine_t)(uintptr_t arguments[MM_SEND_MAX]);

/* Response routine. */
typedef void (*mm_response_routine_t)(struct mm_request_sender *sender, uintptr_t result);

/* The request maker identity. To be used with containerof() macro. */
struct mm_request_sender {
	mm_request_routine_t request;
	mm_response_routine_t response;
};

void
mm_request_handler(uintptr_t *arguments);

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

#endif /* BASE_THREAD_REQUEST_H */
