/*
 * pool.h - MainMemory memory pools.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#ifndef POOL_H
#define POOL_H

#include "common.h"

#define MM_POOL_INDEX_INVALID	0xffffffff


struct mm_pool
{
	struct mm_pool_free_item *free_list;
	char *block_cur_ptr;
	char *block_end_ptr;
	char **block_array;

	uint32_t item_size;
	uint32_t item_last;
	uint32_t block_capacity;
	uint32_t block_array_used;
	uint32_t block_array_size;

	char *pool_name;
};


void mm_pool_init(struct mm_pool *pool, const char *name, uint32_t item_size)
	__attribute__((nonnull(1, 2)));

void mm_pool_discard(struct mm_pool *pool)
	__attribute__((nonnull(1)));

void * mm_pool_idx2ptr(struct mm_pool *pool, uint32_t index)
	__attribute__((nonnull(1)));

uint32_t mm_pool_ptr2idx(struct mm_pool *pool, void *item)
	__attribute__((nonnull(1, 2)));

void * mm_pool_alloc(struct mm_pool *pool)
	__attribute__((nonnull(1)));

void mm_pool_free(struct mm_pool *pool, void *item)
	__attribute__((nonnull(1, 2)));

#endif /* POOL_H */
