/*
 * base/fiber/strand.h - MainMemory fiber strand.
 *
 * Copyright (C) 2013-2019  Aleksey Demakov
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

#ifndef BASE_FIBER_STRAND_H
#define BASE_FIBER_STRAND_H

#include "common.h"

#include "base/context.h"
#include "base/list.h"
#include "base/fiber/runq.h"
#include "base/fiber/wait.h"

/* Forward declarations. */
struct mm_fiber;

/* A strand of fibers. */
struct mm_strand
{
	/* The counter of fiber context switches. */
	uint64_t cswitch_count;

	/* Associated context. */
	struct mm_context *context;

	/* Queue of blocked fibers. */
	struct mm_list block;

	/* Queue of ready to run fibers. */
	struct mm_runq runq;

	/* Queue of fibers waiting for work items. */
	struct mm_list idle;

	/* List of fibers that have finished. */
	struct mm_list dead;

	/* List of asynchronous operations. */
	struct mm_list async;

	/* Current and maximum number of worker fibers. */
	mm_fiber_t nidle;
	mm_fiber_t nworkers;
	mm_fiber_t nworkers_min;
	mm_fiber_t nworkers_max;

	/* Cache of free wait entries. */
	struct mm_wait_cache wait_cache;

	/* The underlying thread. */
	struct mm_thread *thread;

	/* Master fiber. */
	struct mm_fiber *master;

	/* The bootstrap fiber. */
	struct mm_fiber *boot;

	/*
	 * The fields below engage in cross-strand communication.
	 */

	/* Stop flag. */
	bool stop;

} CACHE_ALIGN;

/**********************************************************************
 * Strand initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_strand_prepare(struct mm_strand *strand);

void NONNULL(1)
mm_strand_cleanup(struct mm_strand *strand);

void NONNULL(1, 2)
mm_strand_loop(struct mm_strand *strand, struct mm_context *context);

void NONNULL(1)
mm_strand_stop(struct mm_strand *strand);

/**********************************************************************
 * Strand fiber execution.
 **********************************************************************/

void NONNULL(1)
mm_strand_run_fiber(struct mm_fiber *fiber);

/**********************************************************************
 * Strand diagnostics and statistics.
 **********************************************************************/

void NONNULL(1)
mm_strand_print_fibers(struct mm_strand *strand);

void NONNULL(1)
mm_strand_report_stats(struct mm_strand *strand);

#endif /* BASE_FIBER_STRAND_H */
