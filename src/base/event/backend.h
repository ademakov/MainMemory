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
	/* Event loop self-pipe. */
	struct mm_selfpipe selfpipe;

	/* Events system-specific backend. */
#if HAVE_SYS_EPOLL_H
	struct mm_event_epoll backend;
#endif
#if HAVE_SYS_EVENT_H
	struct mm_event_kqueue backend;
#endif
};

void __attribute__((nonnull(1)))
mm_event_backend_prepare(struct mm_event_backend *backend);

void __attribute__((nonnull(1)))
mm_event_backend_cleanup(struct mm_event_backend *backend);

static inline void __attribute__((nonnull(1, 2, 3)))
mm_event_backend_listen(struct mm_event_backend *backend,
			struct mm_event_batch *changes,
			struct mm_event_batch *events,
			mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_listen(&backend->backend, changes, events, timeout);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_listen(&backend->backend, changes, events, timeout);
#endif
}

static inline void __attribute__((nonnull(1)))
mm_event_backend_notify(struct mm_event_backend *backend)
{
	mm_selfpipe_write(&backend->selfpipe);
}

static inline void __attribute__((nonnull(1)))
mm_event_backend_dampen(struct mm_event_backend *backend)
{
	mm_selfpipe_drain(&backend->selfpipe);
}

#endif /* BASE_EVENT_BACKEND_H */
