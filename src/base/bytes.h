/*
 * base/bytes.h - Byte order operations.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#include "config.h"
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

/**********************************************************************
 * Byte-order conversion.
 **********************************************************************/

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

#else /* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

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

#endif /* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

/* Conversion from network order to host order. */
#define mm_ntohs(x)	mm_betoh16(x)
#define mm_ntohl(x)	mm_betoh32(x)
#define mm_ntohll(x)	mm_betoh64(x)

/* Conversion from host order to network order. */
#define mm_htons(x)	mm_htobe16(x)
#define mm_htonl(x)	mm_htobe32(x)
#define mm_htonll(x)	mm_htobe64(x)

/**********************************************************************
 * Loading from unaligned memory.
 **********************************************************************/

static inline uint16_t
mm_load_be16(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint16_t *v = p;
	return mm_betoh16(*v);
#else
	uint8_t *b = p;
	return ((uint16_t) b[0] << 8) | (uint16_t) b[1];
#endif
}

static inline uint32_t
mm_load_be32(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint32_t *v = p;
	return mm_betoh32(*v);
#else
	uint8_t *b = p;
	return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | (uint32_t) b[3];
#endif
}

static inline uint64_t
mm_load_be64(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint64_t *v = p;
	return mm_betoh64(*v);
#else
	uint8_t *b = p;
	return (((uint64_t) b[0] << 56) | ((uint64_t) b[1] << 48) | ((uint64_t) b[2] << 40) | ((uint64_t) b[3] << 32)
		| ((uint64_t) b[4] << 24) | ((uint64_t) b[5] << 16) | ((uint64_t) b[6] << 8) | (uint64_t) b[7]);
#endif
}

static inline uint16_t
mm_load_le16(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint16_t *v = p;
	return *v;
#else
	uint8_t *b = p;
	return (uint16_t) b[0] | ((uint16_t) b[1] << 8);
#endif
}

static inline uint32_t
mm_load_le32(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint32_t *v = p;
	return *v;
#else
	uint8_t *b = p;
	return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
#endif
}

static inline uint64_t
mm_load_le64(void *p)
{
#if ARCH_X86 || ARCH_X86_64
	uint64_t *v = p;
	return *v;
#else
	uint8_t *b = p;
	return ((uint64_t) b[0] | ((uint64_t) b[1] << 8) | ((uint64_t) b[2] << 16) | ((uint64_t) b[3] << 24)
		| ((uint64_t) b[4] << 32) | ((uint64_t) b[5] << 40) | ((uint64_t) b[6] << 48) | ((uint64_t) b[7] << 56));
#endif
}

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

# define mm_load_h16(x)	mm_load_be16(x)
# define mm_load_h32(x)	mm_load_be32(x)
# define mm_load_h64(x)	mm_load_be64(x)

#else /* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

# define mm_load_h16(x)	mm_load_le16(x)
# define mm_load_h32(x)	mm_load_le32(x)
# define mm_load_h64(x)	mm_load_le64(x)

#endif /* __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__ */

/* Load a value from bytes in network order. */
#define mm_load_ns(x)	mm_load_be16(x)
#define mm_load_nl(x)	mm_load_be32(x)
#define mm_load_nll(x)	mm_load_be64(x)

/* Load a value from bytes in host order. */
#define mm_load_hs(x)	mm_load_h16(x)
#define mm_load_hl(x)	mm_load_h32(x)
#define mm_load_hll(x)	mm_load_h64(x)

#endif /* BASE_BYTES_H */
