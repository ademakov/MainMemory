/*
 * base/runtime.h - Base library runtime.
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

#ifndef BASE_RUNTIME_H
#define BASE_RUNTIME_H

#include "common.h"
#include "base/thread/domain.h"

struct mm_base_params
{
	const char *regular_name;

	uint32_t thread_stack_size;
	uint32_t thread_guard_size;

	mm_routine_t thread_routine;
};

extern uint16_t mm_ncpus;
extern struct mm_domain *mm_regular_domain;

void
mm_base_init(void);

void
mm_base_term(void);

void
mm_base_loop(struct mm_base_params *params);

#endif /* BASE_RUNTIME_H */
