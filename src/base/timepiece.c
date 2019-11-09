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

void
mm_timepiece_init(void)
{
}

void NONNULL(1)
mm_timepiece_prepare(struct mm_timepiece *tp)
{
#if ENABLE_TIMEPIECE_TIMESTAMP
	tp->clock_stamp = 0;
	tp->real_clock_stamp = 0;
#else
	tp->clock_count = 0;
	tp->real_clock_count = 0;
#endif
}
