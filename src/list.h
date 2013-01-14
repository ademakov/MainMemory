/*
 * list.h - MainMemory lists.
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
mm_list_empty(struct mm_list *list)
{
	return !mm_list_has_next(list, list);
}

static inline void
mm_list_splice_next(struct mm_list *item, struct mm_list *head, struct mm_list *tail)
{
	head->prev = item;
	tail->next = item->next;
	item->next->prev = tail;
	item->next = head;
}

static inline void
mm_list_splice_prev(struct mm_list *item, struct mm_list *head, struct mm_list *tail)
{
	tail->next = item;
	head->prev = item->prev;
	item->prev->next = head;
	item->prev = tail;
}

static inline void
mm_list_cleave(struct mm_list *head, struct mm_list *tail)
{
	struct mm_list *prev = head->prev;
	struct mm_list *next = tail->next;
	prev->next = next;
	next->prev = prev;
}

static inline void
mm_list_insert(struct mm_list *list, struct mm_list *item)
{
	mm_list_splice_next(list, item, item);
}

static inline void
mm_list_append(struct mm_list *list, struct mm_list *item)
{
	mm_list_splice_prev(list, item, item);
}

static inline void
mm_list_delete(struct mm_list *item)
{
	mm_list_cleave(item, item);
}

#endif /* LIST_H */
