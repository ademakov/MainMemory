/*
 * base/thr/monitor.c - MainMemory monitor thread synchronization.
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

#include "base/thr/monitor.h"
#include "base/log/error.h"
#include "base/log/trace.h"

void
mm_monitor_prepare(struct mm_monitor *monitor)
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

void
mm_monitor_cleanup(struct mm_monitor *monitor)
{
	ENTER();

	pthread_mutex_destroy(&monitor->lock);
	pthread_cond_destroy(&monitor->cond);

	LEAVE();
}

void
mm_monitor_wait(struct mm_monitor *monitor)
{
	ENTER();

	int err = pthread_cond_wait(&monitor->cond, &monitor->lock);
	if (err)
		mm_fatal(err, "pthread_cond_wait");

	LEAVE();
}

bool
mm_monitor_timedwait(struct mm_monitor *monitor, mm_timeval_t realtime)
{
	ENTER();
	bool rc = true;

	struct timespec ts;
	ts.tv_sec = (realtime / 1000000);
	ts.tv_nsec = (realtime % 1000000) * 1000;

	int err = pthread_cond_timedwait(&monitor->cond, &monitor->lock, &ts);
	if (err) {
		if (err != ETIMEDOUT)
			mm_fatal(err, "pthread_cond_timedwait");
		rc = false;
	}

	LEAVE();
	return rc;
}
