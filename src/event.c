/*
 * event.c - MainMemory event loop.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "event.h"

#include "alloc.h"
#include "core.h"
#include "exit.h"
#include "log.h"
#include "net.h"
#include "port.h"
#include "task.h"
#include "timer.h"
#include "trace.h"
#include "util.h"

#include <string.h>
#if HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif
#if HAVE_SYS_EVENT_H
# include <sys/event.h>
#endif
#include <sys/types.h>
#include <unistd.h>

#define MM_EVENT_NEVENTS	512
#define MM_EVENT_NENTRIES	((uint32_t) 256)

typedef enum {
	MM_EVENT_FD_REGISTER,
	MM_EVENT_FD_UNREGISTER,
	MM_EVENT_FD_TRIGGER_INPUT,
	MM_EVENT_FD_TRIGGER_OUTPUT,
} mm_event_entry_tag_t;

// Event change entry.
struct mm_event_entry
{
	mm_event_entry_tag_t tag;
	int fd;
	struct mm_event_fd *ev_fd;
};

struct mm_event_table
{
	// The epoll/kqueue descriptor.
	int event_fd;

	// The epoll/kevent list size.
	int nevents;

	// The change list indexes.
	uint32_t head_entry;
	uint32_t tail_entry;
	uint32_t last_entry;

	// The internal state lock.
	mm_task_lock_t lock;

	// The tasks blocked on send.
	struct mm_waitset blocked_senders;

	// The change list.
	struct mm_event_entry entries[MM_EVENT_NENTRIES];

	// Event-loop self-pipe.
	int selfpipe_read_fd;
	int selfpipe_write_fd;
	struct mm_event_fd selfevent;
	bool selfpipe_ready;

#ifdef HAVE_SYS_EPOLL_H
	// The epoll list.
	struct epoll_event events[MM_EVENT_NEVENTS];
#endif
#ifdef HAVE_SYS_EVENT_H
	// The kevent list.
	struct kevent events[MM_EVENT_NEVENTS];
#endif
};

/**********************************************************************
 * Event Handler Table.
 **********************************************************************/

// Event handler table size.
#define MM_EVENT_HANDLER_MAX	(255)

// Event handler descriptor.
struct mm_event_hd
{
	mm_event_handler_t handler;
};

// Event handler table.
static struct mm_event_hd mm_event_hd_table[MM_EVENT_HANDLER_MAX];

// The number of registered event handlers.
static int mm_event_hd_table_size;

// A dummy event handler.
static void
mm_event_dummy(mm_event_t event __attribute__((unused)),
	       void *data __attribute__((unused)))
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
	ASSERT(mm_event_hd_table_size == 0);
	(void) mm_event_register_handler(mm_event_dummy);
	ASSERT(mm_event_hd_table_size == 1);

	LEAVE();
}

/* Register an event handler in the table. */
mm_event_hid_t
mm_event_register_handler(mm_event_handler_t handler)
{
	ENTER();

	ASSERT(handler != NULL);
	ASSERT(mm_event_hd_table_size < MM_EVENT_HANDLER_MAX);

	mm_event_hid_t id = mm_event_hd_table_size++;
	mm_event_hd_table[id].handler = handler;

	DEBUG("registered event handler %d", id);

	LEAVE();
	return id;
}

/**********************************************************************
 * File descriptor event handling.
 **********************************************************************/

static inline void
mm_event_input(struct mm_event_fd *ev_fd)
{
	ENTER();

	mm_event_hid_t id = ev_fd->input_handler;
	ASSERT(id < mm_event_hd_table_size);

	if (ev_fd->oneshot_input)
		ev_fd->oneshot_input_trigger = 0;

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	(hd->handler)(MM_EVENT_INPUT, ev_fd);

	LEAVE();
}

static inline void
mm_event_output(struct mm_event_fd *ev_fd)
{
	ENTER();

	mm_event_hid_t id = ev_fd->output_handler;
	ASSERT(id < mm_event_hd_table_size);

	if (ev_fd->oneshot_output)
		ev_fd->oneshot_output_trigger = 0;

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	(hd->handler)(MM_EVENT_OUTPUT, ev_fd);

	LEAVE();
}

static inline void
mm_event_control(struct mm_event_fd *ev_fd, mm_event_t event)
{
	ENTER();

	mm_event_hid_t id = ev_fd->control_handler;
	ASSERT(id < mm_event_hd_table_size);

	struct mm_event_hd *hd = &mm_event_hd_table[id];
	(hd->handler)(event, ev_fd);

	LEAVE();
}

/**********************************************************************
 * epoll support.
 **********************************************************************/

#ifdef HAVE_SYS_EPOLL_H

static void
mm_event_init_sys(struct mm_event_table *events)
{
	ENTER();

	events->event_fd = epoll_create(511);
	if (events->event_fd < 0)
		mm_fatal(errno, "Failed to create epoll fd");

	events->nevents = 0;

	LEAVE();
}

static void
mm_event_free_sys(struct mm_event_table *events)
{
	ENTER();

	if (events->event_fd >= 0)
		close(events->event_fd);

	LEAVE();
}

static bool
mm_event_process_entry(struct mm_event_table *events,
		       struct mm_event_entry *entry)
{
	bool control_event = false;
	struct mm_event_fd *ev_fd = entry->ev_fd;

	int rc;
	struct epoll_event event;
	event.events = 0;
	event.data.ptr = ev_fd;

	switch (entry->tag) {
	case MM_EVENT_FD_REGISTER:
		if (ev_fd->input_handler)
			event.events |= EPOLLIN | EPOLLET | EPOLLRDHUP;
		if (ev_fd->output_handler)
			event.events |= EPOLLOUT | EPOLLET;

		rc = epoll_ctl(events->event_fd, EPOLL_CTL_ADD, entry->fd, &event);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		if (ev_fd->control_handler) {
			mm_event_control(ev_fd, MM_EVENT_REGISTER);
			control_event = true;
		}
		break;

	case MM_EVENT_FD_UNREGISTER:
		rc = epoll_ctl(events->event_fd, EPOLL_CTL_DEL, entry->fd, &event);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		if (ev_fd->control_handler) {
			mm_event_control(ev_fd, MM_EVENT_UNREGISTER);
			control_event = true;
		}
		break;

	case MM_EVENT_FD_TRIGGER_INPUT:
	case MM_EVENT_FD_TRIGGER_OUTPUT:
		break;

	default:
		ABORT();
	}

	return control_event;
}

bool
mm_event_collect(struct mm_event_table *events)
{
	ENTER();

	// Indicate if there were any control events processed.
	bool control_events = false;

	// Go through the change list.
	uint32_t head = events->head_entry;
	uint32_t last = events->last_entry;
	for (; last != head; last++) {
		uint32_t i = last % MM_EVENT_NENTRIES;
		control_events |= mm_event_process_entry(events, &events->entries[i]);
	}

	// Remember the last seen change.
	events->last_entry = last;
	uint32_t tail = events->tail_entry;
	if (tail != last) {
		mm_task_lock(&events->lock);
		events->tail_entry = last;
		if ((tail + MM_EVENT_NENTRIES) == events->head_entry)
			mm_waitset_broadcast(&events->blocked_senders, &events->lock);
		else
			mm_task_unlock(&events->lock);
	}

	LEAVE();
	return control_events;
}

bool
mm_event_poll(struct mm_event_table *events, mm_timeout_t timeout)
{
	ENTER();

	// Find the event wait timeout.
	timeout /= 1000;

	// Flush the log before possible sleep.
	mm_flush();

	// Poll the system for events.
	int n = epoll_wait(events->event_fd,
			   events->events, MM_EVENT_NEVENTS,
			   timeout);

	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		events->nevents = 0;
	} else {
		events->nevents = n;
	}

	LEAVE();
	return (events->nevents != 0);
}

void
mm_event_dispatch(struct mm_event_table *events)
{
	ENTER();

	// Process the received system events.
	for (int i = 0; i < events->nevents; i++) {
		struct epoll_event *event = &events->events[i];
		struct mm_event_fd *ev_fd = event->data.ptr;

		if ((event->events & EPOLLIN) != 0)
			mm_event_input(ev_fd);
		if ((event->events & EPOLLOUT) != 0)
			mm_event_output(ev_fd);

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
			mm_event_control(ev_fd, MM_EVENT_INPUT_ERROR);
		if ((event->events & (EPOLLERR | EPOLLHUP)) != 0)
			mm_event_control(ev_fd, MM_EVENT_OUTPUT_ERROR);
	}

	LEAVE();
}

#endif // HAVE_SYS_EPOLL_H

/**********************************************************************
 * kqueue/kevent support.
 **********************************************************************/

#ifdef HAVE_SYS_EVENT_H

static void
mm_event_init_sys(struct mm_event_table *events)
{
	ENTER();

	events->event_fd = kqueue();
	if (events->event_fd == -1)
		mm_fatal(errno, "Failed to create kqueue");

	events->nevents = 0;

	LEAVE();
}

static void
mm_event_free_sys(struct mm_event_table *events)
{
	ENTER();

	if (events->event_fd >= 0)
		close(events->event_fd);

	LEAVE();
}

static void
mm_event_process_entry(struct mm_event_table *events,
		       struct mm_event_entry *entry)
{
	struct mm_event_fd *ev_fd = entry->ev_fd;
	ASSERT(!ev_fd->changed);

	switch (entry->tag) {
	case MM_EVENT_FD_REGISTER:
		if (ev_fd->input_handler) {
			int flags;
			if (ev_fd->oneshot_input) {
				flags = EV_ADD | EV_ONESHOT;
				ev_fd->oneshot_input_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_READ, flags, 0, 0, ev_fd);
		}
		if (ev_fd->output_handler) {
			int flags;
			if (ev_fd->oneshot_output) {
				flags = EV_ADD | EV_ONESHOT;
				ev_fd->oneshot_output_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_WRITE, flags, 0, 0, ev_fd);
		}
		if (ev_fd->control_handler)
			ev_fd->changed = 1;
		break;

	case MM_EVENT_FD_UNREGISTER:
		if (ev_fd->input_handler
		    && (!ev_fd->oneshot_input
			|| ev_fd->oneshot_input_trigger)) {

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
		}
		if (ev_fd->output_handler
		    && (!ev_fd->oneshot_output
			|| ev_fd->oneshot_output_trigger)) {

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
		}
		if (ev_fd->control_handler)
			ev_fd->changed = 1;
		break;

	case MM_EVENT_FD_TRIGGER_INPUT:
		if (ev_fd->input_handler
		    && ev_fd->oneshot_input
		    && !ev_fd->oneshot_input_trigger) {

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_READ, EV_ADD | EV_ONESHOT,
			       0, 0, ev_fd);

			if (ev_fd->control_handler)
				ev_fd->changed = 1;
		}
		break;

	case MM_EVENT_FD_TRIGGER_OUTPUT:
		if (ev_fd->output_handler
		    && ev_fd->oneshot_output
		    && !ev_fd->oneshot_output_trigger) {

			ASSERT(events->nevents < MM_EVENT_NEVENTS);
			struct kevent *kp = &events->events[events->nevents++];
			EV_SET(kp, entry->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT,
			       0, 0, ev_fd);
		}
		break;

	default:
		ABORT();
	}
}

bool
mm_event_collect(struct mm_event_table *events)
{
	ENTER();
	ASSERT(events->tail_entry == events->last_entry);

	events->nevents = 0;

	// Go through the change list.
	uint32_t head = events->head_entry;
	uint32_t last = events->last_entry;
	for (; last != head; last++) {
		uint32_t i = last % MM_EVENT_NENTRIES;
		struct mm_event_entry *entry = &events->entries[i];

		// To simplify logic handle only one event related to a
		// particular fd per cycle.
		if (unlikely(entry->ev_fd->changed))
			break;

		mm_event_process_entry(events, entry);
	}

	// Remember the last seen change.
	events->last_entry = last;

	LEAVE();
	return (events->nevents != 0);
}

bool
mm_event_poll(struct mm_event_table *events, mm_timeout_t timeout)
{
	ENTER();

	// Calculate the event wait timeout.
	struct timespec ts;
	DEBUG("timeout: %lu", (unsigned long) timeout);
	ts.tv_sec = timeout / 1000000;
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Flush the log before a possible sleep.
	mm_flush();

	// Poll the system for events.
	int n = kevent(events->event_fd,
		       events->events, events->nevents,
		       events->events, MM_EVENT_NEVENTS,
		       &ts);

	DEBUG("kevent changed: %d, received: %d", events->nevents, n);
	if (n < 0) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
		events->nevents = 0;
	} else {
		events->nevents = n;
	}

	LEAVE();
	return (events->nevents || events->tail_entry != events->last_entry);
}

void
mm_event_dispatch(struct mm_event_table *events)
{
	ENTER();

	// Issue REG/UNREG events.
	uint32_t tail = events->tail_entry;
	uint32_t last = events->last_entry;
	for (uint32_t c = tail; c != last; c++) {
		uint32_t i = c % MM_EVENT_NENTRIES;
		struct mm_event_entry *entry = &events->entries[i];

		// Reset the change flag.
		struct mm_event_fd *ev_fd = entry->ev_fd;
		ev_fd->changed = 0;

		// Invoke the control handler with pertinent event.
		if (entry->tag == MM_EVENT_FD_REGISTER)
			mm_event_control(ev_fd, MM_EVENT_REGISTER);
		else if (entry->tag == MM_EVENT_FD_UNREGISTER)
			mm_event_control(ev_fd, MM_EVENT_UNREGISTER);
	}

	if (tail != last) {
		mm_task_lock(&events->lock);
		events->tail_entry = last;
		if ((tail + MM_EVENT_NENTRIES) == events->head_entry)
			mm_waitset_broadcast(&events->blocked_senders, &events->lock);
		else
			mm_task_unlock(&events->lock);
	}

	// Process the received system events.
	for (int i = 0; i < events->nevents; i++) {
		struct kevent *event = &events->events[i];

		if (event->filter == EVFILT_READ) {
			struct mm_event_fd *ev_fd = event->udata;
			mm_event_input(ev_fd);
			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_control(ev_fd, MM_EVENT_INPUT_ERROR);

		} else if (events->events[i].filter == EVFILT_WRITE) {
			struct mm_event_fd *ev_fd = event->udata;
			mm_event_output(ev_fd);
			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_control(ev_fd, MM_EVENT_OUTPUT_ERROR);
		}
	}

	LEAVE();
}

#endif // HAVE_SYS_EVENT_H

/**********************************************************************
 * Self-pipe support.
 **********************************************************************/

// Self pipe for event loop wakeup.
static mm_event_hid_t mm_event_selfpipe_handler;

static void
mm_event_selfpipe_ready(mm_event_t event __attribute__((unused)), void *data)
{
	struct mm_event_table *events
		= containerof(data, struct mm_event_table, selfevent);
	events->selfpipe_ready = true;
}

static void
mm_event_init_selfpipe(void)
{
	ENTER();

	// Register the self-pipe event handler.
	mm_event_selfpipe_handler = mm_event_register_handler(mm_event_selfpipe_ready);

	LEAVE();
}

/**********************************************************************
 * Event poll routines.
 **********************************************************************/

static mm_atomic_uint32_t mm_selfpipe_write_count;

struct mm_event_table *
mm_event_create_table(void)
{
	ENTER();

	struct mm_event_table *events = mm_global_alloc(sizeof(struct mm_event_table));

	// Initialize system specific resources.
	mm_event_init_sys(events);

	// Initialize generic data.
	events->nevents = 0;
	events->head_entry = 0;
	events->tail_entry = 0;
	events->last_entry = 0;
	events->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	mm_waitset_prepare(&events->blocked_senders);

	// Open an event-loop self-pipe.
	int fds[2];
	if (pipe(fds) < 0)
		mm_fatal(errno, "pipe()");
	mm_set_nonblocking(fds[0]);
	mm_set_nonblocking(fds[1]);
	events->selfpipe_read_fd = fds[0];
	events->selfpipe_write_fd = fds[1];
	events->selfpipe_ready = false;

	// Start serving the event loop self-pipe.
	mm_event_prepare_fd(&events->selfevent,
			    mm_event_selfpipe_handler, false,
			    0, false, 0);
	mm_event_register_fd(events, events->selfpipe_read_fd,
			     &events->selfevent);

	LEAVE();
	return events;
}

void
mm_event_destroy_table(struct mm_event_table *events)
{
	ENTER();

	mm_waitset_cleanup(&events->blocked_senders);

	// Close the event-loop self-pipe.
	close(events->selfpipe_read_fd);
	close(events->selfpipe_write_fd);

	// Release system specific resources.
	mm_event_free_sys(events);

	mm_global_free(events);

	LEAVE();
}

void
mm_event_notify(struct mm_event_table *events)
{
	ENTER();

	mm_atomic_uint32_inc(&mm_selfpipe_write_count);

	(void) write(events->selfpipe_write_fd, "", 1);

	LEAVE();
}

void
mm_event_dampen(struct mm_event_table *events)
{
	ENTER();

	// Drain the stale self-pipe notifications if any.
	if (events->selfpipe_ready) {
		events->selfpipe_ready = false;

		char dummy[64];
		while (read(events->selfpipe_read_fd, dummy, sizeof dummy) == sizeof dummy) {
			/* do nothing */
		}
	}

	LEAVE();
}

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static void
mm_event_send(struct mm_event_table *events, int fd,
	      mm_event_entry_tag_t tag, struct mm_event_fd *ev_fd)
{
again:
	mm_task_lock(&events->lock);

	uint32_t head = events->head_entry;
	uint32_t tail = events->tail_entry;
	if (unlikely(head == (tail + MM_EVENT_NENTRIES))) {
		mm_waitset_wait(&events->blocked_senders, &events->lock);
		mm_task_testcancel();
		goto again;
	}

	head %= MM_EVENT_NENTRIES;

	events->entries[head].fd = fd;
	events->entries[head].tag = tag;
	events->entries[head].ev_fd = ev_fd;
	events->head_entry++;

	mm_task_unlock(&events->lock);
}

bool
mm_event_prepare_fd(struct mm_event_fd *ev_fd,
		    mm_event_hid_t input_handler, bool input_oneshot,
		    mm_event_hid_t output_handler, bool output_oneshot,
		    mm_event_hid_t control_handler)
{
	ASSERT(input_handler || output_handler || control_handler);
	ASSERT(input_handler < mm_event_hd_table_size);
	ASSERT(output_handler < mm_event_hd_table_size);
	ASSERT(control_handler < mm_event_hd_table_size);

	ev_fd->input_handler = input_handler;
	ev_fd->output_handler = output_handler;
	ev_fd->control_handler = control_handler;

	ev_fd->changed = false;
	ev_fd->oneshot_input = input_oneshot;
	ev_fd->oneshot_input_trigger = false;
	ev_fd->oneshot_output = output_oneshot;
	ev_fd->oneshot_output_trigger = false;

	return true;
}

void
mm_event_register_fd(struct mm_event_table *events, int fd,
		     struct mm_event_fd *ev_fd)
{
	ENTER();
	ASSERT(fd >= 0);

	mm_event_send(events, fd, MM_EVENT_FD_REGISTER, ev_fd);

	LEAVE();
}

void
mm_event_unregister_fd(struct mm_event_table *events, int fd,
		       struct mm_event_fd *ev_fd)
{
	ENTER();
	ASSERT(fd >= 0);

	mm_event_send(events, fd, MM_EVENT_FD_UNREGISTER, ev_fd);

	LEAVE();
}

void
mm_event_trigger_input(struct mm_event_table *events, int fd,
		       struct mm_event_fd *ev_fd)
{
	ENTER();
	ASSERT(fd >= 0);

	mm_event_send(events, fd, MM_EVENT_FD_TRIGGER_INPUT, ev_fd);

	LEAVE();
}

void
mm_event_trigger_output(struct mm_event_table *events, int fd,
			struct mm_event_fd *ev_fd)
{
	ENTER();
	ASSERT(fd >= 0);

	mm_event_send(events, fd, MM_EVENT_FD_TRIGGER_OUTPUT, ev_fd);

	LEAVE();
}

/**********************************************************************
 * Event subsystem initialization and termination.
 **********************************************************************/

void
mm_event_init(void)
{
	ENTER();

	// Initialize generic data.
	mm_event_init_handlers();
	mm_event_init_selfpipe();

	LEAVE();
}

void
mm_event_term(void)
{
	ENTER();

	LEAVE();
}

void
mm_event_stats(void)
{
	uint32_t write = mm_memory_load(mm_selfpipe_write_count.value);
	mm_verbose("selfpipe stats: write = %u", write);
}
