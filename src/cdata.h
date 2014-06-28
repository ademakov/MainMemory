/*
 * cdata.h - MainMemory core-local data.
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

#ifndef CDATA_H
#define CDATA_H

#include "common.h"

#define MM_CDATA_CHUNK_SIZE	MM_PAGE_SIZE

#define MM_CDATA(type, name) union { type *ptr; mm_cdata_t ref; } name

#define MM_CDATA_ALLOC(name, data) ({					\
		data.ref = mm_cdata_alloc(name, sizeof(*data.ptr));	\
	})

#define MM_CDATA_DEREF(core, data) ({					\
		size_t off = (core) * MM_CDATA_CHUNK_SIZE;		\
		(typeof(data.ptr)) (data.ref + off);			\
	})

typedef uintptr_t mm_cdata_t;

void mm_cdata_init(void);
void mm_cdata_term(void);

mm_cdata_t mm_cdata_alloc(const char *name, size_t size);

void mm_cdata_summary(void);

#endif /* CDATA_H */
