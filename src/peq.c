/* -*- tab-width:8 -*- */
/*
 *
 * Copyright (C) 2013, ivan demakov.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#include "peq.h"

#include "pool.h"
#include "util.h"


/* The memory pool for peq. */
static struct mm_pool mm_peq_pool;

/* The memory pool for peq items. */
static struct mm_pool mm_peq_item_pool;


void mm_peq_init(void)
{
    ENTER();

    mm_pool_init(&mm_peq_pool, "peq", sizeof(struct mm_peq));
    mm_pool_init(&mm_peq_item_pool, "peq_item", sizeof(struct mm_peq_item));

    LEAVE();
}


void mm_peq_term(void)
{
    ENTER();

    mm_pool_discard(&mm_peq_pool);
    mm_pool_discard(&mm_peq_item_pool);

    LEAVE();
}


struct mm_peq *mm_peq_create(void)
{
    ENTER();

    struct mm_peq *peq = mm_pool_alloc(&mm_peq_pool);

    peq->tailFE = mm_pool_alloc(&mm_peq_item_pool);
    peq->tailFE->next = peq->tailFE;
    peq->tailFE->prev = peq->tailFE;
    peq->headFE = peq->tailFE;

    peq->t1 = 0;
    peq->startT1 = 0;
    peq->curT1 = MM_TIMEVAL_MIN;
    peq->sizeT1 = 0;
    peq->usedT1 = 0;

    peq->tailT2 = mm_pool_alloc(&mm_peq_item_pool);
    peq->tailT2->next = peq->tailT2;
    peq->tailT2->prev = peq->tailT2;
    peq->headT2 = peq->tailT2;

    peq->maxT2 = peq->minT2 = 0;
    peq->curT2 = MM_TIMEVAL_MIN;

    peq->numT2 = 0;

    LEAVE();
    return peq;
}


void mm_peq_destroy(struct mm_peq *peq)
{
    ENTER();
    ASSERT(peq->headFE == peq->tailFE);
    ASSERT(peq->usedT1 >= peq->sizeT1);
    ASSERT(peq->headT2 == peq->tailT2);

    mm_free(peq->t1);
    mm_pool_free(&mm_peq_item_pool, peq->tailT2);
    mm_pool_free(&mm_peq_pool, peq);

    LEAVE();
}


static void mm_peq_insert_fe(struct mm_peq *peq, struct mm_peq_item *item)
{
    ENTER();

    struct mm_peq_item *next;
    struct mm_peq_item *prev;

    for (next = peq->headFE; next != peq->tailFE; next = next->next) {
        if (next->val <= item->val) {
            break;
        }
    }
    prev = next->prev;

    item->next = next;
    item->prev = prev;
    prev->next = item;
    next->prev = item;
    if (peq->headFE == next) {
        peq->headFE = item;
    }

    peq->numFE += 1;
    item->idx = MM_PEQ_IDX_FE;

    LEAVE();
}


static void mm_peq_insert_t1(struct mm_peq *peq, struct mm_peq_item *item)
{
    ENTER();

    struct mm_peq_item *next;
    struct mm_peq_item *prev;

    int i = (item->val - peq->startT1) / peq->sizeT1;

    next = peq->t1[i].tail;
    prev = next->prev;

    item->next = next;
    item->prev = prev;
    prev->next = item;
    next->prev = item;
    if (peq->t1[i].head == next) {
        peq->t1[i].head = item;
    }

    item->idx = i;

    if (peq->usedT1 > i) {
        peq->usedT1 = i;
    }

    LEAVE();
}


struct mm_peq_item *mm_peq_insert(struct mm_peq *peq, mm_timeval_t val, void *data)
{
    ENTER();

    struct mm_peq_item *next;
    struct mm_peq_item *prev;
    struct mm_peq_item *item = mm_pool_alloc(&mm_peq_item_pool);

    item->val = val;
    item->data = data;
    item->idx = MM_PEQ_IDX_NO;

    if (peq->curT2 <= val) {
        next = peq->tailT2;
        prev = next->prev;

        item->next = next;
        item->prev = prev;
        prev->next = item;
        next->prev = item;
        if (peq->headT2 == next) {
            peq->headT2 = item;
        }

        if (peq->numT2 == 0) {
            peq->minT2 = val;
            peq->maxT2 = val;
        } else {
            if (peq->minT2 > val) {
                peq->minT2 = val;
            }
            if (peq->maxT2 < val) {
                peq->maxT2 = val;
            }
        }

        peq->numT2 += 1;
        item->idx = MM_PEQ_IDX_T2;
    } else if (peq->curT1 <= val) {
        mm_peq_insert_t1(peq, item);
    } else {
        mm_peq_insert_fe(peq, item);
    }

    LEAVE();
    return item;
}


void mm_peq_delete(struct mm_peq *peq, struct mm_peq_item *item)
{
    ENTER();

    struct mm_peq_item *next = item->next;
    struct mm_peq_item *prev = item->prev;

    if (next) {
        next->prev = prev;
        prev->next = next;

        if (item->idx == MM_PEQ_IDX_FE) {
            if (peq->headFE == item) {
                peq->headFE = next;
            }
            peq->numFE -= 1;
        } else if (item->idx == MM_PEQ_IDX_T2) {
            if (peq->headT2 == item) {
                peq->headT2 = next;
            }
            peq->numT2 -= 1;
        } else if (item->idx >= 0) {
            if (peq->t1[item->idx].head == item) {
                peq->t1[item->idx].head = next;
            }
        }

        item->next = 0;
        item->prev = 0;
        item->idx = MM_PEQ_IDX_NO;
    }

    mm_pool_free(&mm_peq_item_pool, item);

    LEAVE();
}


struct mm_peq_item *mm_peq_getmin(struct mm_peq *peq)
{
    ENTER();

    int i;
    struct mm_peq_item *next;
    struct mm_peq_item *prev;
    struct mm_peq_item *item = 0;

restart:
    if (peq->headFE != peq->tailFE) {
        item = peq->tailFE->prev;
        next = item->next;
        prev = item->prev;

        next->prev = prev;
        prev->next = next;
        if (peq->headFE == item) {
            peq->headFE = next;
        }

        item->next = 0;
        item->prev = 0;
        item->idx = MM_PEQ_IDX_NO;

        peq->numFE -= 1;
    } else {
        while (peq->usedT1 < peq->sizeT1) {
            if (peq->t1[peq->usedT1].head != peq->t1[peq->usedT1].tail) {
                /* The bucket is not empty */
                break;
            }
            peq->usedT1 += 1;
        }

        if (peq->usedT1 < peq->sizeT1) {
            /* The bucket is not empty */
            int used = peq->usedT1;
            peq->usedT1 = used + 1;

            /* If the bucket has exactly one item, return the item. */
            /* In other case, move all items in front end structure. */
            item = peq->t1[used].head;
            peq->t1[used].head = peq->t1[used].tail;

            next = item->next;
            prev = item->prev;
            if (next == peq->t1[used].tail) {
                next->prev = prev;
                prev->next = next;

                item->next = 0;
                item->prev = 0;
                item->idx = MM_PEQ_IDX_NO;
            } else {
                while (item != peq->t1[used].tail) {
                    next->prev = prev;
                    prev->next = next;
                    mm_peq_insert_fe(peq, item);

                    item = next;
                    next = item->next;
                    prev = item->prev;
                }

                item = 0;
                goto restart;
            }
        } else if (peq->numT2 == 1) {
            /* All buckets empty and only one item in T2 */
            item = peq->headT2;
            peq->headT2 = peq->tailT2;
            peq->numT2 = 0;

            next = item->next;
            prev = item->prev;

            next->prev = prev;
            prev->next = next;

            item->next = 0;
            item->prev = 0;
            item->idx = MM_PEQ_IDX_NO;
        } else if (peq->numT2 > 1) {
            /* All buckets empty, move all items in T1 */
            int sizeT1 = (peq->maxT2 - peq->minT2) / peq->numT2;
            if (sizeT1 < 64) {
                sizeT1 = 64;
            }
            if (peq->sizeT1 < sizeT1) {
                sizeT1 *= 2;
                peq->t1 = mm_realloc(peq->t1, sizeT1 * sizeof(*peq->t1));
                for (i = peq->sizeT1; i < sizeT1; ++i) {
                    peq->t1[i].tail = mm_pool_alloc(&mm_peq_item_pool);
                    peq->t1[i].tail->next = peq->t1[i].tail;
                    peq->t1[i].tail->prev = peq->t1[i].tail;
                    peq->t1[i].head = peq->t1[i].tail;
                }
                peq->sizeT1 = sizeT1;
            }

            peq->usedT1 = sizeT1;
            peq->startT1 = peq->curT1 = peq->minT2;
            peq->minT2 = peq->curT2 = peq->maxT2;

            item = peq->headT2;
            peq->headT2 = peq->tailT2;
            peq->numT2 = 0;

            while (item != peq->tailT2) {
                next = item->next;
                prev = item->prev;

                next->prev = prev;
                prev->next = next;
                mm_peq_insert_t1(peq, item);

                item = next;
            }

            item = 0;
            goto restart;
        }
    }

    LEAVE();
    return item;
}



 /* End of code */
