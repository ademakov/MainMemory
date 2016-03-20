/*
 * base/util/hook.c - MainMemory hook routines.
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

#include "base/util/hook.h"

#include "base/list.h"
#include "base/report.h"
#include "base/memory/global.h"

struct mm_hook_link0
{
	struct mm_qlink link;
	uint8_t arity;
	mm_hook_rtn0 proc;
};

struct mm_hook_link1
{
	struct mm_qlink link;
	uint8_t arity;
	mm_hook_rtn1 proc;
	void *data;
};

static struct mm_hook_link0 *
mm_hook_create_link0(mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_global_alloc(sizeof(struct mm_hook_link0));
	link->arity = 0;
	link->proc = proc;
	return link;
}

static struct mm_hook_link1 *
mm_hook_create_link1(mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_global_alloc(sizeof(struct mm_hook_link1));
	link->arity = 1;
	link->proc = proc;
	link->data = data;
	return link;
}

static void
mm_hook_call_link(struct mm_qlink *link)
{
	struct mm_hook_link0 *link0 = (struct mm_hook_link0 *) link;
	uint8_t arity = link0->arity;

	if (arity == 0) {
		(link0->proc)();
	} else {
		struct mm_hook_link1 *link1 = (struct mm_hook_link1 *) link0;
		(link1->proc)(link1->data);
	}
}

void NONNULL(1)
mm_hook_call(struct mm_queue *hook, bool free)
{
	struct mm_qlink *link = mm_queue_head(hook);
	while (link != NULL) {
		struct mm_qlink *next = link->next;

		mm_hook_call_link(link);
		if (free)
			mm_global_free(link);

		link = next;
	}

	if (free)
		mm_queue_prepare(hook);
}

void NONNULL(1, 2)
mm_hook_head_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_hook_create_link0(proc);
	mm_queue_prepend(hook, &link->link);
}

void NONNULL(1, 2)
mm_hook_tail_proc(struct mm_queue *hook, mm_hook_rtn0 proc)
{
	struct mm_hook_link0 *link = mm_hook_create_link0(proc);
	mm_queue_append(hook, &link->link);
}

void NONNULL(1, 2)
mm_hook_head_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_hook_create_link1(proc, data);
	mm_queue_prepend(hook, &link->link);
}

void NONNULL(1, 2)
mm_hook_tail_data_proc(struct mm_queue *hook, mm_hook_rtn1 proc, void *data)
{
	struct mm_hook_link1 *link = mm_hook_create_link1(proc, data);
	mm_queue_append(hook, &link->link);
}

void NONNULL(1)
mm_hook_free(struct mm_queue *hook)
{
	struct mm_qlink *link = mm_queue_head(hook);
	while (link != NULL) {
		struct mm_qlink *next = link->next;
		mm_global_free(link);
		link = next;
	}

	mm_queue_prepare(hook);
}
