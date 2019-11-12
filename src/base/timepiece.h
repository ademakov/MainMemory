/*
 * base/timepiece.h - MainMemory internal clock.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_TIMEPIECE_H
#define BASE_TIMEPIECE_H

#include "common.h"
#include "base/arch/intrinsic.h"

#ifndef ENABLE_TIMEPIECE_TIMESTAMP
# if MM_CPU_TIMESTAMP
#  define ENABLE_TIMEPIECE_TIMESTAMP	(1)
# endif
#endif

/* Internal clock that is very coarse but takes very little CPU time on average.
   It is good enough for many tasks where time precision is not so essential. */
struct mm_timepiece
{
	/* The (almost) current monotonic time. */
	mm_timeval_t clock_value;
	/* The (almost) current real time. */
	mm_timeval_t real_clock_value;

	/* Repeat count until the corresponding time has to be asked. */
	uint32_t clock_count;
	uint32_t real_clock_count;

#if ENABLE_TIMEPIECE_TIMESTAMP
	/* CPU timestamps for the moments when the corresponding time was asked. */
	uint64_t clock_stamp;
	uint64_t real_clock_stamp;
#endif
};

void
mm_timepiece_init(void);

void NONNULL(1)
mm_timepiece_prepare(struct mm_timepiece *tp);

void NONNULL(1)
mm_timepiece_gettime_slow(struct mm_timepiece *tp);

void NONNULL(1)
mm_timepiece_getrealtime_slow(struct mm_timepiece *tp);

static inline void NONNULL(1)
mm_timepiece_reset(struct mm_timepiece *tp)
{
	tp->clock_count = 0;
	tp->real_clock_count = 0;
}

static inline mm_timeval_t NONNULL(1)
mm_timepiece_gettime(struct mm_timepiece *tp)
{
	if (tp->clock_count)
		tp->clock_count--;
	else
		mm_timepiece_gettime_slow(tp);
	return tp->clock_value;
}

static inline mm_timeval_t NONNULL(1)
mm_timepiece_getrealtime(struct mm_timepiece *tp)
{
	if (tp->real_clock_count)
		tp->real_clock_count--;
	else
		mm_timepiece_getrealtime_slow(tp);
	return tp->real_clock_value;
}

#endif /* BASE_TIMEPIECE_H */
