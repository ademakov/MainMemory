/*
 * thread.c - MainMemory threads.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#define _GNU_SOURCE

#include "thread.h"

#include "alloc.h"
#include "clock.h"
#include "log.h"
#include "trace.h"

#include <pthread.h>
#include <sched.h>

#if HAVE_LINUX_FUTEX_H
# include <unistd.h>
# include <linux/futex.h>
# include <sys/syscall.h>
#endif

#if HAVE_MACH_SEMAPHORE_H || HAVE_MACH_THREAD_POLICY_H
# include <mach/mach_init.h>
# if HAVE_MACH_SEMAPHORE_H
#  include <mach/semaphore.h>
#  include <mach/task.h>
# endif
# if HAVE_MACH_THREAD_POLICY_H
#  include <mach/thread_act.h>
#  include <mach/thread_policy.h>
# endif
#endif

struct mm_thread
{
	/* Underlying system thread. */
	pthread_t system_thread;

	/* The task start routine and its argument. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

#if HAVE_LINUX_FUTEX_H
	int wait_futex;
#elif HAVE_MACH_SEMAPHORE_H
	semaphore_t wait_sem;
#else
	pthread_mutex_t wait_lock;
	pthread_cond_t wait_cond;
	bool wait_flag;
#endif

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];
};

__thread struct mm_thread *mm_thread = NULL;

/**********************************************************************
 * Global thread data initialization and termination.
 **********************************************************************/

void
mm_thread_init()
{
	// TODO: have a global thread list used for debugging/statistics.
}

void
mm_thread_term()
{
}

/**********************************************************************
 * Thread attribute routines.
 **********************************************************************/

void
mm_thread_attr_init(struct mm_thread_attr *attr)
{
	memset(attr, 0, sizeof *attr);
}

void
mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag)
{
	attr->cpu_tag = cpu_tag;
}

void
mm_thread_attr_setstack(struct mm_thread_attr *attr,
			void *stack_base, uint32_t stack_size)
{
	attr->stack_base = stack_base;
	attr->stack_size = stack_size;
}

void
mm_thread_attr_setname(struct mm_thread_attr *attr, const char *name)
{
	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof attr->name)
			len = sizeof attr->name - 1;

		memcpy(attr->name, name, len);
	}
	attr->name[len] = 0;
}

/**********************************************************************
 * Thread creation routines.
 **********************************************************************/

static void
mm_thread_setstack_attr(pthread_attr_t *pthr_attr, struct mm_thread_attr *attr)
{
	if (attr->stack_size == 0) {
		// no-op
	} else if (attr->stack_base == NULL) {
		int rc = pthread_attr_setstacksize(pthr_attr, attr->stack_size);
		if (rc)
			mm_fatal(rc, "pthread_attr_setstacksize");
	} else {
		int rc = pthread_attr_setstack(pthr_attr,
					       attr->stack_base,
					       attr->stack_size);
		if (rc)
			mm_fatal(rc, "pthread_attr_setstack");
	}
}

#if ENABLE_SMP && HAVE_PTHREAD_SETAFFINITY_NP
static void
mm_thread_setaffinity(uint32_t cpu_tag)
{
	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(cpu_tag, &cpu_set);

	pthread_t tid = pthread_self();
	int rc = pthread_setaffinity_np(tid, sizeof cpu_set, &cpu_set);
	if (rc)
		mm_error(rc, "failed to set thread affinity");
}
#elif ENABLE_SMP && HAVE_MACH_THREAD_POLICY_H
static void
mm_thread_setaffinity(uint32_t cpu_tag)
{
	thread_affinity_policy_data_t policy;
	policy.affinity_tag = cpu_tag + 1;

	thread_t tid = mach_thread_self();
	kern_return_t kr = thread_policy_set(tid,
					     THREAD_AFFINITY_POLICY,
					     (thread_policy_t) &policy,
			THREAD_AFFINITY_POLICY_COUNT);
	if (kr != KERN_SUCCESS)
		mm_error(0, "failed to set thread affinity");
}
#else
# define mm_thread_setaffinity(cpu_tag) ((void) cpu_tag)
#endif

static void *
mm_thread_entry(void *arg)
{
	ENTER();

	struct mm_thread *thread = arg;

	// Set CPU affinity.
	mm_thread_setaffinity(thread->cpu_tag);

#if HAVE_PTHREAD_SETNAME_NP
	// Let the system know the thread name.
# ifdef __APPLE__
	pthread_setname_np(thread->name);
# else
	pthread_setname_np(pthread_self(), thread->name);
# endif
#endif

	// Set the thread-local pointer to the thread object.
	mm_thread = thread;

	// Run the required routine.
	mm_brief("start thread '%s'", mm_thread_name());
	thread->start(thread->start_arg);
	mm_brief("end thread '%s'", mm_thread_name());
	mm_flush();

	// Reset the thread pointer (just for balanced ENTER/LEAVE trace).
	mm_thread = NULL;

	LEAVE();
	return NULL;
}

struct mm_thread *
mm_thread_create(struct mm_thread_attr *attr,
		 mm_routine_t start, mm_value_t start_arg)
{
	ENTER();
	int rc;

	// Create a thread object.
	struct mm_thread *thread = mm_global_alloc(sizeof (struct mm_thread));
	thread->start = start;
	thread->start_arg = start_arg;

	// Set thread attributes.
	if (attr == NULL) {
		thread->cpu_tag = 0;
		memset(thread->name, 0, MM_THREAD_NAME_SIZE);
	} else {
		thread->cpu_tag = attr->cpu_tag;
		memcpy(thread->name, attr->name, MM_THREAD_NAME_SIZE);
	}

	// Initialize wait/signal control data.
#if HAVE_LINUX_FUTEX_H
	thread->wait_futex = 0;
#elif HAVE_MACH_SEMAPHORE_H
	kern_return_t kr = semaphore_create(mach_task_self(), &thread->wait_sem,
					    SYNC_POLICY_FIFO, 0);
	if (kr != KERN_SUCCESS)
		mm_error(0, "failed to create semaphore");
#else
	rc = pthread_mutex_init(&thread->wait_lock, NULL);
	if (rc)
		mm_fatal(rc, "pthread_mutex_init");
	rc = pthread_cond_init(&thread->wait_cond, NULL);
	if (rc)
		mm_fatal(rc, "pthread_cond_init");
	thread->wait_flag = true;
#endif

	// Set thread system attributes.
	pthread_attr_t pthr_attr;
	pthread_attr_init(&pthr_attr);
	if (attr != NULL)
		mm_thread_setstack_attr(&pthr_attr, attr);

	// Start the thread.
	rc = pthread_create(&thread->system_thread, &pthr_attr,
				mm_thread_entry, thread);
	if (rc)
		mm_fatal(rc, "pthread_create");
	pthread_attr_destroy(&pthr_attr);

	LEAVE();
	return thread;
}

/* Destroy a thread object. It is only safe to call this function upon
   the thread join. */
void
mm_thread_destroy(struct mm_thread *thread)
{
	ENTER();

#if HAVE_LINUX_FUTEX_H
	// nothing to destroy
#elif HAVE_MACH_SEMAPHORE_H
	semaphore_destroy(mach_task_self(), thread->wait_sem);
#else
	pthread_mutex_destroy(&thread->wait_lock);
	pthread_cond_destroy(&thread->wait_cond);
#endif

	mm_global_free(thread);

	LEAVE();
}

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

/* Cancel a running thread. */
void
mm_thread_cancel(struct mm_thread *thread)
{
	ENTER();

	int rc = pthread_cancel(thread->system_thread);
	if (rc)
		mm_error(rc, "pthread_cancel");

	LEAVE();
}

/* Wait for a thread exit. */
void
mm_thread_join(struct mm_thread *thread)
{
	ENTER();

	int rc = pthread_join(thread->system_thread, NULL);
	if (rc)
		mm_error(rc, "pthread_join");

	LEAVE();
}

void
mm_thread_yield(void)
{
	ENTER();

	sched_yield();

	LEAVE();
}

void
mm_thread_wait(void)
{
	ENTER();

	// Flush the log before a possible sleep.
	mm_flush();

#if HAVE_LINUX_FUTEX_H
	syscall(SYS_futex, mm_thread->wait_futex, FUTEX_WAIT, 0, NULL, NULL, 0);
#elif HAVE_MACH_SEMAPHORE_H
	semaphore_wait(mm_thread->wait_sem);
#else
	pthread_mutex_lock(&mm_thread->wait_lock);
	while (mm_thread->wait_flag)
		pthread_cond_wait(&mm_thread->wait_cond, &mm_thread->wait_lock);
	mm_thread->wait_flag = true;
	pthread_mutex_unlock(&mm_thread->wait_lock);
#endif

	LEAVE();
}

void
mm_thread_timedwait(mm_timeout_t timeout)
{
	ENTER();

	// Flush the log before a possible sleep.
	mm_flush();

#if HAVE_LINUX_FUTEX_H
	struct timespec ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	syscall(SYS_futex, mm_thread->wait_futex, FUTEX_WAIT, 0, &ts, NULL, 0);
#elif HAVE_MACH_SEMAPHORE_H
	mach_timespec_t ts;
	ts.tv_sec = (timeout / 1000000);
	ts.tv_nsec = (timeout % 1000000) * 1000;

	semaphore_timedwait(mm_thread->wait_sem, ts);
#else
	struct timespec ts;
	mm_timeval_t time = mm_clock_gettime_realtime() + timeout;
	ts.tv_sec = (time / 1000000);
	ts.tv_nsec = (time % 1000000) * 1000;

	pthread_mutex_lock(&mm_thread->wait_lock);
	while (mm_thread->wait_flag) {
		int rc = pthread_cond_timedwait(&mm_thread->wait_cond, &mm_thread->wait_lock, &ts);
		if (rc == ETIMEDOUT)
			break;
	}
	mm_thread->wait_flag = true;
	pthread_mutex_unlock(&mm_thread->wait_lock);
#endif

	LEAVE();
}

void
mm_thread_signal(struct mm_thread *thread)
{
	ENTER();

#if HAVE_LINUX_FUTEX_H
	syscall(SYS_futex, &thread->wait_futex, FUTEX_WAKE, 1, NULL, NULL, 0);
#elif HAVE_MACH_SEMAPHORE_H
	semaphore_signal(thread->wait_sem);
#else
	pthread_mutex_lock(&thread->wait_lock);
	thread->wait_flag = false;
	pthread_cond_signal(&thread->wait_cond);
	pthread_mutex_unlock(&thread->wait_lock);
#endif

	LEAVE();
}

/**********************************************************************
 * Thread information.
 **********************************************************************/

const char *
mm_thread_name(void)
{
	if (mm_thread == NULL)
		return "main";
	if (mm_thread->name[0] == 0)
		return "unnamed";
	return mm_thread->name;
}
