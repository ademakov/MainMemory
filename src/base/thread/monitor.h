/*
 * base/thread/monitor.h - MainMemory monitor thread synchronization.
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

#ifndef BASE_THREAD_MONITOR_H
#define BASE_THREAD_MONITOR_H

#include "common.h"
#include <pthread.h>

struct mm_thread_monitor
{
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

void __attribute__((nonnull(1)))
mm_thread_monitor_prepare(struct mm_thread_monitor *monitor);

void __attribute__((nonnull(1)))
mm_thread_monitor_cleanup(struct mm_thread_monitor *monitor);

static inline void __attribute__((nonnull(1)))
mm_thread_monitor_lock(struct mm_thread_monitor *monitor)
{
	pthread_mutex_lock(&monitor->lock);
}

static inline void __attribute__((nonnull(1)))
mm_thread_monitor_unlock(struct mm_thread_monitor *monitor)
{
	pthread_mutex_unlock(&monitor->lock);
}

static inline void __attribute__((nonnull(1)))
mm_thread_monitor_signal(struct mm_thread_monitor *monitor)
{
	pthread_cond_signal(&monitor->cond);
}

static inline void __attribute__((nonnull(1)))
mm_thread_monitor_broadcast(struct mm_thread_monitor *monitor)
{
	pthread_cond_broadcast(&monitor->cond);
}

void __attribute__((nonnull(1)))
mm_thread_monitor_wait(struct mm_thread_monitor *monitor);

bool __attribute__((nonnull(1)))
mm_thread_monitor_timedwait(struct mm_thread_monitor *monitor, mm_timeval_t realtime);

#endif /* BASE_THREAD_MONITOR_H */
