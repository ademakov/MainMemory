/*
 * base/event/listener.c - MainMemory event listener.
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

#include "base/event/listener.h"

#include "base/event/dispatch.h"
#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"

#if ENABLE_LINUX_FUTEX
# include "arch/syscall.h"
# include <linux/futex.h>
# include <sys/syscall.h>
#elif ENABLE_MACH_SEMAPHORE
# include <mach/mach_init.h>
# include <mach/task.h>
#else
# include "base/clock.h"
#endif

void __attribute__((nonnull(1, 2)))
mm_listener_prepare(struct mm_listener *listener, struct mm_thread *thread)
{
	ENTER();

	listener->listen_stamp = 0;
	listener->notify_stamp = 0;

	listener->changes_state = MM_LISTENER_CHANGES_PRIVATE;
	listener->changes_stamp = 0;

	listener->arrival_stamp = 0;
	listener->handle_stamp = 0;

	mm_list_prepare(&listener->detach_list);

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	kern_return_t r = semaphore_create(mach_task_self(), &listener->semaphore,
					   SYNC_POLICY_FIFO, 0);
	if (r != KERN_SUCCESS)
		mm_fatal(0, "semaphore_create");
#else
	mm_monitor_prepare(&listener->monitor);
#endif

	mm_event_batch_prepare(&listener->changes, 256);

	listener->nevents = 0;

	listener->thread = thread;

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_listener_cleanup(struct mm_listener *listener)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	// Nothing to do for futexes.
#elif ENABLE_MACH_SEMAPHORE
	semaphore_destroy(mach_task_self(), listener->semaphore);
#else
	mm_monitor_cleanup(&listener->monitor);
#endif

	mm_event_batch_cleanup(&listener->changes);

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_signal(struct mm_listener *listener)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	mm_syscall_3(SYS_futex, (uintptr_t) &listener->notify_stamp, FUTEX_WAKE_PRIVATE, 1);
#elif ENABLE_MACH_SEMAPHORE
	semaphore_signal(listener->semaphore);
#else
	mm_monitor_lock(&listener->monitor); 
	mm_monitor_signal(&listener->monitor);
	mm_monitor_unlock(&listener->monitor);
#endif

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_timedwait(struct mm_listener *listener, uint32_t stamp, mm_timeout_t timeout)
{
	ENTER();

#if ENABLE_LINUX_FUTEX
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a sleep.
	mm_log_relay();

	int rc = mm_syscall_4(SYS_futex, (uintptr_t) &listener->notify_stamp,
			      FUTEX_WAIT_PRIVATE, stamp, (uintptr_t) &ts);
	if (rc != 0 && errno != EWOULDBLOCK && errno != ETIMEDOUT)
		mm_fatal(errno, "futex");
#elif ENABLE_MACH_SEMAPHORE
	(void) stamp;

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
	if (listener->notify_stamp == stamp)
		mm_monitor_timedwait(&listener->monitor, time);
	mm_monitor_unlock(&listener->monitor);
#endif

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_listener_notify(struct mm_listener *listener, struct mm_event_backend *backend)
{
	ENTER();

	// Compare the notify and listen stamps. If the notify stamp has not
	// been already bumped relative to the listen stamp then do it now.
	// Do it atomically so that only a thread that succeeds with this
	// is elected to send a wake-up notification to the target listener.
	const uint32_t listen_stamp = mm_memory_load(listener->listen_stamp);
	mm_memory_load_fence();
	const uint32_t notify_stamp = mm_memory_load(listener->notify_stamp);
	if (notify_stamp == (listen_stamp & ~MM_LISTENER_STATE_MASK)) {
		uint32_t next = notify_stamp + MM_LISTENER_STATE_MASK + 1;
		uint32_t prev = mm_atomic_uint32_cas(&listener->notify_stamp, notify_stamp, next);
		if (prev == notify_stamp) {
			// Get the current state of the listener. It might
			// become obsolete by the time the notification is
			// sent. This is not a problem however as it implies
			// the listener thread has woken up on its own and
			// seen all the sent data.
			//
			// Sometimes this might lead to an extra listener
			// wake up (if the listener makes a full cycle) or
			// a wrong listener being waken (if another listener
			// becomes polling). So listeners should be prepared
			// to get spurious wake up notifications.
			mm_listener_state_t state = listen_stamp & MM_LISTENER_STATE_MASK;
			if (state == MM_LISTENER_WAITING)
				mm_listener_signal(listener);
			else if (state == MM_LISTENER_POLLING)
				mm_event_backend_notify(backend);
		}
	}

	LEAVE();
}

static void __attribute__((nonnull(1)))
mm_listener_finish(struct mm_listener *listener, uint32_t listen_stamp)
{
	// Bump the listen stamp.
	listen_stamp += MM_LISTENER_STATE_MASK + 1;

	// Store it first as the notify stamp then as the proper listen stamp.
	// This order ensures that regardless of a possible race with the CAS
	// operation in the notify function the end result will be correct.
	mm_memory_store(listener->notify_stamp, listen_stamp);
	mm_memory_store_fence();
	mm_memory_store(listener->listen_stamp, listen_stamp);
}

void __attribute__((nonnull(1, 2, 3)))
mm_listener_poll(struct mm_listener *listener, struct mm_event_backend *backend,
		 struct mm_event_receiver *receiver, mm_timeout_t timeout)
{
	ENTER();

	// Get the current listener state.
	const uint32_t listen_stamp = listener->listen_stamp;
	ASSERT((listen_stamp & MM_LISTENER_STATE_MASK) == MM_LISTENER_RUNNING);

	if (timeout != 0) {
		// Cleanup stale event notifications.
		mm_event_backend_dampen(backend);

		// Advertise that the thread is about to sleep.
		uint32_t poll_stamp = listen_stamp | MM_LISTENER_POLLING;
		mm_memory_store(listener->listen_stamp, poll_stamp);

		mm_memory_strict_fence(); // TODO: store_load fence

		// Need to wait for a wake-up notification or timeout
		// unless an already pending notification is detected.
		if (listen_stamp != mm_memory_load(listener->notify_stamp))
			timeout = 0;
	}

	// Check incoming events and wait for notification/timeout.
	mm_event_backend_listen(backend, &listener->changes, receiver, timeout);

	// Advertise the start of another working cycle.
	mm_listener_finish(listener, listen_stamp);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_listener_wait(struct mm_listener *listener, mm_timeout_t timeout)
{
	ENTER();

	// Get the current listener state.
	const uint32_t listen_stamp = listener->listen_stamp;
	ASSERT((listen_stamp & MM_LISTENER_STATE_MASK) == MM_LISTENER_RUNNING);

	if (timeout != 0) {
		// Advertise that the thread is about to sleep.
		uint32_t wait_stamp = listen_stamp | MM_LISTENER_WAITING;
		mm_memory_store(listener->listen_stamp, wait_stamp);

		mm_memory_strict_fence(); // TODO: store_load fence

		// Wait for a wake-up notification or timeout unless
		// an already pending notification is detected.
		if (listen_stamp == mm_memory_load(listener->notify_stamp))
			mm_listener_timedwait(listener, listen_stamp, timeout);
	}

	// Advertise the start of another working cycle.
	mm_listener_finish(listener, listen_stamp);

	LEAVE();
}
