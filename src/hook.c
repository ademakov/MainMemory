/*
 * hook.c - MainMemory hook routines.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#include "hook.h"

#include "alloc.h"
#include "trace.h"

struct mm_hook_link0
{
	struct mm_link link;
	mm_hook_rtn0 proc;
};

struct mm_hook_link1
{
	struct mm_link link;
	mm_hook_rtn1 proc;
	void *data;
};

static struct mm_hook_link0 *
mm_hook_create_link0(mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_global_alloc(sizeof(struct mm_hook_link0));
	link->proc = proc;
	return link;
}

static struct mm_hook_link1 *
mm_hook_create_link1(mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_global_alloc(sizeof(struct mm_hook_link1));
	link->proc = (mm_hook_rtn1) (((intptr_t) proc) | 1);
	link->data = data;
	return link;
}

static void
mm_hook_call_link(struct mm_link *link)
{
	struct mm_hook_link0 *link0 = (struct mm_hook_link0 *) link;
	mm_hook_rtn0 proc0 = link0->proc;
	uint8_t arity = ((uintptr_t) proc0) & 0x1;

	if (arity == 0) {
		proc0();
	} else {
		mm_hook_rtn1 proc1 = (mm_hook_rtn1) (((uintptr_t) proc0) & ~1);
		struct mm_hook_link1 *link1 = (struct mm_hook_link1 *) link0;
		proc1(link1->data);
	}
}

void
mm_hook_call(struct mm_queue *hook, bool free)
{
	struct mm_link *link = mm_queue_head(hook);
	while (link != NULL) {
		struct mm_link *next = link->next;

		mm_hook_call_link(link);
		if (free)
			mm_global_free(link);

		link = next;
	}

	if (free)
		mm_queue_init(hook);
}

void
mm_hook_head_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_hook_create_link0(proc);
	mm_queue_insert_head(hook, &link->link);
}

void
mm_hook_tail_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_hook_create_link0(proc);
	mm_queue_append(hook, &link->link);
}

void
mm_hook_head_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_hook_create_link1(proc, data);
	mm_queue_insert_head(hook, &link->link);
}

void
mm_hook_tail_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_hook_create_link1(proc, data);
	mm_queue_append(hook, &link->link);
}

void
mm_hook_free(struct mm_queue *hook)
{
	struct mm_link *link = mm_queue_head(hook);
	while (link != NULL) {
		struct mm_link *next = link->next;
		mm_global_free(link);
		link = next;
	}

	mm_queue_init(hook);
}
