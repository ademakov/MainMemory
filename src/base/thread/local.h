/*
 * base/thread/local.h - MainMemory thread-local data.
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

#ifndef BASE_THREAD_LOCAL_H
#define BASE_THREAD_LOCAL_H

#include "common.h"

struct mm_domain;

#define MM_THREAD_LOCAL_CHUNK_SIZE	MM_PAGE_SIZE

#define MM_THREAD_LOCAL(type, name) union { type *ptr; mm_thread_local_t ref; } name

#define MM_THREAD_LOCAL_ALLOC(domain, name, data) ({			\
		data.ref = mm_thread_local_alloc(domain, name,		\
						 sizeof(*data.ptr));	\
	})

#define MM_THREAD_LOCAL_DEREF(num, data) ({				\
		size_t off = (num) * MM_THREAD_LOCAL_CHUNK_SIZE;	\
		(typeof(data.ptr)) (data.ref + off);			\
	})

typedef uintptr_t mm_thread_local_t;

void NONNULL(1)
mm_thread_local_init(struct mm_domain *domain);

void NONNULL(1)
mm_thread_local_term(struct mm_domain *domain);

mm_thread_local_t NONNULL(1)
mm_thread_local_alloc(struct mm_domain *domain, const char *name, size_t size);

void NONNULL(1)
mm_thread_local_summary(struct mm_domain *domain);

#endif /* BASE_THREAD_LOCAL_H */
