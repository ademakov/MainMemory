/*
 * event/selfpipe.h - MainMemory self-pipe trick.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#ifndef EVENT_SELFPIPE_H
#define EVENT_SELFPIPE_H

#include "common.h"

struct mm_selfpipe
{
	int read_fd;
	int write_fd;

	bool read_ready;
};

static inline void __attribute__((nonnull(1)))
mm_selfpipe_set_ready(struct mm_selfpipe *selfpipe)
{
	selfpipe->read_ready = true;
}

void __attribute__((nonnull(1)))
mm_selfpipe_prepare(struct mm_selfpipe *selfpipe);

void __attribute__((nonnull(1)))
mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe);

void __attribute__((nonnull(1)))
mm_selfpipe_write(struct mm_selfpipe *selfpipe);

void __attribute__((nonnull(1)))
mm_selfpipe_drain(struct mm_selfpipe *selfpipe);

void mm_selfpipe_stats(void);

#endif /* EVENT_SELFPIPE_H */
