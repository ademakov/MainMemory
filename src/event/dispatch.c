/*
 * event/dispatch.c - MainMemory event dispatch.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "event/dispatch.h"

#include "base/log/trace.h"

void __attribute__((nonnull(1)))
mm_dispatch_prepare(struct mm_dispatch *dispatch)
{
	ENTER();

	dispatch->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;

	dispatch->polling_listener = NULL;
	mm_list_init(&dispatch->waiting_listeners);
	mm_event_batch_prepare(&dispatch->pending_changes);

	// Initialize system-specific resources.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_prepare(&dispatch->events);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_prepare(&dispatch->events);
#endif

	// Open a self-pipe.
	mm_selfpipe_prepare(&dispatch->selfpipe);

	// Register the self-pipe.
	mm_event_batch_add(&dispatch->pending_changes,
			   MM_EVENT_REGISTER,
			   &dispatch->selfpipe.event_fd);
	mm_dispatch_listen(dispatch,
			   &dispatch->pending_changes,
			   &dispatch->pending_changes,
			   0);
	mm_event_batch_clear(&dispatch->pending_changes);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch)
{
	ENTER();

	// Release pending batch memory.
	mm_event_batch_cleanup(&dispatch->pending_changes);

	// Close the event self-pipe.
	mm_selfpipe_cleanup(&dispatch->selfpipe);

	// Close the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_cleanup(&dispatch->events);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_cleanup(&dispatch->events);
#endif

	LEAVE();
}
