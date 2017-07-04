/*
 * base/thread/ident.h - MainMemory thread identification.
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

#ifndef BASE_THREAD_IDENT_H
#define BASE_THREAD_IDENT_H

#include "common.h"

struct mm_thread_ident_pair
{
	mm_thread_t domain;
	mm_thread_t thread;
};

struct mm_thread_ident_pair
mm_thread_ident_alloc(mm_thread_t ndomains, mm_thread_t nthreads);

#endif /* BASE_THREAD_IDENT_H */
