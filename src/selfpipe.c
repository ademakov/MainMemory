/*
 * selfpipe.c - MainMemory concurrent self-pipe trick.
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

#include "selfpipe.h"

#include "log.h"
#include "trace.h"
#include "util.h"

#include <unistd.h>

static void
mm_selfpipe_drain(struct mm_selfpipe *selfpipe)
{
	ENTER();

	char dummy[64];
	while (read(selfpipe->read_fd, dummy, sizeof dummy) == sizeof dummy) {
		/* do nothing */
	}

	LEAVE();
}

void
mm_selfpipe_prepare(struct mm_selfpipe *selfpipe)
{
	ENTER();

	int fds[2];

	if (pipe(fds) < 0) {
		mm_fatal(errno, "pipe()");
	}

	mm_set_nonblocking(fds[0]);
	mm_set_nonblocking(fds[1]);

	selfpipe->read_fd = fds[0];
	selfpipe->write_fd = fds[1];

	selfpipe->ready = false;
	selfpipe->listen = false;
	selfpipe->count.value = 0;

	LEAVE();
}

void
mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe)
{
	ENTER();

	close(selfpipe->read_fd);
	close(selfpipe->write_fd);

	LEAVE();
}

void
mm_selfpipe_notify(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_atomic_uint32_inc(&selfpipe->count);

	mm_memory_fence();

	if (mm_memory_load(selfpipe->listen)) {
		(void) write(selfpipe->write_fd, "", 1);
	}

	LEAVE();
}

bool
mm_selfpipe_listen(struct mm_selfpipe *selfpipe)
{
	ENTER();

	if (selfpipe->ready) {
		mm_selfpipe_drain(selfpipe);
		selfpipe->ready = false;
	}

	mm_memory_store(selfpipe->listen, true);

	mm_memory_fence();

	uint32_t w = mm_atomic_uint32_fetch_and_set(&selfpipe->count, 0);

	LEAVE();
	return (w != 0);
}

void
mm_selfpipe_divert(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_memory_store(selfpipe->listen, false);

	LEAVE();
}
