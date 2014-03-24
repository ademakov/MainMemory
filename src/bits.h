/*
 * bits.h - Bit manipulation.
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

#ifndef BITS_H
#define BITS_H

/* Count leading zeros (from MSB). Zero argument is not allowed. */
#define mm_clz(x)	__builtin_clz(x)

/* Count trailing zeros (from LSB). Zero argument is not allowed. */
#define mm_ctz(x)	__builtin_ctz(x)

/* For non-zero arguments just like ctz(x)+1 but for zero returns zero too. */
#define mm_ffs(x)	__builtin_ffs(x)

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

#endif /* BITS_H */
