/*
 * synch.h - MainMemory simple thread synchronization.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef SYNCH_H
#define SYNCH_H

#include "common.h"

/* Forward declarations. */
struct mm_event_table;

/*
 * NB: Currently only one thread is allowed to wait on the synch object.
 */

/* An actual object might contain additional hidden fields. So it is not
   allowed to have it as a static nor embedded in another structure. */
struct mm_synch
{
	/* Synchronization variable. */
	mm_atomic_uint32_t value;

	/* Synchronization type token. */
	uint32_t magic;
};

/*
 * The mm_synch_test() and mm_synch_clear() functions are not synchronized.
 * There might be data contention between these functions and mm_synch_wait()
 * or mm_synch_timedwait() functions so it is only safe to use them together
 * when all of their calls are serialized by some other means, for instance,
 * by being called just from single 'owner' thread.
 */

static inline bool
mm_synch_test(struct mm_synch *synch)
{
	return mm_memory_load(synch->value) != 0;
}

struct mm_synch *mm_synch_create(void);
struct mm_synch *mm_synch_create_cond(void);
struct mm_synch *mm_synch_create_event_poll(struct mm_event_table *events);

void mm_synch_destroy(struct mm_synch *synch)
	__attribute__((nonnull(1)));

void mm_synch_wait(struct mm_synch *synch)
	__attribute__((nonnull(1)));

bool mm_synch_timedwait(struct mm_synch *synch, mm_timeout_t timeout);

void mm_synch_signal(struct mm_synch *synch)
	__attribute__((nonnull(1)));

void mm_synch_clear(struct mm_synch *synch)
	__attribute__((nonnull(1)));

#endif /* SYNCH_H */
