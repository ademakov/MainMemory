/* -*- mode:C++; tab-width:8 -*- */
/*
 * event.h - MainMemory event queue
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PEQ_H
#define PEQ_H

#include "common.h"

#define MM_PEQ_IDX_NO (-1)
#define MM_PEQ_IDX_T2 (-2)
#define MM_PEQ_IDX_FE (-3)

struct mm_peq_item
{
    struct mm_peq_item *next;
    struct mm_peq_item *prev;

    mm_timeval_t val;
    void *data;
    int idx;
};


struct mm_peq_bucket
{
    struct mm_peq_item *head;
    struct mm_peq_item *tail;
};


struct mm_peq
{
    struct mm_peq_item *headFE; /**< front end head */
    struct mm_peq_item *tailFE; /**< front end tail */
    int numFE;

    struct mm_peq_bucket *t1;   /**< T1 structure */
    mm_timeval_t startT1;       /**< used to calculate the bucket */
    mm_timeval_t curT1;         /**< minimum timestamp threshhold of events in T1 */
    int sizeT1;                 /**< size of bucket array */
    int usedT1;                 /**< fisrt used bucket */

    struct mm_peq_item *headT2; /**< t2 head */
    struct mm_peq_item *tailT2; /**< t2 tail */
    mm_timeval_t maxT2;         /**< maximum timestamp of all events in T2 */
    mm_timeval_t minT2;         /**< minimum timestamp of all events in T2 */
    mm_timeval_t curT2;         /**< minimum timestamp threshhold of events in T2 */
    int numT2;                  /**< number of events in T2 */
};


void mm_peq_init(void);


void mm_peq_term(void);


struct mm_peq *mm_peq_create(void);


void mm_peq_destroy(struct mm_peq *peq);


struct mm_peq_item *mm_peq_insert(struct mm_peq *peq, mm_timeval_t val, void *data);


void mm_peq_delete(struct mm_peq *peq, struct mm_peq_item *item);


struct mm_peq_item *mm_peq_getmin(struct mm_peq *peq);


#endif

/* End of file */
