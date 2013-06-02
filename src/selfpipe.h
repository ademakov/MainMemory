/*
 * selfpipe.h - MainMemory concurrent self-pipe trick.
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

#ifndef SELFPIPE_H
#define SELFPIPE_H

#include "common.h"

struct mm_selfpipe
{
	int read_fd;
	int write_fd;

	/* Read fd is ready. */
	bool ready;

	/* Read end is listening. */
	bool listen;

	char padding[MM_CACHELINE - 2 * sizeof(int) - 2 * sizeof(bool)];

	/* Notification count. */
	mm_atomic_uint32_t count;
};

void mm_selfpipe_prepare(struct mm_selfpipe *selfpipe);
void mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe);

void mm_selfpipe_notify(struct mm_selfpipe *selfpipe);
bool mm_selfpipe_listen(struct mm_selfpipe *selfpipe);
void mm_selfpipe_divert(struct mm_selfpipe *selfpipe);

static inline void
mm_selfpipe_set_ready(struct mm_selfpipe *selfpipe)
{
	selfpipe->ready = true;
}

#endif /* SELFPIPE_H */
