/*
 * list.h - MainMemory lists.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LIST_H
#define LIST_H

#include "common.h"

struct mm_list
{
	struct mm_list *next;
	struct mm_list *prev;
};

static inline void
mm_list_init(struct mm_list *list)
{
	list->next = list->prev = list;
}

static inline struct mm_list *
mm_list_head(struct mm_list *list)
{
	return list->next;
}

static inline struct mm_list *
mm_list_tail(struct mm_list *list)
{
	return list->prev;
}

static inline bool
mm_list_has_next(struct mm_list *list, struct mm_list *item)
{
	return item->next != list;
}

static inline bool
mm_list_has_prev(struct mm_list *list, struct mm_list *item)
{
	return item->prev != list;
}

static inline bool
mm_list_is_empty(struct mm_list *list)
{
	return mm_list_has_next(list, list);
}

static inline void
mm_list_insert(struct mm_list *list, struct mm_list *item)
{
	item->next = list->next;
	list->next->prev = item;
	item->prev = list;
	list->next = item;
}

static inline void
mm_list_insert_tail(struct mm_list *list, struct mm_list *item)
{
	item->prev = list->prev;
	list->prev->next = item;
	item->next = list;
	list->prev = item;
}

static inline void
mm_list_delete(struct mm_list *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}

#endif /* LIST_H */
