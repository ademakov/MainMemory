/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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
 * Event handlers.
 **********************************************************************/

/* Event handler table size. */
#define MM_EVENT_HANDLER_MAX	(255)

/* Event handler table. */
static struct mm_event_hdesc mm_event_hdesc_table[MM_EVENT_HANDLER_MAX];

/* The number of registered event handlers. */
static int mm_event_hdesc_table_size;

// A dummy event handler.
static void
mm_event_dummy(mm_event_t event UNUSED, void *data UNUSED)
{
	DEBUG("hmm, dummy event handler invoked.");
}

// Initialize the event handler table.
static void
mm_event_init_handlers(void)
{
	ENTER();
	ASSERT(MM_EVENT_HANDLER_MAX < 256);

	// Register dummy handler with zero id.
	ASSERT(mm_event_hdesc_table_size == 0);
	(void) mm_event_register_handler(mm_event_dummy);
	ASSERT(mm_event_hdesc_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_event_hid_t
mm_event_register_handler(mm_event_handler_t handler)
{
	ENTER();

	ASSERT(handler != NULL);
	ASSERT(mm_event_hdesc_table_size < MM_EVENT_HANDLER_MAX);

	mm_event_hid_t id = mm_event_hdesc_table_size++;
	mm_event_hdesc_table[id].handler = handler;

	DEBUG("registered event handler %d", id);

	LEAVE();
	return id;
}

/**********************************************************************
 * I/O events support.
 **********************************************************************/

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_hid_t handler,
		    mm_event_occurrence_t input_mode, mm_event_occurrence_t output_mode,
		    mm_event_affinity_t target)
{
	ASSERT(fd >= 0);
	ASSERT(handler > 0);
	ASSERT(handler < mm_event_hdesc_table_size);

	sink->fd = fd;
	sink->target = MM_THREAD_NONE;

#if ENABLE_SMP
	sink->receive_stamp = 0;
	sink->dispatch_stamp = 0;
	sink->complete_stamp = 0;
#endif

	sink->handler = handler;
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

	mm_event_hid_t id = sink->handler;
	ASSERT(id < mm_event_hdesc_table_size);
	struct mm_event_hdesc *hd = &mm_event_hdesc_table[id];

	// Count the received event.
	mm_event_update_dispatch_stamp(sink);
	DEBUG("sink %d got event %u on thread %u", sink->fd, sink->dispatch_stamp, mm_thread_self());

	// Handle the received event.
	(hd->handler)(event, sink);

	LEAVE();
}

void NONNULL(1)
mm_event_detach(struct mm_event_fd *sink)
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

	// Initialize generic data.
	mm_event_init_handlers();
	mm_selfpipe_init();

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
