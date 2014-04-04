/*
 * selfpipe.c - MainMemory concurrent self-pipe trick.
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

#include "selfpipe.h"

#include "log.h"
#include "trace.h"
#include "util.h"

#include <unistd.h>

static mm_atomic_uint32_t mm_selfpipe_write_count;

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

	selfpipe->read_ready = false;

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
mm_selfpipe_write(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_atomic_uint32_inc(&mm_selfpipe_write_count);

	(void) write(selfpipe->write_fd, "", 1);

	LEAVE();
}

bool
mm_selfpipe_drain(struct mm_selfpipe *selfpipe)
{
	ENTER();

	bool ready = selfpipe->read_ready;
	if (ready) {
		selfpipe->read_ready = false;

		char dummy[64];
		while (read(selfpipe->read_fd, dummy, sizeof dummy) == sizeof dummy) {
			/* do nothing */
		}
	}

	LEAVE();
	return ready;
}

void
mm_selfpipe_stats(void)
{
	uint32_t write = mm_memory_load(mm_selfpipe_write_count.value);
	mm_verbose("selfpipe stats: write = %u", write);
}
