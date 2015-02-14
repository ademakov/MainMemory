/*
 * event/selfpipe.c - MainMemory self-pipe trick.
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

#include "event/selfpipe.h"
#include "event/nonblock.h"

#include "arch/atomic.h"
#include "arch/memory.h"

#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"

#include <unistd.h>

/**********************************************************************
 * Self-pipe Handler.
 **********************************************************************/

/* Self-pipe event handler ID. */
static mm_event_hid_t mm_selfpipe_handler;

static void
mm_selfpipe_ready(mm_event_t event __attribute__((unused)), void *data)
{
	ENTER();

	struct mm_selfpipe *selfpipe = containerof(data, struct mm_selfpipe, event_fd);

	selfpipe->read_ready = true;

	LEAVE();
}

/**********************************************************************
 * Self-pipe Initialization.
 **********************************************************************/

void
mm_selfpipe_init(void)
{
	ENTER();

	// Register the self-pipe event handler.
	mm_selfpipe_handler = mm_event_register_handler(mm_selfpipe_ready);

	LEAVE();
}

/**********************************************************************
 * Self-pipe Statistics.
 **********************************************************************/

static mm_atomic_uint32_t mm_selfpipe_write_count;

void
mm_selfpipe_stats(void)
{
	uint32_t write_count = mm_memory_load(mm_selfpipe_write_count);
	mm_verbose("selfpipe stats: write = %u", write_count);
}

/**********************************************************************
 * Self-pipe instance.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_selfpipe_prepare(struct mm_selfpipe *selfpipe)
{
	ENTER();

	int fds[2];

	if (pipe(fds) < 0)
		mm_fatal(errno, "pipe()");

	mm_set_nonblocking(fds[0]);
	mm_set_nonblocking(fds[1]);

	mm_event_prepare_fd(&selfpipe->event_fd, fds[0],
			    mm_selfpipe_handler, false, 0, false, 0);
	selfpipe->write_fd = fds[1];
	selfpipe->read_ready = false;

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe)
{
	ENTER();

	close(selfpipe->event_fd.fd);
	close(selfpipe->write_fd);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_selfpipe_write(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_atomic_uint32_inc(&mm_selfpipe_write_count);

	(void) write(selfpipe->write_fd, "", 1);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_selfpipe_drain(struct mm_selfpipe *selfpipe)
{
	ENTER();

	if (selfpipe->read_ready) {
		selfpipe->read_ready = false;

		char dummy[64];
		while (read(selfpipe->event_fd.fd, dummy, sizeof dummy) == sizeof dummy) {
			/* do nothing */
		}
	}

	LEAVE();
}
