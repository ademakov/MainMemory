/*
 * value.h - MainMemory common values.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef VALUE_H
#define VALUE_H

#include "common.h"

/*
 * Task and future special result codes.
 */

/* The result is unavailable as the task/future has been canceled. */
#define MM_RESULT_CANCELED	((mm_value_t) -1)

/* The result is unavailable as the task/future is still running. */
#define MM_RESULT_NOTREADY	((mm_value_t) -2)

/* The result is unavailable as the future has not yet started. */
#define MM_RESULT_DEFERRED	((mm_value_t) -3)

/* The result is unavailable as not needed in the first place. */
#define MM_RESULT_UNWANTED	((mm_value_t) -4)

#endif /* VALUE_H */
