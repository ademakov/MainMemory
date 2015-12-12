/*
 * base/scan.c - String scanning routines.
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

#include "base/scan.h"

#include <stdlib.h>

/**********************************************************************
 * Digit table.
 **********************************************************************/

static uint8_t mm_scan_dtab[256] = {
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	 0,  1,  2,  3,    4,  5,  6,  7,    8,  9, -1, -1,   -1, -1, -1, -1,

	-1, 10, 11, 12,   13, 14, 15, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, 10, 11, 12,   13, 14, 15, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,

	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,

	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
	-1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,   -1, -1, -1, -1,
};

static inline uint8_t
mm_scan_digit(const char *sp, const char *ep)
{
	return sp < ep ? mm_scan_dtab[(uint8_t) *sp] : -1;
}

/**********************************************************************
 * Integer value scanning routines.
 **********************************************************************/

#define MM_SCAN_DIGITS(S, b, n, m, off)					\
	do {								\
		const uint##n##_t t = ((uint##n##_t) m) / b;		\
		const uint8_t r = ((uint##n##_t) m) % b;		\
		uint8_t d = mm_scan_digit(sp + off, ep);		\
		if (d >= b) {						\
			if (xp != NULL)					\
				*xp = EINVAL;				\
			return sp;					\
		}							\
		sp += off;						\
		uint##n##_t v = d;					\
		while ((d = mm_scan_digit(++sp, ep)) < b) {		\
			if (v >= t && (d > r || v > t)) {		\
				while (mm_scan_digit(++sp, ep) < b)	\
					/* skip */;			\
				if (xp != NULL)				\
					*xp = ERANGE;			\
				v = m;					\
				break;					\
			}						\
			v = v * b + d;					\
		}							\
		*vp = S(v);						\
		return sp;						\
	} while(0)

#define MM_SCAN_NATURAL(n, m)						\
	do {								\
		if (unlikely(sp >= ep)) {				\
			if (xp != NULL)					\
				*xp = EINVAL;				\
			return sp;					\
		}							\
		switch (*sp) {						\
		case '0':						\
			if ((sp + 2) < ep && (sp[1] | 0x20) == 'x'	\
			    && mm_scan_dtab[(uint8_t) sp[2]] < 16) {	\
				MM_SCAN_DIGITS(+, 16, n, m, 2);		\
			} else {					\
		default:						\
				MM_SCAN_DIGITS(+, 10, n, m, 0);		\
			}						\
		}							\
	} while(0)

#define MM_SCAN_DECIMAL(n, mn, mp)					\
	do {								\
		if (unlikely(sp >= ep)) {				\
			if (xp != NULL)					\
				*xp = EINVAL;				\
			return sp;					\
		}							\
		switch (*sp) {						\
		case '-':						\
			MM_SCAN_DIGITS(-, 10, n, mn, 1);		\
		case '+':						\
			MM_SCAN_DIGITS(+, 10, n, mp, 1);		\
		default:						\
			MM_SCAN_DIGITS(+, 10, n, mp, 0);		\
		}							\
	} while(0)

#define MM_SCAN_INTEGER(n, mn, mp, mx)					\
	do {								\
		if (unlikely(sp >= ep)) {				\
			if (xp != NULL)					\
				*xp = EINVAL;				\
			return sp;					\
		}							\
		switch (*sp) {						\
		case '-':						\
			MM_SCAN_DIGITS(-, 10, n, mn, 1);		\
		case '+':						\
			MM_SCAN_DIGITS(+, 10, n, mp, 1);		\
		case '0':						\
			if ((sp + 2) < ep && (sp[1] | 0x20) == 'x'	\
			    && mm_scan_dtab[(uint8_t) sp[2]] < 16) {	\
				MM_SCAN_DIGITS(+, 16, n, mx, 2);	\
			} else {					\
		default:						\
				MM_SCAN_DIGITS(+, 10, n, mp, 0);	\
			}						\
		}							\
	} while(0)

const char * NONNULL(1, 3, 4)
mm_scan_u32(uint32_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DIGITS(+, 10, 32, UINT32_MAX, 0);
}

const char * NONNULL(1, 3, 4)
mm_scan_u64(uint64_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DIGITS(+, 10, 64, UINT64_MAX, 0);
}

const char * NONNULL(1, 3, 4)
mm_scan_x32(uint32_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DIGITS(+, 16, 32, UINT32_MAX, 0);
}

const char * NONNULL(1, 3, 4)
mm_scan_x64(uint64_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DIGITS(+, 16, 64, UINT64_MAX, 0);
}

const char * NONNULL(1, 3, 4)
mm_scan_n32(uint32_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_NATURAL(32, UINT32_MAX);
}

const char * NONNULL(1, 3, 4)
mm_scan_n64(uint64_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_NATURAL(64, UINT64_MAX);
}

const char * NONNULL(1, 3, 4)
mm_scan_d32(int32_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DECIMAL(32, INT32_MIN, INT32_MAX);
}

const char * NONNULL(1, 3, 4)
mm_scan_d64(int64_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_DECIMAL(64, INT64_MIN, INT64_MAX);
}

const char * NONNULL(1, 3, 4)
mm_scan_i32(int32_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_INTEGER(32, INT32_MIN, INT32_MAX, UINT32_MAX);
}

const char * NONNULL(1, 3, 4)
mm_scan_i64(int64_t *vp, int *xp, const char *sp, const char *ep)
{
	MM_SCAN_INTEGER(64, INT64_MIN, INT64_MAX, UINT64_MAX);
}

/**********************************************************************
 * Floating point value scanning routines.
 **********************************************************************/

const char * NONNULL(1, 3, 4)
mm_scan_float(float *vp, int *xp, const char *sp, const char *ep)
{
	int dummy;
	if (xp == NULL)
		xp = &dummy;

	if (unlikely(sp >= ep)) {
		*xp = EINVAL;
		return sp;
	}

	float v = 0;

	// TODO
	*xp = ENOTSUP;

	*vp = v;
	return sp;
}

const char * NONNULL(1, 3, 4)
mm_scan_double(double *vp, int *xp, const char *sp, const char *ep)
{
	int dummy;
	if (xp == NULL)
		xp = &dummy;

	if (unlikely(sp >= ep)) {
		*xp = EINVAL;
		return sp;
	}

	double v = 0;

	// TODO
	*xp = ENOTSUP;

	*vp = v;
	return sp;
}

/**********************************************************************
 * Boolean value scanning routine.
 **********************************************************************/

#define U(x)		((unsigned char) (x))
#define Cx1(a)		(U(a) | 0x20)
#define Cx2(a, b)	(U(a) | (U(b) << 8) | 0x2020)
#define Cx3(a, b, c)	(U(a) | (U(b) << 8) | (U(c) << 16) | 0x202020)
#define Cx4(a, b, c, d)	(U(a) | (U(b) << 8) | (U(c) << 16) | (U(d) << 24) | 0x20202020)

const char * NONNULL(1, 3, 4)
mm_scan_bool(bool *vp, int *xp, const char *sp, const char *ep)
{
	int dummy;
	if (xp == NULL)
		xp = &dummy;

	if (unlikely(sp >= ep)) {
		*xp = EINVAL;
		return sp;
	}

	size_t n = ep - sp;
	switch (*sp) {
	case 't':
		if (n < 4 || Cx3(sp[1], sp[2], sp[3]) != Cx3('r', 'u', 'e')) {
			*xp = EINVAL;
		} else {
			*vp = true;
			sp += 4;
		}
		break;

	case 'f':
		if (n < 5 || Cx4(sp[1], sp[2], sp[3], sp[4]) != Cx4('a', 'l', 's', 'e')) {
			*xp = EINVAL;
		} else {
			*vp = false;
			sp += 5;
		}
		break;

	case 'y':
		if (n < 3 || Cx2(sp[1], sp[2]) != Cx2('e', 's')) {
			*xp = EINVAL;
		} else {
			*vp = true;
			sp += 3;
		}
		break;

	case 'n':
		if (n < 2 || Cx1(sp[1]) != Cx1('o')) {
			*xp = EINVAL;
		} else {
			*vp = false;
			sp += 2;
		}
		break;

	case 'o':
		if (n < 2)
			break;
		if (Cx1(sp[1]) != Cx1('n')) {
			if (n < 3 || Cx2(sp[1], sp[2]) != Cx2('f', 'f')) {
				*xp = EINVAL;
			} else {
				*vp = false;
				sp += 3;
			}
		} else {
			*vp = true;
			sp += 2;
		}
		break;

	default:
		if (isdigit(*sp) || *sp == '+' || *sp == '-') {
			int64_t v;
			int x = 0;
			sp = mm_scan_i64(&v, &x, sp, ep);
			if (!x)
				*vp = (v != 0);
			else
				*xp = x;
			return sp;
		} else {
			*xp = EINVAL;
		}
		break;
	}

	return sp;
}

#undef U
#undef Cx1
#undef Cx2
#undef Cx3
#undef Cx4
