/*
 * hook.h - MainMemory hook routines.
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

#ifndef HOOK_H
#define HOOK_H

#include "common.h"

struct mm_hook
{
	struct mm_hook_link *head;
	struct mm_hook_link *tail;
};

void mm_hook_init(struct mm_hook *hook);
void mm_hook_free(struct mm_hook *hook);

void mm_hook_head_proc(struct mm_hook *hook, void (*proc)(void));
void mm_hook_tail_proc(struct mm_hook *hook, void (*proc)(void));
void mm_hook_call_proc(struct mm_hook *hook, bool free);

void mm_hook_head_data_proc(struct mm_hook *hook, void (*proc)(void *), void *data);
void mm_hook_tail_data_proc(struct mm_hook *hook, void (*proc)(void *), void *data);
void mm_hook_call_data_proc(struct mm_hook *hook, bool free);

#endif /* HOOK_H */
