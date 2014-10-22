/*
 * base/list.h - MainMemory lists.
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

#ifndef BASE_LIST_H
#define BASE_LIST_H

#include "common.h"
#include "arch/atomic.h"
#include "arch/memory.h"

/**********************************************************************
 * Double-linked circular list.
 **********************************************************************/

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
mm_list_is_head(struct mm_list *list, struct mm_list *item)
{
	return list->next == item;
}

static inline bool
mm_list_is_tail(struct mm_list *list, struct mm_list *item)
{
	return list->prev == item;
}

static inline bool
mm_list_empty(struct mm_list *list)
{
	return mm_list_is_tail(list, list);
}

static inline void
mm_list_splice(struct mm_list *item, struct mm_list *head, struct mm_list *tail)
{
	head->prev = item;
	tail->next = item->next;
	item->next->prev = tail;
	item->next = head;
}

static inline void
mm_list_insert(struct mm_list *item, struct mm_list *item2)
{
	mm_list_splice(item, item2, item2);
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
mm_list_insert_prev(struct mm_list *item, struct mm_list *item2)
{
	mm_list_splice_prev(item, item2, item2);
}

static inline void
mm_list_append(struct mm_list *list, struct mm_list *item)
{
	mm_list_splice_prev(list, item, item);
}

static inline void
mm_list_cleave(struct mm_list *prev, struct mm_list *next)
{
	prev->next = next;
	next->prev = prev;
}

static inline void
mm_list_delete(struct mm_list *item)
{
	mm_list_cleave(item->prev, item->next);
}

static inline struct mm_list *
mm_list_delete_head(struct mm_list *list)
{
	struct mm_list *head = mm_list_head(list);
	mm_list_delete(head);
	return head;
}

static inline struct mm_list *
mm_list_delete_tail(struct mm_list *list)
{
	struct mm_list *tail = mm_list_tail(list);
	mm_list_delete(tail);
	return tail;
}

/**********************************************************************
 * Single-linked list.
 **********************************************************************/

struct mm_link
{
	struct mm_link *next;
};

static inline void
mm_link_init(struct mm_link *list)
{
	list->next = NULL;
}

static inline struct mm_link *
mm_link_head(struct mm_link *list)
{
	return list->next;
}

static inline bool
mm_link_is_last(struct mm_link *item)
{
	return item->next == NULL;
}

static inline bool
mm_link_empty(struct mm_link *list)
{
	return mm_link_is_last(list);
}

static inline void
mm_link_splice(struct mm_link *item, struct mm_link *head, struct mm_link *tail)
{
	tail->next = item->next;
	item->next = head;
}

static inline void
mm_link_insert(struct mm_link *item, struct mm_link *item2)
{
	mm_link_splice(item, item2, item2);
}

static inline void
mm_link_cleave(struct mm_link *prev, struct mm_link *next)
{
	prev->next = next;
}

static inline void
mm_link_delete_next(struct mm_link *item)
{
	mm_link_cleave(item, item->next->next);
}

static inline struct mm_link *
mm_link_delete_head(struct mm_link *list)
{
	struct mm_link *head = mm_link_head(list);
	mm_link_delete_next(list);
	return head;
}

static inline struct mm_link *
mm_link_shared_head(struct mm_link *list)
{
	return mm_memory_load(list->next);
}

static inline struct mm_link *
mm_link_cas_head(struct mm_link *list, struct mm_link *head, struct mm_link *item)
{
	void **headp = (void **) &list->next;
	return mm_atomic_ptr_cas(headp, head, item);
}

/**********************************************************************
 * Single-linked list with FIFO support.
 **********************************************************************/

#define MM_QUEUE_INIT(queue) queue = { { NULL }, &queue.head };

struct mm_queue
{
	struct mm_link head;
	struct mm_link *tail;
};

static inline void
mm_queue_init(struct mm_queue *list)
{
	list->head.next = NULL;
	list->tail = &list->head;
}

static inline struct mm_link *
mm_queue_head(struct mm_queue *list)
{
	return list->head.next;
}

static inline struct mm_link *
mm_queue_tail(struct mm_queue *list)
{
	return list->tail;
}

static inline bool
mm_queue_is_last(struct mm_link *item)
{
	return item->next == NULL;
}

static inline bool
mm_queue_empty(struct mm_queue *list)
{
	return mm_queue_is_last(&list->head);
}

static inline void
mm_queue_splice_head(struct mm_queue *list, struct mm_link *head, struct mm_link *tail)
{
	tail->next = list->head.next;
	if (list->head.next == NULL)
		list->tail = tail;
	list->head.next = head;
}

static inline void
mm_queue_insert_head(struct mm_queue *list, struct mm_link *item)
{
	mm_queue_splice_head(list, item, item);
}

static inline void
mm_queue_splice_tail(struct mm_queue *list, struct mm_link *head, struct mm_link *tail)
{
	tail->next = NULL;
	list->tail->next = head;
	list->tail = tail;
}

static inline void
mm_queue_append(struct mm_queue *list, struct mm_link *item)
{
	mm_queue_splice_tail(list, item, item);
}

static inline struct mm_link *
mm_queue_delete_head(struct mm_queue *list)
{
	struct mm_link *head = mm_queue_head(list);
	list->head.next = head->next;
	if (head->next == NULL)
		list->tail = &list->head;
	return head;
}

#endif /* BASE_LIST_H */
