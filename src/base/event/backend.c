/*
 * base/event/backend.c - MainMemory event system backend.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/event/backend.h"

#include "base/event/batch.h"
#include "base/log/trace.h"

void __attribute__((nonnull(1)))
mm_event_backend_prepare(struct mm_event_backend *backend)
{
	ENTER();

	// Open the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_prepare(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_prepare(&backend->backend);
#endif

	// Try to use native system notify mechanism.
#if MM_EVENT_NATIVE_NOTIFY
	backend->native_notify
# if HAVE_SYS_EPOLL_H
		= mm_event_epoll_enable_notify(&backend->backend);
# elif HAVE_SYS_EVENT_H
		= mm_event_kqueue_enable_notify(&backend->backend);
# endif
	if (backend->native_notify)
		goto leave;
#endif

	// Open the event self-pipe.
	mm_selfpipe_prepare(&backend->selfpipe);

	// Register the self-pipe.
	struct mm_event_batch changes;
	mm_event_batch_prepare(&changes, 1);
	mm_event_batch_add(&changes, MM_EVENT_REGISTER,
			   &backend->selfpipe.event_fd);
	mm_event_backend_listen(backend, &changes, NULL, 0);
	mm_event_batch_cleanup(&changes);

#if MM_EVENT_NATIVE_NOTIFY
leave:
#endif
	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_backend_cleanup(struct mm_event_backend *backend)
{
	ENTER();

	// Close the event self-pipe.
# if MM_EVENT_NATIVE_NOTIFY
	if (!backend->native_notify)
		mm_selfpipe_cleanup(&backend->selfpipe);
#else
	mm_selfpipe_cleanup(&backend->selfpipe);
#endif

	// Close the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_cleanup(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_cleanup(&backend->backend);
#endif

	LEAVE();
}
