/*
 * base/timepiece.c - MainMemory internal clock.
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

#include "base/timepiece.h"

#include "base/clock.h"
#include "base/report.h"

#include <sys/time.h>

#if ENABLE_TIMEPIECE_TIMESTAMP
# define MM_TIMEPIECE_COUNT		(25)
#else
# define MM_TIMEPIECE_COUNT		(250)
#endif

#if ENABLE_TIMEPIECE_TIMESTAMP

#define MM_TIMEPIECE_DELTA_USEC		(2000)

#define MM_TIMEPIECE_RETRY_LOG_STEP	(50)
#define MM_TIMEPIECE_RETRY_LIMIT	(1000)

static uint64_t mm_timepiece_delta;

static uint64_t
mm_timepiece_probe(uint64_t *usec, uint32_t *cpu)
{
	struct timeval tv;
	int rc = gettimeofday(&tv, NULL);
	uint64_t tsc = mm_cpu_tscp(cpu);
	if (rc < 0)
		mm_fatal(errno, "gettimeofday()");
	*usec = 1000 * 1000 * (uint64_t) tv.tv_sec + tv.tv_usec;
	return tsc;
}

static uint64_t
mm_timepiece_gauge(void)
{
	uint64_t start_usec;
	uint32_t start_cpu;
	uint64_t start_tsc = mm_timepiece_probe(&start_usec, &start_cpu);
	for (;;) {
		uint64_t usec;
		uint32_t cpu;
		uint64_t tsc = mm_timepiece_probe(&usec, &cpu);
		uint64_t delta_usec = usec - start_usec;
		if (delta_usec >= MM_TIMEPIECE_DELTA_USEC) {
			if (cpu != start_cpu)
				return 0;
			uint64_t delta_tsc = tsc - start_tsc;
			return MM_TIMEPIECE_DELTA_USEC * delta_tsc / delta_usec;
		}
	}
}

#endif

void
mm_timepiece_init(void)
{
#if ENABLE_TIMEPIECE_TIMESTAMP
	uint32_t count = 0;
	uint64_t prev_delta = 0;
	for (;;) {
		uint64_t delta = mm_timepiece_gauge();
		if (delta == 0) {
			if ((++count % MM_TIMEPIECE_RETRY_LOG_STEP) == 0) {
				if (count == MM_TIMEPIECE_RETRY_LOG_STEP)
					mm_warning(0, "hmm, it takes unusually long to calibrate TSC");
				else if (count < MM_TIMEPIECE_RETRY_LIMIT)
					mm_warning(0, "...still trying to calibrate TSC");
				else
					mm_fatal(0, "...failed to calibrate TSC");
			}
			continue;
		}

		uint64_t delta2 = delta > prev_delta ? delta - prev_delta : prev_delta - delta;
		if (delta2 <= (delta / 100)) {
			mm_verbose("TSC calibration: %llu ticks per %u microseconds",
				   (unsigned long long) delta, MM_TIMEPIECE_DELTA_USEC);
			mm_timepiece_delta = delta;
			break;
		}
		prev_delta = delta;
	}
#endif
}

void NONNULL(1)
mm_timepiece_prepare(struct mm_timepiece *tp)
{
	tp->clock_count = 0;
	tp->real_clock_count = 0;

#if ENABLE_TIMEPIECE_TIMESTAMP
	tp->clock_stamp = 0;
	tp->real_clock_stamp = 0;
#endif
}

void NONNULL(1)
mm_timepiece_gettime_slow(struct mm_timepiece *tp)
{
	tp->clock_count = MM_TIMEPIECE_COUNT;

#if ENABLE_TIMEPIECE_TIMESTAMP
	uint64_t stamp = mm_cpu_tsc();
	if ((tp->clock_stamp + mm_timepiece_delta) <= stamp)
	{
		tp->clock_stamp = stamp;
		tp->clock_value = mm_clock_gettime_monotonic_coarse();
		TRACE("%lld", (long long) tp->clock_value);
	}
#else
	tp->clock_value = mm_clock_gettime_monotonic_coarse();
	TRACE("%lld", (long long) tp->clock_value);
#endif
}

void NONNULL(1)
mm_timepiece_getrealtime_slow(struct mm_timepiece *tp)
{
	tp->real_clock_count = MM_TIMEPIECE_COUNT;

#if ENABLE_TIMEPIECE_TIMESTAMP
	uint64_t stamp = mm_cpu_tsc();
	if ((tp->real_clock_stamp + mm_timepiece_delta) <= stamp)
	{
		tp->real_clock_stamp = stamp;
		tp->real_clock_value = mm_clock_gettime_realtime_coarse();
		TRACE("%lld", (long long) tp->real_clock_value);
	}
#else
	tp->real_clock_value = mm_clock_gettime_realtime_coarse();
	TRACE("%lld", (long long) tp->real_clock_value);
#endif
}
