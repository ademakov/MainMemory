/*
 * clock.c - MainMemory time routines.
 *
 * Copyright (C) 2013  Aleksey Demakov.
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

#include "clock.h"

#include "util.h"

#ifdef HAVE_MACH_MACH_TIME_H
# include <mach/mach_time.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
# include <time.h>
#endif

#ifdef HAVE_MACH_MACH_TIME_H

static mm_timeval_t mm_abstime_numer;
static mm_timeval_t mm_abstime_denom;

void
mm_clock_init()
{
	ENTER();

	mach_timebase_info_data_t timebase_info;
	(void) mach_timebase_info(&timebase_info);
	ASSERT(timebase_info.denom != 0);

	mm_abstime_numer = timebase_info.numer;
	mm_abstime_denom = timebase_info.numer * 1000;

	LEAVE();
}

mm_timeval_t
mm_clock_realtime(void)
{
	struct timeval tv;
	(void) gettimeofday(&tv, 0);
	return tv.tv_sec * 1000000LL + tv.tv_usec;
}

mm_timeval_t
mm_clock_monotonic(void)
{
	uint64_t at = mach_absolute_time();
	return at * mm_abstime_numer / mm_abstime_denom;
}

#else

void
mm_clock_init()
{
}

mm_timeval_t
mm_clock_realtime(void)
{
	return 0;
}

mm_timeval_t
mm_clock_monotonic(void)
{
	return 0;
}

#endif