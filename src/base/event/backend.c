/*
 * base/event/backend.c - MainMemory event system backend.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#include "base/report.h"
#include "base/event/batch.h"

void NONNULL(1)
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
# if HAVE_SYS_EPOLL_H
	backend->native_notify = mm_event_epoll_enable_notify(&backend->backend);
# elif HAVE_SYS_EVENT_H
	backend->native_notify = mm_event_kqueue_enable_notify(&backend->backend);
# endif
	if (backend->native_notify)
		goto leave;
#endif

	// Open the event self-pipe.
	mm_selfpipe_prepare(&backend->selfpipe);

	// Register the self-pipe.
	struct mm_event_change change;
	change.kind = MM_EVENT_REGISTER,
	change.sink = &backend->selfpipe.event_fd;
	mm_event_backend_change(backend, &change);

#if MM_EVENT_NATIVE_NOTIFY
leave:
#endif
	LEAVE();
}

void NONNULL(1)
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

void NONNULL(1)
mm_event_backend_storage_prepare(struct mm_event_backend_storage *storage)
{
	ENTER();

#if HAVE_SYS_EPOLL_H
	mm_event_epoll_storage_prepare(&storage->storage);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_storage_prepare(&storage->storage);
#endif

	LEAVE();
}
