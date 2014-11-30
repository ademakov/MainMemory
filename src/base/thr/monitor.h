/*
 * base/thr/monitor.h - MainMemory monitor thread synchronization.
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

#ifndef BASE_THR_MONITOR_H
#define	BASE_THR_MONITOR_H

#include "common.h"
#include <pthread.h>

struct mm_monitor
{
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

void mm_monitor_prepare(struct mm_monitor *monitor)
	__attribute__((nonnull(1)));

void mm_monitor_cleanup(struct mm_monitor *monitor)
	__attribute__((nonnull(1)));

static inline void __attribute__((nonnull(1)))
mm_monitor_lock(struct mm_monitor *monitor)
{
	pthread_mutex_lock(&monitor->lock);
}

static inline void __attribute__((nonnull(1)))
mm_monitor_unlock(struct mm_monitor *monitor)
{
	pthread_mutex_unlock(&monitor->lock);
}

static inline void __attribute__((nonnull(1)))
mm_monitor_signal(struct mm_monitor *monitor)
{
	pthread_cond_signal(&monitor->cond);
}

static inline void __attribute__((nonnull(1)))
mm_monitor_broadcast(struct mm_monitor *monitor)
{
	pthread_cond_broadcast(&monitor->cond);
}

void mm_monitor_wait(struct mm_monitor *monitor)
	__attribute__((nonnull(1)));

bool mm_monitor_timedwait(struct mm_monitor *monitor, mm_timeval_t realtime)
	__attribute__((nonnull(1)));

#endif /* BASE_THR_MONITOR_H */
