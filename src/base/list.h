/*
 * base/list.h - MainMemory lists.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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
#include "base/atomic.h"
#include <stdatomic.h>

/**********************************************************************
 * Double-linked circular list.
 **********************************************************************/

#define MM_LIST_INIT(list) list = { { &list.base, &list.base } };

struct mm_link
{
	struct mm_link *next;
	struct mm_link *prev;
};

struct mm_list
{
	struct mm_link base;
};

static inline void
mm_list_prepare(struct mm_list *list)
{
	list->base.next = list->base.prev = &list->base;
}

static inline const struct mm_link *
mm_list_stub(const struct mm_list *list)
{
	return &list->base;
}

static inline struct mm_link *
mm_list_head(struct mm_list *list)
{
	return list->base.next;
}

static inline struct mm_link *
mm_list_tail(struct mm_list *list)
{
	return list->base.prev;
}

static inline bool
mm_list_is_head(const struct mm_list *list, const struct mm_link *item)
{
	return list->base.next == item;
}

static inline bool
mm_list_is_tail(const struct mm_list *list, const struct mm_link *item)
{
	return list->base.prev == item;
}

static inline bool
mm_list_empty(struct mm_list *list)
{
	return mm_list_head(list) == mm_list_stub(list);
}

static inline void
mm_list_splice_next(struct mm_link *item, struct mm_link *head, struct mm_link *tail)
{
	head->prev = item;
	tail->next = item->next;
	item->next->prev = tail;
	item->next = head;
}

static inline void
mm_list_splice_prev(struct mm_link *item, struct mm_link *head, struct mm_link *tail)
{
	tail->next = item;
	head->prev = item->prev;
	item->prev->next = head;
	item->prev = tail;
}

static inline void
mm_list_insert_next(struct mm_link *item, struct mm_link *item2)
{
	mm_list_splice_next(item, item2, item2);
}

static inline void
mm_list_insert_prev(struct mm_link *item, struct mm_link *item2)
{
	mm_list_splice_prev(item, item2, item2);
}

static inline void
mm_list_insert(struct mm_list *list, struct mm_link *item)
{
	mm_list_insert_next(&list->base, item);
}

static inline void
mm_list_append(struct mm_list *list, struct mm_link *item)
{
	mm_list_insert_prev(&list->base, item);
}

static inline void
mm_list_cleave(struct mm_link *prev, struct mm_link *next)
{
	prev->next = next;
	next->prev = prev;
}

static inline void
mm_list_delete(struct mm_link *item)
{
	mm_list_cleave(item->prev, item->next);
}

static inline struct mm_link *
mm_list_remove_head(struct mm_list *list)
{
	struct mm_link *head = mm_list_head(list);
	mm_list_delete(head);
	return head;
}

static inline struct mm_link *
mm_list_remove_tail(struct mm_list *list)
{
	struct mm_link *tail = mm_list_tail(list);
	mm_list_delete(tail);
	return tail;
}

/**********************************************************************
 * Single-linked list with LIFO discipline (stack).
 **********************************************************************/

struct mm_slink
{
	struct mm_slink *next;
};

struct mm_stack
{
	struct mm_slink head;
};

static inline void
mm_stack_prepare(struct mm_stack *list)
{
	list->head.next = NULL;
}

static inline void
mm_slink_prepare(struct mm_slink *item)
{
	item->next = NULL;
}

static inline struct mm_slink *
mm_stack_head(struct mm_stack *list)
{
	return list->head.next;
}

static inline bool
mm_stack_is_tail(const struct mm_slink *item)
{
	return item->next == NULL;
}

static inline bool
mm_stack_empty(const struct mm_stack *list)
{
	return mm_stack_is_tail(&list->head);
}

static inline void
mm_stack_insert_span(struct mm_stack *list, struct mm_slink *head, struct mm_slink *tail)
{
	tail->next = list->head.next;
	list->head.next = head;
}

static inline void
mm_stack_insert(struct mm_stack *list, struct mm_slink *item)
{
	mm_stack_insert_span(list, item, item);
}

static inline void
mm_stack_remove_next(struct mm_slink *item)
{
	item->next = item->next->next;
}

static inline struct mm_slink *
mm_stack_remove(struct mm_stack *list)
{
	struct mm_slink *head = mm_stack_head(list);
	mm_stack_remove_next(&list->head);
	return head;
}

static inline struct mm_slink *
mm_stack_atomic_load_head(struct mm_stack *list)
{
	return mm_memory_load(list->head.next);
}

static inline struct mm_slink *
mm_stack_atomic_cas_head(struct mm_stack *list, struct mm_slink *head, struct mm_slink *item)
{
	void **headp = (void **) &list->head.next;
	return mm_atomic_ptr_cas(headp, head, item);
}

/**********************************************************************
 * Single-linked list with FIFO discipline support.
 **********************************************************************/

#define MM_QUEUE_INIT(queue) queue = { .head = { NULL }, .tail = &queue.head };

struct mm_qlink
{
	struct mm_qlink *next;
};

struct mm_queue
{
	struct mm_qlink head;
	struct mm_qlink *tail;
};

static inline void
mm_queue_prepare(struct mm_queue *list)
{
	list->head.next = NULL;
	list->tail = &list->head;
}

static inline void
mm_qlink_prepare(struct mm_qlink *item)
{
	item->next = NULL;
}

static inline struct mm_qlink *
mm_queue_head(struct mm_queue *list)
{
	return list->head.next;
}

static inline struct mm_qlink *
mm_queue_tail(struct mm_queue *list)
{
	return list->tail;
}

static inline bool
mm_queue_is_tail(const struct mm_qlink *item)
{
	return item->next == NULL;
}

static inline bool
mm_queue_empty(const struct mm_queue *list)
{
	return mm_queue_is_tail(&list->head);
}

static inline void
mm_queue_append_span(struct mm_queue *list, struct mm_qlink *head, struct mm_qlink *tail)
{
	tail->next = NULL;
	list->tail->next = head;
	list->tail = tail;
}

static inline void
mm_queue_prepend_span(struct mm_queue *list, struct mm_qlink *head, struct mm_qlink *tail)
{
	tail->next = list->head.next;
	if (list->head.next == NULL)
		list->tail = tail;
	list->head.next = head;
}

static inline void
mm_queue_append(struct mm_queue *list, struct mm_qlink *item)
{
	mm_queue_append_span(list, item, item);
}

static inline void
mm_queue_prepend(struct mm_queue *list, struct mm_qlink *item)
{
	mm_queue_prepend_span(list, item, item);
}

static inline struct mm_qlink *
mm_queue_remove(struct mm_queue *list)
{
	struct mm_qlink *head = mm_queue_head(list);
	list->head.next = head->next;
	if (head->next == NULL)
		list->tail = &list->head;
	return head;
}

/**********************************************************************
 * MPSC concurrent queue.
 **********************************************************************/

/*
 * This is basically Dmitry Vyukov's intrusive MPSC node-based queue:
 *
 * http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
 *
 * The only thing is that here the 'tail' and 'head' have reverse meanining.
 */

struct mm_mpsc_qlink
{
	 struct mm_mpsc_qlink *_Atomic next;
};

struct mm_mpsc_queue
{
	CACHE_ALIGN struct mm_mpsc_qlink *_Atomic tail;
	CACHE_ALIGN struct mm_mpsc_qlink *head;
	struct mm_mpsc_qlink stub;
};

static inline void
mm_mpsc_qlink_prepare(struct mm_mpsc_qlink *link)
{
	atomic_init(&link->next, NULL);
}

static inline void
mm_mpsc_queue_prepare(struct mm_mpsc_queue *list)
{
	list->head = &list->stub;
	atomic_init(&list->tail, &list->stub);
	mm_mpsc_qlink_prepare(&list->stub);
}

static inline void
mm_mpsc_queue_append_span(struct mm_mpsc_queue *list, struct mm_mpsc_qlink *head, struct mm_mpsc_qlink *tail)
{
	struct mm_mpsc_qlink *prev = atomic_exchange(&list->tail, tail);
	atomic_store_explicit(&prev->next, head, memory_order_release);
}

static inline void
mm_mpsc_queue_append(struct mm_mpsc_queue *list, struct mm_mpsc_qlink *link)
{
	mm_mpsc_queue_append_span(list, link, link);
}

static inline struct mm_mpsc_qlink *
mm_mpsc_queue_remove(struct mm_mpsc_queue *list)
{
	struct mm_mpsc_qlink *head = list->head;
	struct mm_mpsc_qlink *next = atomic_load_explicit(&head->next, memory_order_acquire);
	if (head == &list->stub) {
		if (next == NULL)
			return NULL;
		list->head = head = next;
		next = atomic_load_explicit(&next->next, memory_order_acquire);
	}
	if (next != NULL) {
		list->head = next;
		return head;
	}

	struct mm_mpsc_qlink *tail = atomic_load_explicit(&list->tail, memory_order_acquire);
	if (tail != head)
		return NULL;

	mm_mpsc_qlink_prepare(&list->stub);
	mm_mpsc_queue_append(list, &list->stub);
	next = atomic_load_explicit(&head->next, memory_order_acquire);
	if (next) {
		list->head = next;
		return head;
	}
	return NULL;
}

#endif /* BASE_LIST_H */
