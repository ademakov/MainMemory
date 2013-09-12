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

	// Take the notification into account. But it will be really
	// delivered to the listening side only when it makes a next
	// mm_selfpipe_listen() call.
	mm_atomic_uint32_inc(&selfpipe->count);

	mm_memory_fence();

	// Make a write() call to the pipe if the listening side of the
	// self-pipe is currently indeed listening. The objective here is
	// to save expensive system calls when this does not contribute to
	// the overall progress.
	//
	// Note that this logic is far from precise. For instance, this
	// thread might get preempted on the write() call. And during this
	// time the listening side might wakeup for a different reason,
	// perform one or more full event-handling cycles and consume the
	// notification held in the "count" field. And then this thread
	// gets CPU time again and really finishes the write call causing
	// spurious wakeup on the listening side.
	//
	// The key here is that no notification is ever lost or delayed
	// rather than no extra write() call is ever made. Without exact
	// control over the thread scheduling policy the only safe option
	// is to make sure that the time span of a write() call always
	// overlaps the time span of the corresponding listening side poll().
	// Even if the error, as in the example above, might be as large as
	// several other poll() cycles.
	if (mm_memory_load(selfpipe->listen)) {
		(void) write(selfpipe->write_fd, "", 1);
	}

	LEAVE();
}

/*
 * Signal that the listening side of the self-pipe is really going into
 * the listening mode and thus it will be needed to notify it thru the
 * pipe write() calls. The return value indicates if there were any
 * notifications while the listening side was not really listening.
 */
bool
mm_selfpipe_listen(struct mm_selfpipe *selfpipe)
{
	ENTER();

	// Drain the stalled notifications if any.
	if (selfpipe->ready) {
		mm_selfpipe_drain(selfpipe);
		selfpipe->ready = false;
	}

	// Signal that the listening side is here.
	mm_memory_store(selfpipe->listen, true);

	mm_memory_fence();

	// See if there were some unseen notifications pending.
	uint32_t n = mm_atomic_uint32_fetch_and_set(&selfpipe->count, 0);

	LEAVE();
	return (n != 0);
}

/*
 * Signal that the listening side of the self-pipe is going to be busy
 * with something else so there is no point to make pipe write() calls.
 */
void
mm_selfpipe_divert(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_memory_store(selfpipe->listen, false);

	LEAVE();
}
