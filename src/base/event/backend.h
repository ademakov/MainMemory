/*
 * base/event/backend.h - MainMemory events system backend.
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

#ifndef BASE_EVENT_BACKEND_H
#define BASE_EVENT_BACKEND_H

#include "common.h"
#include "base/event/epoll.h"
#include "base/event/kqueue.h"
#include "base/event/selfpipe.h"

#if HAVE_SYS_EPOLL_H
# define MM_EVENT_BACKEND_NEVENTS MM_EVENT_EPOLL_NEVENTS
#elif HAVE_SYS_EVENT_H
# define MM_EVENT_BACKEND_NEVENTS MM_EVENT_KQUEUE_NEVENTS
#endif

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

struct mm_event_backend_storage
{
	/* System-specific events storage. */
#if HAVE_SYS_EPOLL_H
	struct mm_event_epoll_storage storage;
#elif HAVE_SYS_EVENT_H
	struct mm_event_kqueue_storage storage;
#endif
};

void NONNULL(1)
mm_event_backend_prepare(struct mm_event_backend *backend);

void NONNULL(1)
mm_event_backend_cleanup(struct mm_event_backend *backend);

void NONNULL(1)
mm_event_backend_storage_prepare(struct mm_event_backend_storage *storage);

/*
 * Tell if the backend requires all change events to be serialized.
 *
 * The existing backends (epoll and kqueue) do not require this. So
 * this is just a stub for possible future implementation of select
 * or poll backends.
 */
static inline bool NONNULL(1)
mm_event_backend_serial(struct mm_event_backend *backend UNUSED)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return false;
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_listen(struct mm_event_backend *backend,
			struct mm_event_batch *changes,
			struct mm_event_listener *listener,
			mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_listen(&backend->backend, changes, listener, timeout);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_listen(&backend->backend, changes, listener, timeout);
#endif
}

static inline void NONNULL(1, 2)
mm_event_backend_change(struct mm_event_backend *backend, struct mm_event_change *change)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_change(&backend->backend, change);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_change(&backend->backend, change);
#endif
}

static inline void NONNULL(1)
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

static inline void NONNULL(1)
mm_event_backend_dampen(struct mm_event_backend *backend)
{
#if MM_EVENT_NATIVE_NOTIFY
	if (backend->native_notify)
		return;
#endif
	mm_selfpipe_drain(&backend->selfpipe);
}

#endif /* BASE_EVENT_BACKEND_H */
