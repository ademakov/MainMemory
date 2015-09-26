/*
 * base/bytes.h - Byte order operations.
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

#ifndef BASE_BYTES_H
#define BASE_BYTES_H

#include <stdint.h>

#ifndef __BYTE_ORDER__
# error "Missing predefined compiler macro __BYTE_ORDER__."
#endif
#ifndef __ORDER_BIG_ENDIAN__
# error "Missing predefined compiler macro __ORDER_BIG_ENDIAN__."
#endif
#ifndef __ORDER_LITTLE_ENDIAN__
# error "Missing predefined compiler macro __ORDER_LITTLE_ENDIAN__."
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
# error "Unsupported byte order."
#endif

static inline uint16_t
mm_bswap16(uint16_t x)
{
	return __builtin_bswap16(x);
}

static inline uint32_t
mm_bswap32(uint32_t x)
{
	return __builtin_bswap32(x);
}

static inline uint64_t
mm_bswap64(uint64_t x)
{
	return __builtin_bswap64(x);
}

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

# define mm_betoh16(x)	(x)
# define mm_betoh32(x)	(x)
# define mm_betoh64(x)	(x)

# define mm_htobe16(x)	(x)
# define mm_htobe32(x)	(x)
# define mm_htobe64(x)	(x)

# define mm_letoh16(x)	mm_bswap16(x)
# define mm_letoh32(x)	mm_bswap32(x)
# define mm_letoh64(x)	mm_bswap64(x)

# define mm_htole16(x)	mm_bswap16(x)
# define mm_htole32(x)	mm_bswap32(x)
# define mm_htole64(x)	mm_bswap64(x)

#else

# define mm_betoh16(x)	mm_bswap16(x)
# define mm_betoh32(x)	mm_bswap32(x)
# define mm_betoh64(x)	mm_bswap64(x)

# define mm_htobe16(x)	mm_bswap16(x)
# define mm_htobe32(x)	mm_bswap32(x)
# define mm_htobe64(x)	mm_bswap64(x)

# define mm_letoh16(x)	(x)
# define mm_letoh32(x)	(x)
# define mm_letoh64(x)	(x)

# define mm_htole16(x)	(x)
# define mm_htole32(x)	(x)
# define mm_htole64(x)	(x)

#endif

#define mm_ntohs(x)	mm_betoh16(x)
#define mm_ntohl(x)	mm_betoh32(x)
#define mm_ntohll(x)	mm_betoh64(x)

#define mm_htons(x)	mm_htobe16(x)
#define mm_htonl(x)	mm_htobe32(x)
#define mm_htonll(x)	mm_htobe64(x)

#endif /* BASE_BYTES_H */
