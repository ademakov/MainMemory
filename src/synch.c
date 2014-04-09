/*
 * synch.c - MainMemory simple thread synchronization.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "synch.h"

#include "alloc.h"
#include "clock.h"
#include "event.h"
#include "log.h"
#include "trace.h"

#include <pthread.h>

#if HAVE_LINUX_FUTEX_H
# include <unistd.h>
# include <linux/futex.h>
# include <sys/syscall.h>
#endif

#if HAVE_MACH_SEMAPHORE_H
# include <mach/mach_init.h>
# if HAVE_MACH_SEMAPHORE_H
#  include <mach/semaphore.h>
#  include <mach/task.h>
# endif
#endif

#define MM_THREAD_SYNCH_COND	0x796c4000
#define MM_THREAD_SYNCH_POLL	0x796c4001

#if HAVE_LINUX_FUTEX_H
# define MM_THREAD_SYNCH_FAST	0x796c4002
#endif

#if HAVE_MACH_SEMAPHORE_H
# define MM_THREAD_SYNCH_MACH	0x796c4003
#endif

/**********************************************************************
 * Synchronization based on pthread condition variables.
 **********************************************************************/

/* A synchronization object based on pthread. */
struct mm_synch_cond
{
	struct mm_synch base;

	pthread_mutex_t lock;
	pthread_cond_t cond;
};

struct mm_synch *
mm_synch_create_cond(void)
{
	ENTER();

	struct mm_synch_cond *cond = mm_global_alloc(sizeof(struct mm_synch_cond));

	cond->base.value.value = 0;
	cond->base.magic = MM_THREAD_SYNCH_COND;

	int rc = pthread_mutex_init(&cond->lock, NULL);
	if (rc)
		mm_fatal(rc, "pthread_mutex_init");
	rc = pthread_cond_init(&cond->cond, NULL);
	if (rc)
		mm_fatal(rc, "pthread_cond_init");

	LEAVE();
	return &cond->base;
}

static void
mm_synch_destroy_cond(struct mm_synch *synch)
{
	struct mm_synch_cond *cond = (struct mm_synch_cond *) synch;

	pthread_mutex_destroy(&cond->lock);
	pthread_cond_destroy(&cond->cond);

	mm_global_free(cond);
}

static void
mm_synch_wait_cond(struct mm_synch *synch)
{
	struct mm_synch_cond *cond = (struct mm_synch_cond *) synch;

	pthread_mutex_lock(&cond->lock);

	while (cond->base.value.value == 0) {
		int err = pthread_cond_wait(&cond->cond, &cond->lock);
		if (err)
			mm_fatal(err, "pthread_cond_wait");
	}

	cond->base.value.value = 0;

	pthread_mutex_unlock(&cond->lock);
}

static bool
mm_synch_timedwait_cond(struct mm_synch *synch, mm_timeout_t timeout)
{
	bool rc = true;

	struct mm_synch_cond *cond = (struct mm_synch_cond *) synch;

	struct timespec ts;
	mm_timeval_t time = mm_clock_gettime_realtime() + timeout;
	ts.tv_sec = (time / 1000000);
	ts.tv_nsec = (time % 1000000) * 1000;

	pthread_mutex_lock(&cond->lock);

	while (cond->base.value.value == 0) {
		int err = pthread_cond_timedwait(&cond->cond, &cond->lock, &ts);
		if (err) {
			if (err != ETIMEDOUT)
				mm_fatal(err, "pthread_cond_timedwait");
			rc = false;
			break;
		}
	}

	cond->base.value.value = 0;

	pthread_mutex_unlock(&cond->lock);
	return rc;
}

static void
mm_synch_signal_cond(struct mm_synch *synch)
{
	struct mm_synch_cond *cond = (struct mm_synch_cond *) synch;

	pthread_mutex_lock(&cond->lock);

	cond->base.value.value = 1;
	pthread_cond_signal(&cond->cond);

	pthread_mutex_unlock(&cond->lock);
}

static void
mm_synch_clear_cond(struct mm_synch *synch)
{
	struct mm_synch_cond *cond = (struct mm_synch_cond *) synch;

	mm_memory_store(cond->base.value.value, 0);
	mm_memory_strict_fence();
}

/**********************************************************************
 * Synchronization based on event loop poll.
 **********************************************************************/

#define POLL_GUARD 0

struct mm_synch_poll
{
	struct mm_synch base;

	struct mm_event_table *events;
	bool waiting;
};

struct mm_synch *
mm_synch_create_event_poll(struct mm_event_table *events)
{
	ENTER();

	struct mm_synch_poll *poll = mm_global_alloc(sizeof(struct mm_synch_poll));

	poll->base.value.value = 0;
	poll->base.magic = MM_THREAD_SYNCH_POLL;
	poll->events = events;
	poll->waiting = false;

	LEAVE();
	return &poll->base;
}

static void
mm_synch_destroy_poll(struct mm_synch *synch)
{
	mm_global_free(synch);
}

static void
mm_synch_wait_poll(struct mm_synch *synch)
{
	struct mm_synch_poll *poll = (struct mm_synch_poll *) synch;

#if POLL_GUARD
	// Advertise that the thread is about to sleep.
	mm_memory_store(poll->waiting, true);

	// FIXME: have a store-atomic fence.
	mm_memory_fence();

	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&poll->base.value, 0);
	if (value == 0)
		mm_event_poll(poll->events, MM_TIMEOUT_INFINITE);

	// Advertise that the thread has woken up.
	mm_memory_store(poll->waiting, false);
#else
	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&poll->base.value, 0);
	if (value == 0)
		mm_event_poll(poll->events, MM_TIMEOUT_INFINITE);
#endif
}

static bool
mm_synch_timedwait_poll(struct mm_synch *synch, mm_timeout_t timeout)
{
	bool rc = true;

	struct mm_synch_poll *poll = (struct mm_synch_poll *) synch;

#if POLL_GUARD
	// Advertise that the thread is about to sleep.
	mm_memory_store(poll->waiting, true);

	// FIXME: have a store-atomic fence.
	mm_memory_fence();

	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&poll->base.value, 0);
	if (value == 0)
		rc = mm_event_poll(poll->events, timeout);

	// Advertise that the thread has woken up.
	mm_memory_store(poll->waiting, false);
#else
	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&poll->base.value, 0);
	if (value == 0)
		rc = mm_event_poll(poll->events, timeout);
#endif

	return rc;
}

static void
mm_synch_signal_poll(struct mm_synch *synch)
{
	struct mm_synch_poll *poll = (struct mm_synch_poll *) synch;

	mm_memory_store(poll->base.value.value, 1);

#if POLL_GUARD
	// FIXME: have a store-load fence.
	mm_memory_strict_fence();

	if (mm_memory_load(poll->waiting))
		mm_event_notify(poll->events);
#else
	mm_event_notify(poll->events);
#endif
}

static void
mm_synch_clear_poll(struct mm_synch *synch)
{
	struct mm_synch_poll *poll = (struct mm_synch_poll *) synch;

	// Cleanup stale event signals.
	mm_event_dampen(poll->events);

	mm_memory_store(poll->base.value.value, 0);
	mm_memory_strict_fence();
}

/**********************************************************************
 * Synchronization based on Linux futexes.
 **********************************************************************/

#if MM_THREAD_SYNCH_FAST

static struct mm_synch *
mm_synch_create_fast(void)
{
	ENTER();

	struct mm_synch *fast = mm_global_alloc(sizeof(struct mm_synch));

	fast->value.value = 0;
	fast->magic = MM_THREAD_SYNCH_FAST;

	LEAVE();
	return fast;
}

static void
mm_synch_destroy_fast(struct mm_synch *synch)
{
	mm_global_free(synch);
}

static void
mm_synch_wait_fast(struct mm_synch *synch)
{
	syscall(SYS_futex, &synch->value.value, FUTEX_WAIT, 0, NULL, NULL, 0);
}

static bool
mm_synch_timedwait_fast(struct mm_synch *synch, mm_timeout_t timeout)
{
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	syscall(SYS_futex, &synch->value.value, FUTEX_WAIT, 0, &ts, NULL, 0);
	return true;
}

static void
mm_synch_signal_fast(struct mm_synch *synch)
{
	syscall(SYS_futex, &synch->value.value, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static void
mm_synch_clear_fast(struct mm_synch *synch)
{
	mm_memory_store(synch->value.value, 0);
	mm_memory_strict_fence();
}

#endif

/**********************************************************************
 * Synchronization based on Mach semaphores.
 **********************************************************************/

#if MM_THREAD_SYNCH_MACH

struct mm_synch_mach
{
	struct mm_synch base;

	semaphore_t sem;

	bool waiting;
};

static struct mm_synch *
mm_synch_create_mach(void)
{
	ENTER();

	struct mm_synch_mach *mach = mm_global_alloc(sizeof(struct mm_synch_mach));

	mach->base.value.value = 0;
	mach->base.magic = MM_THREAD_SYNCH_MACH;
	mach->waiting = false;

	kern_return_t r = semaphore_create(mach_task_self(), &mach->sem,
					   SYNC_POLICY_FIFO, 0);
	if (r != KERN_SUCCESS)
		mm_fatal(0, "semaphore_create");

	LEAVE();
	return &mach->base;
}

static void
mm_synch_destroy_mach(struct mm_synch *synch)
{
	struct mm_synch_mach *mach = (struct mm_synch_mach *) synch;

	semaphore_destroy(mach_task_self(), mach->sem);

	mm_global_free(mach);
}

static void
mm_synch_wait_mach(struct mm_synch *synch)
{
	struct mm_synch_mach *mach = (struct mm_synch_mach *) synch;

	// Advertise that the thread is about to sleep.
	mm_memory_store(mach->waiting, true);

	// FIXME: have an atomic-atomic fence.
	mm_memory_fence();

	// FIXME: protect against spurious wakeups.
	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&mach->base.value, 0);
	if (value == 0) {
		semaphore_wait(mach->sem);
	}

	// Advertise that the thread has woken up.
	mm_memory_store(mach->waiting, false);
}

static bool
mm_synch_timedwait_mach(struct mm_synch *synch, mm_timeout_t timeout)
{
	bool rc = true;

	struct mm_synch_mach *mach = (struct mm_synch_mach *) synch;

	mach_timespec_t ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Advertise that the thread is about to sleep.
	mm_memory_store(mach->waiting, true);

	// FIXME: have a store-atomic fence.
	mm_memory_fence();

	// FIXME: protect against spurious wakeups.
	// Check to see if there are already some wake signals pending.
	uint32_t value = mm_atomic_uint32_fetch_and_set(&mach->base.value, 0);
	if (value == 0) {
		kern_return_t r = semaphore_timedwait(mach->sem, ts);
		if (r != KERN_SUCCESS) {
			if (r != KERN_OPERATION_TIMED_OUT)
				mm_fatal(0, "semaphore_timedwait");
			rc = false;
		}
	}

	// Advertise that the thread has woken up.
	mm_memory_store(mach->waiting, false);

	return rc;
}

static void
mm_synch_signal_mach(struct mm_synch *synch)
{
	struct mm_synch_mach *mach = (struct mm_synch_mach *) synch;

	mm_memory_store(mach->base.value.value, 1);

	// FIXME: have a store-load fence.
	mm_memory_strict_fence();

	if (mm_memory_load(mach->waiting))
		semaphore_signal(mach->sem);
}

static void
mm_synch_clear_mach(struct mm_synch *synch)
{
	struct mm_synch_mach *mach = (struct mm_synch_mach *) synch;

	mm_memory_store(mach->base.value.value, 0);
	mm_memory_strict_fence();
}

#endif

/**********************************************************************
 * Common synchronization routines.
 **********************************************************************/

struct mm_synch *
mm_synch_create(void)
{
#if MM_THREAD_SYNCH_FAST
	return mm_synch_create_fast();
#elif MM_THREAD_SYNCH_MACH
	return mm_synch_create_mach();
#else
	return mm_synch_create_cond();
#endif
}

void
mm_synch_destroy(struct mm_synch *synch)
{
	ENTER();

	switch (synch->magic) {
	case MM_THREAD_SYNCH_COND:
		mm_synch_destroy_cond(synch);
		break;

	case MM_THREAD_SYNCH_POLL:
		mm_synch_destroy_poll(synch);
		break;

#if MM_THREAD_SYNCH_FAST
	case MM_THREAD_SYNCH_FAST:
		mm_synch_destroy_fast(synch);
		break;
#endif

#if MM_THREAD_SYNCH_MACH
	case MM_THREAD_SYNCH_MACH:
		mm_synch_destroy_mach(synch);
		break;
#endif

	default:
		ABORT();
	}

	LEAVE();
}

void
mm_synch_wait(struct mm_synch *synch)
{
	ENTER();

	// Flush the log before a possible sleep.
	mm_flush();

	switch (synch->magic) {
	case MM_THREAD_SYNCH_COND:
		mm_synch_wait_cond(synch);
		break;

	case MM_THREAD_SYNCH_POLL:
		mm_synch_wait_poll(synch);
		break;

#if MM_THREAD_SYNCH_FAST
	case MM_THREAD_SYNCH_FAST:
		mm_synch_wait_fast(synch);
		break;
#endif

#if MM_THREAD_SYNCH_MACH
	case MM_THREAD_SYNCH_MACH:
		mm_synch_wait_mach(synch);
		break;
#endif

	default:
		ABORT();
	}

	LEAVE();
}

bool
mm_synch_timedwait(struct mm_synch *synch, mm_timeout_t timeout)
{
	ENTER();
	bool rc;

	// Flush the log before a possible sleep.
	mm_flush();

	switch (synch->magic) {
	case MM_THREAD_SYNCH_COND:
		rc = mm_synch_timedwait_cond(synch, timeout);
		break;

	case MM_THREAD_SYNCH_POLL:
		rc = mm_synch_timedwait_poll(synch, timeout);
		break;

#if MM_THREAD_SYNCH_FAST
	case MM_THREAD_SYNCH_FAST:
		rc = mm_synch_timedwait_fast(synch, timeout);
		break;
#endif

#if MM_THREAD_SYNCH_MACH
	case MM_THREAD_SYNCH_MACH:
		rc = mm_synch_timedwait_mach(synch, timeout);
		break;
#endif

	default:
		ABORT();
	}

	LEAVE();
	return rc;
}

void
mm_synch_signal(struct mm_synch *synch)
{
	ENTER();

	switch (synch->magic) {
	case MM_THREAD_SYNCH_COND:
		mm_synch_signal_cond(synch);
		break;

	case MM_THREAD_SYNCH_POLL:
		mm_synch_signal_poll(synch);
		break;

#if MM_THREAD_SYNCH_FAST
	case MM_THREAD_SYNCH_FAST:
		mm_synch_signal_fast(synch);
		break;
#endif

#if MM_THREAD_SYNCH_MACH
	case MM_THREAD_SYNCH_MACH:
		mm_synch_signal_mach(synch);
		break;
#endif

	default:
		ABORT();
	}

	LEAVE();
}

void
mm_synch_clear(struct mm_synch *synch)
{
	ENTER();

	switch (synch->magic) {
	case MM_THREAD_SYNCH_COND:
		mm_synch_clear_cond(synch);
		break;

	case MM_THREAD_SYNCH_POLL:
		mm_synch_clear_poll(synch);
		break;

#if MM_THREAD_SYNCH_FAST
	case MM_THREAD_SYNCH_FAST:
		mm_synch_clear_fast(synch);
		break;
#endif

#if MM_THREAD_SYNCH_MACH
	case MM_THREAD_SYNCH_MACH:
		mm_synch_clear_mach(synch);
		break;
#endif

	default:
		ABORT();
	}

	LEAVE();
}
