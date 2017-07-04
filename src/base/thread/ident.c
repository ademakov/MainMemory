/*
 * base/thread/ident.c - MainMemory thread identification.
 *
 * Copyright (C) 2017  Aleksey Demakov
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

#include "base/thread/ident.h"

#include "base/lock.h"
#include "base/report.h"

static mm_common_lock_t mm_thread_ident_lock = MM_COMMON_LOCK_INIT;

static mm_thread_t mm_next_domain = 0;
static mm_thread_t mm_next_thread = 0;

struct mm_thread_ident_pair
mm_thread_ident_alloc(mm_thread_t ndomains, mm_thread_t nthreads)
{
	ENTER();
	VERIFY(nthreads > 0);
	struct mm_thread_ident_pair result = { MM_THREAD_NONE, MM_THREAD_NONE };

	mm_common_lock(&mm_thread_ident_lock);

	uint32_t next_thread = mm_next_thread + nthreads;
	if (next_thread <= MM_THREAD_NONE) {
		if (ndomains == 0) {
			result.thread = mm_next_thread;
			mm_next_thread = next_thread;
		} else {
			uint32_t next_domain = mm_next_domain + ndomains;
			if (next_domain <= MM_THREAD_NONE) {
				result.domain = mm_next_domain;
				result.thread = mm_next_thread;
				mm_next_domain = next_domain;
				mm_next_thread = next_thread;
			}
		}
	}

	mm_common_unlock(&mm_thread_ident_lock);

	LEAVE();
	return result;
}
