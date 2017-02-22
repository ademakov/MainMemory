/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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

#include "base/event/event.h"

#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/event/selfpipe.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

/**********************************************************************
 * I/O events support.
 **********************************************************************/

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_handler_t handler,
		    mm_event_occurrence_t input_mode, mm_event_occurrence_t output_mode,
		    mm_event_affinity_t target)
{
	ASSERT(fd >= 0);
	// It is forbidden to have both input and output ignored.
	VERIFY(input_mode != MM_EVENT_IGNORED || output_mode != MM_EVENT_IGNORED);

	sink->fd = fd;
	sink->target = MM_THREAD_NONE;
	sink->handler = handler;

#if ENABLE_SMP
	sink->receive_stamp = 0;
	sink->dispatch_stamp = 0;
	sink->complete_stamp = 0;
#endif
	sink->queued_events = 0;

	sink->loose_target = (target == MM_EVENT_LOOSE);
	sink->bound_target = (target == MM_EVENT_BOUND);

	sink->oneshot_input_trigger = false;
	sink->oneshot_output_trigger = false;
	sink->changed = false;

	if (input_mode == MM_EVENT_IGNORED) {
		sink->regular_input = false;
		sink->oneshot_input = false;
	} else if (input_mode == MM_EVENT_ONESHOT) {
		// Oneshot state cannot be properly managed for loose sinks.
		ASSERT(!sink->loose_target);
		sink->regular_input = false;
		sink->oneshot_input = true;
	} else {
		sink->regular_input = true;
		sink->oneshot_input = false;
	}

	if (output_mode == MM_EVENT_IGNORED) {
		sink->regular_output = false;
		sink->oneshot_output = false;
	} else if (output_mode == MM_EVENT_ONESHOT) {
		// Oneshot state cannot be properly managed for loose sinks.
		ASSERT(!sink->loose_target);
		sink->regular_output = false;
		sink->oneshot_output = true;
	} else {
		sink->regular_output = true;
		sink->oneshot_output = false;
	}

	return true;
}

void NONNULL(1)
mm_event_register_fd(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	mm_thread_t target = mm_event_target(sink);
	if (target == MM_THREAD_NONE) {
		sink->target = listener->target;
	} else {
		VERIFY(target == listener->target);
	}
	mm_event_listener_add(listener, sink, MM_EVENT_REGISTER);
}

void NONNULL(1)
mm_event_unregister_fd(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_UNREGISTER);
}

void NONNULL(1)
mm_event_unregister_faulty_fd(struct mm_event_fd *sink)
{
	struct mm_event_change change = { .kind = MM_EVENT_UNREGISTER, .sink = sink };
	struct mm_domain *domain = mm_domain_selfptr();
	struct mm_event_dispatch *dispatch = mm_domain_getdispatch(domain);
	mm_event_backend_change(&dispatch->backend, &change);
}

void NONNULL(1)
mm_event_trigger_input(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_INPUT);
}

void NONNULL(1)
mm_event_trigger_output(struct mm_event_fd *sink)
{
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_event_listener *listener = mm_thread_getlistener(thread);
	VERIFY(mm_event_target(sink) == listener->target);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_OUTPUT);
}
