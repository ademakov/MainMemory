/*
 * base/event/backend.h - MainMemory events system backend.
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

#ifndef BASE_EVENT_BACKEND_H
#define BASE_EVENT_BACKEND_H

#include "common.h"
#include "base/report.h"
#include "base/event/epoll.h"
#include "base/event/kqueue.h"

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
};

/* System-specific events storage. */
#if HAVE_SYS_EPOLL_H
# define mm_event_backend_local mm_event_epoll_local
#elif HAVE_SYS_EVENT_H
# define mm_event_backend_local mm_event_kqueue_local
#else
# error "Event backend is not implemented"
#endif

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_backend_prepare(struct mm_event_backend *backend);

void NONNULL(1)
mm_event_backend_cleanup(struct mm_event_backend *backend);

void NONNULL(1, 2)
mm_event_backend_local_prepare(struct mm_event_backend_local *local, struct mm_event_backend *backend);

void NONNULL(1)
mm_event_backend_local_cleanup(struct mm_event_backend_local *local);

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_event_backend_poll(struct mm_event_backend *backend, struct mm_event_backend_local *local, mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_poll(&backend->backend, local, timeout);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_poll(&backend->backend, local, timeout);
#endif
}

static inline void NONNULL(1)
mm_event_backend_notify(struct mm_event_backend *backend UNUSED)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_notify(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_notify(&backend->backend);
#endif
}

static inline void NONNULL(1)
mm_event_backend_notify_clean(struct mm_event_backend *backend UNUSED)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_notify_clean(&backend->backend);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_notify_clean(&backend->backend);
#endif
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

static inline bool NONNULL(1)
mm_event_backend_has_changes(struct mm_event_backend_local *local UNUSED)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return local->nevents != 0;
#endif
}

static inline bool NONNULL(1)
mm_event_backend_has_urgent_changes(struct mm_event_backend_local *local UNUSED)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return local->nunregister != 0;
#endif
}

static inline void NONNULL(1, 2)
mm_event_backend_flush(struct mm_event_backend *backend UNUSED, struct mm_event_backend_local *local UNUSED)
{
#if HAVE_SYS_EPOLL_H
	// Nothing to do.
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_flush(&backend->backend, local);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_register_fd(struct mm_event_backend *backend, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_register_fd(&backend->backend, local, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_register_fd(&backend->backend, local, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_unregister_fd(struct mm_event_backend *backend, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_unregister_fd(&backend->backend, local, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_unregister_fd(&backend->backend, local, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_enable_input(struct mm_event_backend *backend UNUSED, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_enable_input(&backend->backend, local, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_trigger_input(&backend->backend, local, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_enable_output(struct mm_event_backend *backend UNUSED, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_enable_output(&backend->backend, local, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_trigger_output(&backend->backend, local, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_disable_input(struct mm_event_backend *backend UNUSED, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_disable_input(&backend->backend, local, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_disable_output(struct mm_event_backend *backend UNUSED, struct mm_event_backend_local *local, struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_disable_output(&backend->backend, local, sink);
#endif
}

#endif /* BASE_EVENT_BACKEND_H */
