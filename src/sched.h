/*
 * sched.h - MainMemory task scheduler.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef SCHED_H
#define SCHED_H

#include "common.h"

/* Forward declaration. */
struct mm_task;

/* The currently running task. */
extern __thread struct mm_task *mm_running_task;

void mm_sched_run(struct mm_task *task);

void mm_sched_start(void);

void mm_sched_yield(void);
void mm_sched_block(void);
void mm_sched_abort(void) __attribute__((noreturn));

#endif /* SCHED_H */
