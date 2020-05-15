/*
 * base/event/backend.c - MainMemory event system backend.
 *
 * Copyright (C) 2015-2020  Aleksey Demakov
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

void NONNULL(1, 2)
mm_event_backend_prepare(struct mm_event_backend *backend, struct mm_event_backend_local *some_local UNUSED)
{
	ENTER();

	// Open the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_prepare(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_prepare(&backend->backend);
#endif

	// Set up the notification mechanism.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_enable_notify(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_enable_notify(&backend->backend);
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_backend_cleanup(struct mm_event_backend *backend)
{
	ENTER();

	// Close the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_cleanup(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_cleanup(&backend->backend);
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_backend_local_prepare(struct mm_event_backend_local *local)
{
	ENTER();

#if HAVE_SYS_EPOLL_H
	mm_event_epoll_local_prepare(local);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_storage_prepare(local);
#endif

	LEAVE();
}
