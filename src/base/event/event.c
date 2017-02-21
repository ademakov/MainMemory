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
	ASSERT(handler > 0);
	ASSERT(handler < mm_event_hdesc_table_size);

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
		sink->regular_output = false;
		sink->oneshot_output = true;
	} else {
		sink->regular_output = true;
		sink->oneshot_output = false;
	}

	return true;
}

void NONNULL(1, 2)
mm_event_register_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch)
{
	mm_thread_t thread = mm_event_target(sink);
	if (thread == MM_THREAD_NONE) {
		thread = sink->target = mm_thread_self();
	} else {
		ASSERT(mm_event_target(sink) == thread);
	}
	struct mm_event_listener *listener = mm_event_dispatch_listener(dispatch, thread);
	mm_event_listener_add(listener, sink, MM_EVENT_REGISTER);
}

void NONNULL(1, 2)
mm_event_unregister_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch)
{
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_event_listener *listener = mm_event_dispatch_listener(dispatch, thread);
	mm_event_listener_add(listener, sink, MM_EVENT_UNREGISTER);
}

void NONNULL(1, 2)
mm_event_unregister_faulty_fd(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch)
{
	struct mm_event_change change = { .kind = MM_EVENT_UNREGISTER, .sink = sink };
	mm_event_backend_change(&dispatch->backend, &change);
}

void NONNULL(1, 2)
mm_event_trigger_input(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch)
{
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_event_listener *listener = mm_event_dispatch_listener(dispatch, thread);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_INPUT);
}

void NONNULL(1, 2)
mm_event_trigger_output(struct mm_event_fd *sink, struct mm_event_dispatch *dispatch)
{
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_event_listener *listener = mm_event_dispatch_listener(dispatch, thread);
	mm_event_listener_add(listener, sink, MM_EVENT_TRIGGER_OUTPUT);
}

void NONNULL(1)
mm_event_convey(struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();
	ASSERT(sink->loose_target || mm_event_target(sink) == mm_thread_self());

	// Count the received event.
	mm_event_update_dispatch_stamp(sink);
	DEBUG("sink %d got event %u on thread %u", sink->fd, sink->dispatch_stamp, mm_thread_self());

	// Handle the received event.
	(sink->handler)(event, sink);

	LEAVE();
}

void NONNULL(1)
mm_event_complete(struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(sink->loose_target || mm_event_target(sink) == mm_thread_self());

	// Mark the sink as detached.
	mm_event_update_complete_stamp(sink);
	DEBUG("sink %d done event %u on thread %u", sink->fd, sink->dispatch_stamp, mm_thread_self());

	LEAVE();
}

/**********************************************************************
 * Event subsystem initialization.
 **********************************************************************/

void
mm_event_init(void)
{
	ENTER();

#if HAVE_SYS_EPOLL_H
	mm_event_epoll_init();
#endif

	LEAVE();
}

/**********************************************************************
 * Event subsystem statistics.
 **********************************************************************/

void
mm_event_stats(void)
{
//	mm_selfpipe_stats();
}
