/*
 * base/thread/monitor.c - MainMemory monitor thread synchronization.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#include "base/thread/monitor.h"

#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"

void __attribute__((nonnull(1)))
mm_thread_monitor_prepare(struct mm_thread_monitor *monitor)
{
	ENTER();

	int err = pthread_mutex_init(&monitor->lock, NULL);
	if (err)
		mm_fatal(err, "pthread_mutex_init");
	err = pthread_cond_init(&monitor->cond, NULL);
	if (err)
		mm_fatal(err, "pthread_cond_init");

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_thread_monitor_cleanup(struct mm_thread_monitor *monitor)
{
	ENTER();

	pthread_mutex_destroy(&monitor->lock);
	pthread_cond_destroy(&monitor->cond);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_thread_monitor_wait(struct mm_thread_monitor *monitor)
{
	ENTER();

	// Publish the log before a possible sleep.
	mm_log_relay();

	int err = pthread_cond_wait(&monitor->cond, &monitor->lock);
	if (err)
		mm_fatal(err, "pthread_cond_wait");

	LEAVE();
}

bool __attribute__((nonnull(1)))
mm_thread_monitor_timedwait(struct mm_thread_monitor *monitor, mm_timeval_t realtime)
{
	ENTER();
	bool rc = true;

	struct timespec ts;
	ts.tv_sec = (realtime / 1000000);
	ts.tv_nsec = (realtime % 1000000) * 1000;

	// Publish the log before a possible sleep.
	mm_log_relay();

	int err = pthread_cond_timedwait(&monitor->cond, &monitor->lock, &ts);
	if (err) {
		if (err != ETIMEDOUT)
			mm_fatal(err, "pthread_cond_timedwait");
		rc = false;
	}

	LEAVE();
	return rc;
}
