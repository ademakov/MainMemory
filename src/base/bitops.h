/*
 * base/bitops.h - Bit operations.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef BASE_BITOPS_H
#define BASE_BITOPS_H

/* Count leading zeros (from MSB). Zero argument is not allowed. */
#define mm_clz(x) ({					\
		unsigned _r;				\
		typeof(x) _x = x;			\
		if (sizeof(_x) <= sizeof(int))		\
			_r = __builtin_clz(_x);		\
		else if (sizeof(_x) <= sizeof(long))	\
			_r = __builtin_clzl(_x);	\
		else					\
			_r = __builtin_clzll(_x);	\
		_r;					\
	})

/* Count trailing zeros (from LSB). Zero argument is not allowed. */
#define mm_ctz(x) ({					\
		unsigned _r;				\
		typeof(x) _x = x;			\
		if (sizeof(_x) <= sizeof(int))		\
			_r = __builtin_ctz(_x);		\
		else if (sizeof(_x) <= sizeof(long))	\
			_r = __builtin_ctzl(_x);	\
		else					\
			_r = __builtin_ctzll(_x);	\
		_r;					\
	})

/* For non-zero arguments just like ctz(x)+1 but for zero returns zero too. */
#define mm_ffs(x) ({					\
		unsigned _r;				\
		typeof(x) _x = x;			\
		if (sizeof(_x) <= sizeof(int))		\
			_r = __builtin_ffs(_x);		\
		else if (sizeof(_x) <= sizeof(long))	\
			_r = __builtin_ffsl(_x);	\
		else					\
			_r = __builtin_ffsll(_x);	\
		_r;					\
	})

/* Count set bits. */
#define mm_popcount(x) ({				\
		unsigned _r;				\
		typeof(x) _x = x;			\
		if (sizeof(_x) <= sizeof(int))		\
			_r = __builtin_popcount(_x);	\
		else if (sizeof(_x) <= sizeof(long))	\
			_r = __builtin_popcountl(_x);	\
		else					\
			_r = __builtin_popcountll(_x);	\
		_r;					\
	})

/* Cyclic left bit rotation. */
#define mm_rotl32(x, r) ({				\
	uint32_t _x = (uint32_t) (x);			\
	uint32_t _r = (uint32_t) (r);			\
	_x = (_x << _r) | (_x >> (32 - _r));		\
	_x;						\
})

/* Check if a number is a power of 2. */
#define mm_is_pow2(x) ({				\
		typeof(x) _x = (x);			\
		_x != 0 && (_x & (_x - 1)) == 0;	\
	})

/* Check if a number is a power of 2 or zero. */
#define mm_is_pow2z(x) ({				\
		typeof(x) _x = (x);			\
		(_x & (_x - 1)) == 0;			\
	})

/* Round down to a power of 2 multiple. */
#define mm_round_down(x, p) ({				\
		typeof(x) _x = (x);			\
		typeof(p) _p = (p);			\
		_x & ~(_p - 1);				\
	})

/* Round up to a power of 2 multiple. */
#define mm_round_up(x, p) ({				\
		typeof(x) _x = (x);			\
		typeof(p) _p = (p);			\
		(_x + _p - 1) & ~(_p - 1);		\
	})

/* Count all bits in an integer. */
#define mm_nbits(x) ({						\
		unsigned _r;					\
		if (sizeof(typeof(x)) <= sizeof(int))		\
			_r = 8 * sizeof(int);			\
		else if (sizeof(typeof(x)) <= sizeof(long))	\
			_r = 8 * sizeof(long);			\
		else						\
			_r = 8 * sizeof(long long);		\
		_r;						\
	})

/* Round down to a nearest power of 2. */
#define mm_lower_pow2(x) ({					\
		typeof(x) __x = x;				\
		typeof(x) __r = 1;				\
		((__x < 2) ? __x :				\
		 __r << (mm_nbits(__x) - 1 - mm_clz(__x)));	\
	})

/* Round up to a nearest power of 2. */
#define mm_upper_pow2(x) ({					\
		typeof(x) __x = x;				\
		typeof(x) __r = 1;				\
		((__x < 2) ? __x :				\
		 __r << (mm_nbits(__x) - mm_clz(__x - 1)));	\
	})

#endif /* BASE_BITOPS_H */
