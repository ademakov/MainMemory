/*
 * base/scan.h - String scanning routines.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef BASE_SCAN_H
#define BASE_SCAN_H

#include "common.h"

#include <ctype.h>

/**********************************************************************
 * Basic scanning routines.
 **********************************************************************/

static inline const char * NONNULL(1, 2)
mm_scan_skip_space(const char *sp, const char *ep)
{
	while (sp < ep && isspace(*sp))
		sp++;
	return sp;
}

/**********************************************************************
 * Integer value scanning routines.
 **********************************************************************/

const char * NONNULL(1, 3, 4)
mm_scan_u32(uint32_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_u64(uint64_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_x32(uint32_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_x64(uint64_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_n32(uint32_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_n64(uint64_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_d32(int32_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_d64(int64_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_i32(int32_t *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_i64(int64_t *vp, int *xp, const char *sp, const char *ep);

/**********************************************************************
 * Floating point value scanning routines.
 **********************************************************************/

const char * NONNULL(1, 3, 4)
mm_scan_float(float *vp, int *xp, const char *sp, const char *ep);

const char * NONNULL(1, 3, 4)
mm_scan_double(double *vp, int *xp, const char *sp, const char *ep);

/**********************************************************************
 * Human-readable value scanning routines.
 **********************************************************************/

const char * NONNULL(1, 3, 4)
mm_scan_bool(bool *vp, int *xp, const char *sp, const char *ep);

#endif /* BASE_SCAN_H */
