/*
 * memcache/result.h - MainMemory memcache results.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#ifndef MEMCACHE_RESULT_H
#define	MEMCACHE_RESULT_H

#include "memcache/memcache.h"

typedef enum
{
	MC_RESULT_NONE = 0,
#if ENABLE_MEMCACHE_DELEGATE
	MC_RESULT_FUTURE,
#endif

	MC_RESULT_BLANK,
	MC_RESULT_OK,
	MC_RESULT_END,
	MC_RESULT_ERROR,
	MC_RESULT_EXISTS,
	MC_RESULT_STORED,
	MC_RESULT_DELETED,
	MC_RESULT_TOUCHED,
	MC_RESULT_NOT_FOUND,
	MC_RESULT_NOT_STORED,
	MC_RESULT_INC_DEC_NON_NUM,
	MC_RESULT_NOT_IMPLEMENTED,
	MC_RESULT_CANCELED,
	MC_RESULT_VERSION,

	MC_RESULT_ENTRY,
	MC_RESULT_ENTRY_CAS,
	MC_RESULT_VALUE,

	MC_RESULT_QUIT,

	MC_RESULT_BINARY_QUIT,
	MC_RESULT_BINARY_NOOP,
	MC_RESULT_BINARY_UNKNOWN,

} mc_result_t;

#endif /* MEMCACHE_RESULT_H */
