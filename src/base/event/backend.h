/*
 * base/event/backend.h - MainMemory events system backend.
 *
 * Copyright (C) 2015-2018  Aleksey Demakov
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

/* System-specific events storage. */
#if HAVE_SYS_EPOLL_H
# define mm_event_backend_storage mm_event_epoll_storage
#elif HAVE_SYS_EVENT_H
# define mm_event_backend_storage mm_event_kqueue_storage
#else
struct mm_event_backend_storage;
#endif

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_backend_prepare(struct mm_event_backend *backend, struct mm_event_backend_storage *some_storage);

void NONNULL(1)
mm_event_backend_cleanup(struct mm_event_backend *backend);

void NONNULL(1)
mm_event_backend_storage_prepare(struct mm_event_backend_storage *storage);

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_event_backend_poll(struct mm_event_backend *backend, struct mm_event_backend_storage *storage,
		      mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_poll(&backend->backend, storage, timeout);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_poll(&backend->backend, storage, timeout);
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
mm_event_backend_notify_clean(struct mm_event_backend *backend)
{
#if MM_EVENT_NATIVE_NOTIFY
	if (backend->native_notify) {
# if HAVE_SYS_EPOLL_H
		mm_event_epoll_notify_clean(&backend->backend);
# endif
		return;
	}
#endif
	mm_selfpipe_clean(&backend->selfpipe);
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

static inline bool NONNULL(1)
mm_event_backend_has_changes(struct mm_event_backend_storage *storage UNUSED)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return storage->nevents != 0;
#endif
}

static inline bool NONNULL(1)
mm_event_backend_has_urgent_changes(struct mm_event_backend_storage *storage UNUSED)
{
#if HAVE_SYS_EPOLL_H
	return false;
#elif HAVE_SYS_EVENT_H
	return storage->nunregister != 0;
#endif
}

static inline void NONNULL(1, 2)
mm_event_backend_flush(struct mm_event_backend *backend UNUSED, struct mm_event_backend_storage *storage UNUSED)
{
#if HAVE_SYS_EPOLL_H
	// Nothing to do.
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_flush(&backend->backend, storage);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_register_fd(struct mm_event_backend *backend, struct mm_event_backend_storage *storage UNUSED,
			     struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_register_fd(&backend->backend, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_register_fd(&backend->backend, storage, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_unregister_fd(struct mm_event_backend *backend, struct mm_event_backend_storage *storage,
			       struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_unregister_fd(&backend->backend, storage, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_unregister_fd(&backend->backend, storage, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_trigger_input(struct mm_event_backend *backend, struct mm_event_backend_storage *storage UNUSED,
			       struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_trigger_input(&backend->backend, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_trigger_input(&backend->backend, storage, sink);
#endif
}

static inline void NONNULL(1, 2, 3)
mm_event_backend_trigger_output(struct mm_event_backend *backend, struct mm_event_backend_storage *storage UNUSED,
			        struct mm_event_fd *sink)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_trigger_output(&backend->backend, sink);
#elif HAVE_SYS_EVENT_H
	mm_event_kqueue_trigger_output(&backend->backend, storage, sink);
#endif
}

/**********************************************************************
 * Interface for handling events delivered to the target thread.
 **********************************************************************/

/* Prepare a poller thread for handling received events. */
static inline void NONNULL(1)
mm_event_backend_poller_start(struct mm_event_backend_storage *storage UNUSED)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_poller_start(storage);
#endif
}

/* Make a poller thread done with received events. */
static inline void NONNULL(1, 2)
mm_event_backend_poller_finish(struct mm_event_backend *backend UNUSED, struct mm_event_backend_storage *storage UNUSED)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_poller_finish(&backend->backend, storage);
#endif
}

/* Start processing of an event after it is accepted by the poller
   thread. */
static inline void NONNULL(1, 2)
mm_event_backend_poller_input(struct mm_event_backend_storage *storage UNUSED, struct mm_event_fd *sink, uint32_t flag)
{
	/* Start processing the event. */
	mm_event_handle_input(sink, flag);
	/* Perform backend-specific I/O state reset. */
#if HAVE_SYS_EPOLL_H
	if ((sink->flags & MM_EVENT_ONESHOT_INPUT) != 0)
		mm_event_epoll_reset_poller_input(storage, sink);
#endif
}
static inline void NONNULL(1, 2)
mm_event_backend_poller_output(struct mm_event_backend_storage *storage UNUSED, struct mm_event_fd *sink, uint32_t flag)
{
	/* Start processing the event. */
	mm_event_handle_output(sink, flag);
	/* Perform backend-specific I/O state reset. */
#if HAVE_SYS_EPOLL_H
	if ((sink->flags & MM_EVENT_ONESHOT_OUTPUT) != 0)
		mm_event_epoll_reset_poller_output(storage, sink);
#endif
}

/* Start processing of an event after it is delivered to the target
   thread. */
static inline void NONNULL(1)
mm_event_backend_target_input(struct mm_event_fd *sink, uint32_t flag)
{
	/* Start processing the event. */
	mm_event_handle_input(sink, flag);
	/* Perform backend-specific I/O state reset. */
#if HAVE_SYS_EPOLL_H
	if ((sink->flags & MM_EVENT_ONESHOT_INPUT) != 0)
		mm_event_epoll_reset_input(sink);
#endif
}
static inline void NONNULL(1)
mm_event_backend_target_output(struct mm_event_fd *sink, uint32_t flag)
{
	/* Start processing the event. */
	mm_event_handle_output(sink, flag);
	/* Perform backend-specific I/O state reset. */
#if HAVE_SYS_EPOLL_H
	if ((sink->flags & MM_EVENT_ONESHOT_OUTPUT) != 0)
		mm_event_epoll_reset_output(sink);
#endif
}

#endif /* BASE_EVENT_BACKEND_H */
