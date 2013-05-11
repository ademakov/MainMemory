/*
 * timer.h - MainMemory timers.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef TIMER_H
#define TIMER_H

#include "common.h"
#include "clock.h"
#include "task.h"
#include "timeq.h"

#define MM_TIMER_ERROR	((mm_timer_t) -1)
#define MM_TIMER_BLOCK	((mm_timer_t) -2)

typedef mm_timeq_ident_t mm_timer_t;

void mm_timer_init(void);
void mm_timer_term(void);

void mm_timer_tick(void);

mm_timeval_t mm_timer_next(void);

mm_timer_t mm_timer_create(mm_clock_t clock, mm_task_flags_t flags,
			   mm_routine_t start, uintptr_t start_arg);
void mm_timer_destroy(mm_timer_t timer_id);

void mm_timer_settime(mm_timer_t timer_id, bool abstime,
		      mm_timeval_t value, mm_timeval_t interval);

void mm_timer_block(mm_timeout_t timeout);

#endif /* TIMER_H */
