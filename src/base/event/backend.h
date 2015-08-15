/*
 * base/event/backend.h - MainMemory events system backend.
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

#ifndef BASE_EVENT_BACKEND_H
#define BASE_EVENT_BACKEND_H

#include "common.h"
#include "base/event/epoll.h"
#include "base/event/kqueue.h"
#include "base/event/selfpipe.h"

struct mm_event_backend
{
	/* Events system-specific backend. */
#if HAVE_SYS_EPOLL_H
	struct mm_event_epoll backend;
#elif HAVE_SYS_EVENT_H
	struct mm_event_kqueue backend;
#endif

#if MM_EVENT_NATIVE_NOTIFY
	/* A flag indicating that the system supports a better
	   notification mechanism than a self-pipe. */
	bool native_notify;
#endif

	/* Event loop self-pipe. */
	struct mm_selfpipe selfpipe;
};

void __attribute__((nonnull(1)))
mm_event_backend_prepare(struct mm_event_backend *backend);

void __attribute__((nonnull(1)))
mm_event_backend_cleanup(struct mm_event_backend *backend);

/*
 * Tell if the backend requires all change events to be serialized.
 *
 * The existing backends (epoll and kqueue) do not require this. So
 * this is just a stub for possible future implementation of select
 * or poll backends.
 */
static inline bool __attribute__((nonnull(1)))
mm_event_backend_serial(struct mm_event_backend *backend __mm_unused__)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return false;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_event_backend_listen(struct mm_event_backend *backend,
			struct mm_event_batch *changes,
			struct mm_event_receiver *receiver,
			mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_listen(&backend->backend, changes, receiver, timeout);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_listen(&backend->backend, changes, receiver, timeout);
#endif
}

static inline void __attribute__((nonnull(1)))
mm_event_backend_notify(struct mm_event_backend *backend)
{
#if MM_EVENT_NATIVE_NOTIFY
	if (backend->native_notify) {
# if HAVE_SYS_EPOLL_H
		mm_event_epoll_notify(&backend->backend);
# elif HAVE_SYS_EVENT_H
		mm_event_kqueue_notify(&backend->backend);
# endif
		return;
	}
#endif
	mm_selfpipe_write(&backend->selfpipe);
}

static inline void __attribute__((nonnull(1)))
mm_event_backend_dampen(struct mm_event_backend *backend)
{
#if MM_EVENT_NATIVE_NOTIFY
	if (backend->native_notify)
		return;
#endif
	mm_selfpipe_drain(&backend->selfpipe);
}

#endif /* BASE_EVENT_BACKEND_H */
