/*
 * base/util/hook.h - MainMemory hook routines.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef BASE_UTIL_HOOK_H
#define BASE_UTIL_HOOK_H

#include "common.h"

/* Forward declaration. */
struct mm_queue;

typedef void (*mm_hook_rtn0)(void);
typedef void (*mm_hook_rtn1)(void *);

void mm_hook_free(struct mm_queue *hook)
	__attribute__((nonnull(1)));

void mm_hook_call(struct mm_queue *hook, bool free)
	__attribute__((nonnull(1)));

void mm_hook_head_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
	__attribute__((nonnull(1, 2)));

void mm_hook_tail_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
	__attribute__((nonnull(1, 2)));

void mm_hook_head_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
	__attribute__((nonnull(1, 2)));

void mm_hook_tail_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
	__attribute__((nonnull(1, 2)));

#endif /* BASE_UTIL_HOOK_H */
