/*
 * event/listener.c - MainMemory event listener.
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

#include "event/listener.h"
#include "event/dispatch.h"

#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"

#if ENABLE_LINUX_FUTEX
# include <unistd.h>
# include <linux/futex.h>
# include <sys/syscall.h>
#elif ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#else
# include "base/sys/clock.h"
#endif

void __attribute__((nonnull(1)))
mm_listener_prepare(struct mm_listener *listener)
{
	ENTER();

	listener->listen_stamp = 1;
	listener->notify_stamp = 0;
	listener->state = MM_LISTENER_RUNNING;

#if ENABLE_MACH_SEMAPHORE
	kern_return_t r = semaphore_create(mach_task_self(), &listener->semaphore,
					   SYNC_POLICY_FIFO, 0);
	if (r != KERN_SUCCESS)
		mm_fatal(0, "semaphore_create");
#else
	mm_monitor_prepare(&listener->monitor);
#endif

	mm_event_batch_prepare(&listener->changes);
	mm_event_batch_prepare(&listener->events);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_listener_cleanup(struct mm_listener *listener)
{
	ENTER();

#if ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_monitor_cleanup(&listener->monitor);
#endif

	mm_event_batch_cleanup(&listener->changes);
	mm_event_batch_cleanup(&listener->events);

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_signal(struct mm_listener *listener)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	syscall(SYS_futex, &listener->notify_stamp, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
#elif ENABLE_MACH_SEMAPHORE
	semaphore_signal(listener->semaphore);
#else
	mm_monitor_signal(&listener->monitor);
#endif

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_timedwait(struct mm_listener *listener, mm_timeout_t timeout)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a sleep.
	mm_log_relay();

	int rc = syscall(SYS_futex, &listener->notify_stamp, FUTEX_WAIT_PRIVATE, notify_stamp, &ts, NULL, 0);
	if (rc != 0 && unlikely(errno != ETIMEDOUT))
		mm_fatal(errno, "futex");
#elif ENABLE_MACH_SEMAPHORE
	mach_timespec_t ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a sleep.
	mm_log_relay();

	kern_return_t r = semaphore_timedwait(listener->semaphore, ts);
	if (r != KERN_SUCCESS && unlikely(r != KERN_OPERATION_TIMED_OUT))
		mm_fatal(0, "semaphore_timedwait");
#else
	mm_timeval_t time = mm_clock_gettime_realtime() + timeout;

	mm_monitor_lock(&listener->monitor); 
	if (listener->listen_stamp != listener->notify_stamp)
		mm_monitor_timedwait(&listener->monitor, time);
	mm_monitor_unlock(&listener->monitor);
#endif

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_handle(struct mm_listener *listener)
{
	ENTER();

	for (unsigned int i = 0; i < listener->events.nevents; i++) {
		struct mm_event *event = &listener->events.events[i];
		switch (event->event) {
		case MM_EVENT_INPUT:
			mm_event_input(event->ev_fd);
			break;
		case MM_EVENT_OUTPUT:
			mm_event_output(event->ev_fd);
			break;
		case MM_EVENT_REGISTER:
		case MM_EVENT_UNREGISTER:
		case MM_EVENT_INPUT_ERROR:
		case MM_EVENT_OUTPUT_ERROR:
			mm_event_control(event->ev_fd, event->event);
			break;
		}
	}

	mm_event_batch_clear(&listener->events);

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_listener_notify(struct mm_listener *listener, struct mm_dispatch *dispatch)
{
	ENTER();

	// Make sure that any data that might have been sent to the target
	// listener thread becomes visible.
	mm_memory_strict_fence();

	// Compare notify and listen stamps. If the notify stamp lags behind
	// the listen stamp then synchronize them. Do it atomically so that
	// only a thread that succeeds in doing so is elected to send a wakeup
	// notification to the target listener.
	const uint32_t notify_stamp = mm_memory_load(listener->notify_stamp);
	const uint32_t listen_stamp = mm_memory_load(listener->listen_stamp);
	if (notify_stamp != listen_stamp &&
	    notify_stamp == mm_atomic_uint32_cas(&listener->notify_stamp,
					         notify_stamp,
					         listen_stamp)) {

		// Get the current state of the listener. It might become
		// obsolete by the time the notification is sent. This is not
		// a problem however as it implies the listener thread has
		// woken up on its own and seen all the sent data.
		//
		// Sometimes this might lead to an extra listener wakeup (if
		// the listener makes a full cycle) or a wrong listener being
		// waken (if another listener becomes polling). So listeners
		// should be prepared to get spurious wake up notifications.
		mm_listener_state_t state = mm_memory_load(listener->state);

		// Send a wakeup notification.
		if (state == MM_LISTENER_WAITING)
			mm_listener_signal(listener);
		else if (state == MM_LISTENER_POLLING)
			mm_dispatch_notify(dispatch);
	}

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_listener_listen(struct mm_listener *listener, struct mm_dispatch *dispatch, mm_timeout_t timeout)
{
	ENTER();

	bool has_pending_changes = false;
	struct mm_listener *polling_listener = NULL;
	struct mm_listener *waiting_listener = NULL;

	mm_task_lock(&dispatch->lock);

	if (dispatch->polling_listener == NULL) {
		dispatch->polling_listener = listener;

		mm_event_batch_append(&listener->changes, &dispatch->pending_changes);
		mm_event_batch_clear(&dispatch->pending_changes);
	} else {
		mm_list_insert(&dispatch->waiting_listeners, &listener->link);

		has_pending_changes = mm_listener_has_changes(listener);
		if (has_pending_changes) {
			mm_event_batch_append(&dispatch->pending_changes, &listener->changes);
			mm_event_batch_clear(&listener->changes);
		}
	}

	polling_listener = dispatch->polling_listener;

	mm_task_unlock(&dispatch->lock);

	if (listener == polling_listener) {

		if (mm_listener_has_urgent_changes(listener)) {
			DEBUG("urgent events");
			timeout = 0;
		}

		if (unlikely(timeout == 0)) {
			mm_dispatch_listen(dispatch, &listener->changes,
						 &listener->events, 0);
		} else {
			// TODO: spin holding CPU a little checking for notifications

			// Advertise that the thread is about to sleep.
			mm_memory_store(listener->state, MM_LISTENER_POLLING);

			mm_memory_strict_fence();

			uint32_t notify_stamp = mm_memory_load(listener->notify_stamp);
			uint32_t listen_stamp = mm_memory_load(listener->listen_stamp);

			if (listen_stamp == notify_stamp)
				timeout = 0;

			mm_dispatch_listen(dispatch, &listener->changes,
						 &listener->events, timeout);

			// Advertise that the thread has woken up.
			mm_memory_store(listener->state, MM_LISTENER_RUNNING);
		}

		// Dispatch received events.
		mm_listener_handle(listener);

		// Cleanup stale event notifications.
		mm_dispatch_dampen(dispatch);

	} else {
		if (has_pending_changes)
			mm_listener_notify(polling_listener, dispatch);

		if (likely(timeout != 0)) {
			// TODO: spin holding CPU a little checking for notifications

			// Advertise that the thread is about to sleep.
			mm_memory_store(listener->state, MM_LISTENER_WAITING);

			mm_memory_strict_fence(); // TODO: store_load fence

			uint32_t notify_stamp = mm_memory_load(listener->notify_stamp);
			uint32_t listen_stamp = mm_memory_load(listener->listen_stamp);

			if (listen_stamp != notify_stamp)
				mm_listener_timedwait(listener, timeout);

			// Advertise that the thread has woken up.
			mm_memory_store(listener->state, MM_LISTENER_RUNNING);
		}
	}

	// Advertise that the thread starts another working cycle.
	mm_memory_store(listener->notify_stamp, listener->listen_stamp);
	mm_memory_store(listener->listen_stamp, listener->listen_stamp + 1);

	mm_task_lock(&dispatch->lock);

	if (listener == polling_listener) {
		dispatch->polling_listener = NULL;
#if 0
		has_changes = !mm_event_batch_empty(&dispatch->pending_changes);
		if (has_changes && !mm_list_empty(&dispatch->waiting_listeners)) {
			struct mm_list *link = mm_list_head(&dispatch->waiting_listeners);
			waiting_listener = containerof(link, struct mm_listener, link);
		}
#endif
	} else {
		mm_list_delete(&listener->link);
	}

	mm_task_unlock(&dispatch->lock);

	if (listener == polling_listener) {
		if (waiting_listener != NULL)
			mm_listener_notify(waiting_listener, dispatch);
		mm_event_batch_clear(&listener->changes);
	}

	LEAVE();
}
