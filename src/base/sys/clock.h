/*
 * base/sys/clock.h - MainMemory time routines.
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

#ifndef BASE_SYS_CLOCK_H
#define BASE_SYS_CLOCK_H

#include "common.h"

#define MM_CLOCK_REALTIME	((mm_clock_t) 0)
#define MM_CLOCK_MONOTONIC	((mm_clock_t) 1)

typedef int mm_clock_t;

void mm_clock_init(void);

mm_timeval_t mm_clock_gettime(mm_clock_t clock);
mm_timeval_t mm_clock_gettime_realtime(void);
mm_timeval_t mm_clock_gettime_monotonic(void);

#endif /* BASE_SYS_CLOCK_H */
