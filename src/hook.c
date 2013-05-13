/*
 * hook.c - MainMemory hook routines.
 *
 * Copyright (C) 2013  Aleksey Demakov
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
#include "util.h"

struct mm_hook_link
{
	struct mm_hook_link *next;
};

struct mm_hook_proc
{
	struct mm_hook_link link;
	void (*proc)(void);
};

struct mm_hook_data_proc
{
	struct mm_hook_link link;
	void (*proc)(void *);
	void *data;
};

static void
mm_hook_add_head(struct mm_hook *hook, struct mm_hook_link *link)
{
	link->next = hook->head;
	if (hook->head == NULL)
		hook->head = hook->tail = link;
	else
		hook->head = link;
}

static void
mm_hook_add_tail(struct mm_hook *hook, struct mm_hook_link *link)
{
	link->next = NULL;
	if (hook->head == NULL)
		hook->head = hook->tail = link;
	else
		hook->tail->next = link;
}

void
mm_hook_init(struct mm_hook *hook)
{
	hook->head = hook->tail = NULL;
}

void
mm_hook_head_proc(struct mm_hook *hook, void (*proc)(void))
{
	ASSERT(proc != NULL);

	struct mm_hook_proc *link = mm_alloc(sizeof(struct mm_hook_proc));
	link->proc = proc;
	mm_hook_add_head(hook, &link->link);
}

void
mm_hook_tail_proc(struct mm_hook *hook, void (*proc)(void))
{
	ASSERT(proc != NULL);

	struct mm_hook_proc *link = mm_alloc(sizeof(struct mm_hook_proc));
	link->proc = proc;
	mm_hook_add_tail(hook, &link->link);
}

void
mm_hook_head_data_proc(struct mm_hook *hook, void (*proc)(void *), void *data)
{
	ASSERT(proc != NULL);

	struct mm_hook_data_proc *link = mm_alloc(sizeof(struct mm_hook_data_proc));
	link->proc = proc;
	link->data = data;
	mm_hook_add_head(hook, &link->link);
}

void
mm_hook_tail_data_proc(struct mm_hook *hook, void (*proc)(void *), void *data)
{
	ASSERT(proc != NULL);

	struct mm_hook_data_proc *link = mm_alloc(sizeof(struct mm_hook_data_proc));
	link->proc = proc;
	link->data = data;
	mm_hook_add_tail(hook, &link->link);
}

void
mm_hook_call_proc(struct mm_hook *hook, bool free)
{
	struct mm_hook_link *link = hook->head;
	while (link != NULL) {
		struct mm_hook_proc *proc = (struct mm_hook_proc *) link;
		struct mm_hook_link *next = link->next;

		proc->proc();
		if (free)
			mm_free(proc);

		link = next;
	}

	if (free)
		mm_hook_init(hook);
}

void
mm_hook_call_data_proc(struct mm_hook *hook, bool free)
{
	struct mm_hook_link *link = hook->head;
	while (link != NULL) {
		struct mm_hook_data_proc *proc = (struct mm_hook_data_proc *) link;
		struct mm_hook_link *next = link->next;

		proc->proc(proc->data);
		if (free)
			mm_free(proc);

		link = next;
	}

	if (free)
		mm_hook_init(hook);
}

void
mm_hook_free(struct mm_hook *hook)
{
	struct mm_hook_link *link = hook->head;
	while (link != NULL) {
		struct mm_hook_link *next = link->next;
		mm_free(link);
		link = next;
	}

	mm_hook_init(hook);
}
