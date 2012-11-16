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

#define MM_LIST_ENTRY(list, type, field) \
	((type *) ((char *)(list) - offsetof(type, field)))

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

static inline int
mm_list_is_empty(struct mm_list *list)
{
	return (list == list->next);
}

static inline void
mm_list_delete(struct mm_list *elem)
{
	elem->next->prev = elem->prev;
	elem->prev->next = elem->next;
}

static inline void
mm_list_insert(struct mm_list *list, struct mm_list *elem)
{
	elem->next = list->next;
	list->next->prev = elem;
	elem->prev = list;
	list->next = elem;
}

#endif /* LIST_H */
