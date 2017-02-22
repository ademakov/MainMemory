/*
 * base/event/handle.h - MainMemory event handling.
 *
 * Copyright (C) 2016-2017  Aleksey Demakov
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

#ifndef BASE_EVENT_HANDLE_H
#define BASE_EVENT_HANDLE_H

#include "base/event/event.h"
#include "base/event/backend.h"

/* Mark a sink as having a pending event after it is received from
   the system. */
static inline void
mm_event_update(struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	sink->receive_stamp++;
#endif
}

/* Check if a sink has some not yet fully processed events. */
static inline bool NONNULL(1)
mm_event_active(const struct mm_event_fd *sink UNUSED)
{
#if ENABLE_SMP
	// TODO: acquire memory fence
	mm_event_stamp_t stamp = mm_memory_load(sink->complete_stamp);
	return sink->receive_stamp != stamp;
#else
	return true;
#endif
}

/* Start processing of an event after it is delivered to the target
   thread. */
static inline void NONNULL(1)
mm_event_handle_basic(struct mm_event_fd *sink, mm_event_t event)
{
#if ENABLE_SMP
	/* Count the received event. */
	sink->dispatch_stamp++;
#endif
	/* Schedule it for processing. */
	(sink->handler)(event, sink);
}

/* Start processing of an event after it is delivered to the target
   thread. Also reset oneshot I/O state if needed. */
static inline void NONNULL(1)
mm_event_handle(struct mm_event_fd *sink, mm_event_t event)
{
	/* Start processing the event. */
	mm_event_handle_basic(sink, event);
	/* Perform backend-specific I/O state reset. */
	if (event < MM_EVENT_OUTPUT)
		mm_event_backend_reset_input(sink);
	else if (event < MM_EVENT_DISABLE)
		mm_event_backend_reset_output(sink);
}

/* Start processing of an event after it is delivered to the target
   thread. The event must be an I/O event and the call must be made
   by a poller thread. */
static inline void NONNULL(1)
mm_event_handle_poller_io(struct mm_event_fd *sink, mm_event_t event)
{
	/* Start processing the event. */
	mm_event_handle_basic(sink, event);
	/* Perform backend-specific I/O state reset. */
	if (event < MM_EVENT_OUTPUT)
		mm_event_backend_reset_input(sink);
	else
		mm_event_backend_reset_output(sink);
}

#endif /* BASE_EVENT_HANDLE_H */
