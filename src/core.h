/*
 * core.h - MainMemory core.
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

#ifndef CORE_H
#define CORE_H

#include "common.h"
#include "list.h"
#include "runq.h"

/* Forward declaration. */
struct mm_timeq;

/* Virtual core state. */
struct mm_core
{
	/* The master task. */
	struct mm_task *master;

	/* Current and maximum number of worker tasks. */
	uint32_t nworkers;
	uint32_t nworkers_max;

	/* The flag indicating that a master sleeps waiting for a worker. */
	bool master_waits_worker;

	/* The queue of ready to run tasks. */
	struct mm_runq run_queue;

	/* The queue of delayed tasks. */
	struct mm_timeq *time_queue;

	/* An almost current time value. */
	mm_timeval_t time_value;
	mm_timeval_t real_time_value;

	/* The bootstrap task. */
	struct mm_task *boot;

	/* The list of worker tasks that have finished. */
	struct mm_list dead_list;
};

extern __thread struct mm_core *mm_core;

void mm_core_init(void);
void mm_core_term(void);

void mm_core_start(void);
void mm_core_stop(void);

void mm_core_update_time(void);
void mm_core_update_real_time(void);

#endif /* CORE_H */
