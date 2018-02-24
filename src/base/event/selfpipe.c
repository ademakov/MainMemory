/*
 * base/event/selfpipe.c - MainMemory self-pipe trick.
 *
 * Copyright (C) 2013-2018  Aleksey Demakov
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

#include "base/event/selfpipe.h"

#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/nonblock.h"

#include <unistd.h>

void NONNULL(1)
mm_selfpipe_prepare(struct mm_selfpipe *selfpipe)
{
	ENTER();

	int fds[2];

	if (pipe(fds) < 0)
		mm_fatal(errno, "pipe()");

	mm_set_nonblocking(fds[0]);
	mm_set_nonblocking(fds[1]);

	mm_event_prepare_fd(&selfpipe->sink, fds[0], NULL, NULL, MM_EVENT_IGNORED, MM_EVENT_IGNORED, false);
	selfpipe->sink.flags = MM_EVENT_REGULAR_INPUT | MM_EVENT_NOTIFY_FD;
	selfpipe->write_fd = fds[1];

	LEAVE();
}

void NONNULL(1)
mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe)
{
	ENTER();

	mm_close(selfpipe->sink.fd);
	mm_close(selfpipe->write_fd);

	LEAVE();
}

void NONNULL(1)
mm_selfpipe_write(struct mm_selfpipe *selfpipe)
{
	ENTER();

	(void) mm_write(selfpipe->write_fd, "", 1);

	LEAVE();
}

void NONNULL(1)
mm_selfpipe_clean(struct mm_selfpipe *selfpipe)
{
	ENTER();

	if ((selfpipe->sink.flags & MM_EVENT_INPUT_READY) != 0) {
		selfpipe->sink.flags &= ~MM_EVENT_INPUT_READY;

		char dummy[64];
		while (mm_read(selfpipe->sink.fd, dummy, sizeof dummy) == sizeof dummy) {
			/* do nothing */
		}
	}

	LEAVE();
}
