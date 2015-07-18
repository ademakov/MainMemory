/*
 * base/event/event.h - MainMemory event loop.
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

#ifndef BASE_EVENT_EVENT_H
#define BASE_EVENT_EVENT_H

#include "common.h"
#include "base/list.h"

#if defined(HAVE_SYS_EPOLL_H)
# undef MM_ONESHOT_HANDLERS
#else
# define MM_ONESHOT_HANDLERS	1
#endif

/* Event types. */
typedef enum {
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_ATTACH,
	MM_EVENT_DETACH,
	MM_EVENT_REGISTER,
	MM_EVENT_UNREGISTER,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
	MM_EVENT_DISPATCH_STUB,

} mm_event_t;

/* Event details. */
struct mm_event
{
	mm_event_t event;
	struct mm_event_fd *ev_fd;
};

/**********************************************************************
 * Event subsystem initialization.
 **********************************************************************/

void mm_event_init(void);

/**********************************************************************
 * Event subsystem statistics.
 **********************************************************************/

void mm_event_stats(void);

/**********************************************************************
 * Event handlers.
 **********************************************************************/

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, void *data);

/* Event handler descriptor. */
struct mm_event_hdesc
{
	mm_event_handler_t handler;
};

/* Event handler table. */
extern struct mm_event_hdesc mm_event_hdesc_table[];

/* The number of registered event handlers. */
extern int mm_event_hdesc_table_size;

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

/* Event sink. */
struct mm_event_fd
{
	/* Pending detach list link. */
	struct mm_link detach_link;

	/* The file descriptor to watch. */
	int fd;

	/* Counters to detect detach feasibility. */
	uint32_t arrival_stamp;
	uint32_t detach_stamp;

	/* The thread the handler is pinned to. */
	mm_thread_t target;

	/* Event handers. */
	mm_event_hid_t handler;

	/* Event flags */
	unsigned attached : 1;
	unsigned pending_detach : 1;
	unsigned changed : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned oneshot_input_trigger : 1;
	unsigned regular_output;
	unsigned oneshot_output : 1;
	unsigned oneshot_output_trigger : 1;
};

bool __attribute__((nonnull(1)))
mm_event_prepare_fd(struct mm_event_fd *sink,
		    int fd, mm_event_hid_t handler,
		    bool regular_input, bool oneshot_input,
		    bool regular_output, bool oneshot_output);

static inline void __attribute__((nonnull(1)))
mm_event_handle(struct mm_event_fd *sink, mm_event_t event)
{
	mm_event_hid_t id = sink->handler;
	struct mm_event_hdesc *hd = &mm_event_hdesc_table[id];
	(hd->handler)(event, sink);
}

static inline mm_thread_t __attribute__((nonnull(1)))
mm_event_target(const struct mm_event_fd *sink)
{
	return sink->target;
}

static inline bool __attribute__((nonnull(1)))
mm_event_attached(const struct mm_event_fd *sink)
{
	return sink->attached;
}

#endif /* BASE_EVENT_EVENT_H */