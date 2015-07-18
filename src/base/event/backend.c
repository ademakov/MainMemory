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

void __attribute__((nonnull(1)))
mm_event_backend_prepare(struct mm_event_backend *backend)
{
	// Open the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_prepare(&backend->backend);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_prepare(&backend->backend);
#endif

	// Open the event self-pipe.
	mm_selfpipe_prepare(&backend->selfpipe);
}

void __attribute__((nonnull(1)))
mm_event_backend_cleanup(struct mm_event_backend *backend)
{
	// Close the event self-pipe.
	mm_selfpipe_cleanup(&backend->selfpipe);

	// Close the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_cleanup(&backend->backend);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_cleanup(&backend->backend);
#endif
}