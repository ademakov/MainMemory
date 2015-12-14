/*
 * base/event/event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "base/event/epoll.h"
#include "base/event/selfpipe.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
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
		    mm_event_mode_t input_mode, mm_event_mode_t output_mode,
		    mm_event_target_t target)
{
	ASSERT(fd >= 0);
	ASSERT(handler > 0);
	ASSERT(handler < mm_event_hdesc_table_size);

	sink->fd = fd;
	sink->target = MM_THREAD_NONE;
	sink->handler = handler;

	sink->loose_target = (target == MM_EVENT_TARGET_LOOSE);
	sink->bound_target = (target == MM_EVENT_TARGET_BOUND);

	if (input_mode == MM_EVENT_IGNORED) {
		sink->regular_input = false;
		sink->oneshot_input = false;
#if MM_ONESHOT_HANDLERS
	} else if (input_mode == MM_EVENT_ONESHOT) {
		sink->regular_input = false;
		sink->oneshot_input = true;
#endif
	} else {
		sink->regular_input = true;
		sink->oneshot_input = false;
	}

	if (output_mode == MM_EVENT_IGNORED) {
		sink->regular_output = false;
		sink->oneshot_output = false;
#if MM_ONESHOT_HANDLERS
	} else if (output_mode == MM_EVENT_ONESHOT) {
		sink->regular_output = false;
		sink->oneshot_output = true;
#endif
	} else {
		sink->regular_output = true;
		sink->oneshot_output = false;
	}

	sink->arrival_stamp = 0;
	sink->changed = 0;
	sink->oneshot_input_trigger = 0;
	sink->oneshot_output_trigger = 0;

	sink->detach_stamp = 0;
	sink->attached = 0;
	sink->pending_detach = 0;

	return true;
}

void NONNULL(1)
mm_event_handle(struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();
	DEBUG("handle sink %p, fd %d, target %u, event %d",
	      sink, sink->fd, mm_event_target(sink), event);
	ASSERT(event != MM_EVENT_ATTACH && event != MM_EVENT_DETACH);
	ASSERT(sink->loose_target || mm_event_target(sink) == mm_thread_self());

	mm_event_hid_t id = sink->handler;
	ASSERT(id < mm_event_hdesc_table_size);
	struct mm_event_hdesc *hd = &mm_event_hdesc_table[id];

	// If the event sink is detached then attach it before handling
	// the received event. If it is in the state of pending detach
	// then quit this state.
	if (!sink->attached) {
		DEBUG("attach %d to %d, stamp %u", sink->fd,
		      mm_thread_self(), sink->detach_stamp);
		(hd->handler)(MM_EVENT_ATTACH, sink);
		sink->attached = 1;
	} else if (sink->pending_detach) {
		mm_list_delete(&sink->detach_link);
		sink->pending_detach = 0;
	}

	(hd->handler)(event, sink);

	LEAVE();
}

void NONNULL(1)
mm_event_detach(struct mm_event_fd *sink, uint32_t stamp)
{
	ENTER();
	DEBUG("detach sink %p, fd %d, target %u, stamp %u\n",
	      sink, sink->fd, mm_event_target(sink), stamp);
	ASSERT(mm_event_target(sink) == mm_thread_self());

	mm_event_hid_t id = sink->handler;
	ASSERT(id < mm_event_hdesc_table_size);
	struct mm_event_hdesc *hd = &mm_event_hdesc_table[id];

	ASSERT(sink->pending_detach);
	mm_list_delete(&sink->detach_link);
	sink->pending_detach = 0;

	ASSERT(sink->attached);
	(hd->handler)(MM_EVENT_DETACH, sink);
	sink->attached = 0;

	mm_memory_store_fence();
	mm_memory_store(sink->detach_stamp, stamp);

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
