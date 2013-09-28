/*
 * thread.c - MainMemory threads.
 *
 * Copyright (C) 2013  Aleksey Demakov
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
#include "log.h"
#include "trace.h"

#include <pthread.h>
#include <sched.h>

#if HAVE_MACH_THREAD_POLICY_H
# include <mach/mach.h>
# include <mach/thread_policy.h>
#endif

struct mm_thread
{
	/* Underlying system thread. */
	pthread_t system_thread;

	/* The task start routine and its argument. */
	mm_routine_t start;
	uintptr_t start_arg;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

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
	ENTER();

	if (likely(name != NULL)) {
		size_t len = strlen(name);
		if (len >= sizeof attr->name)
			len = sizeof attr->name - 1;
		memcpy(attr->name, name, len);
		attr->name[len] = 0;
	} else {
		attr->name[0] = 0;
	}

	LEAVE();
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
		if (rc) {
			mm_fatal(rc, "pthread_attr_setstacksize");
		}
	} else {
		int rc = pthread_attr_setstack(pthr_attr,
					       attr->stack_base,
					       attr->stack_size);
		if (rc) {
			mm_fatal(rc, "pthread_attr_setstack");
		}
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
	if (rc) {
		mm_error(rc, "failed to set thread affinity");
	}
}
#elif ENABLE_SMP && HAVE_MACH_THREAD_POLICY_H
static void
mm_thread_setaffinity(uint32_t cpu_tag)
{
	thread_affinity_policy_data_t policy;
	policy.affinity_tag = cpu_tag + 1;

	thread_t tid = mach_thread_self();
	int rc = thread_policy_set(tid,
				   THREAD_AFFINITY_POLICY,
				   (thread_policy_t) &policy,
				   THREAD_AFFINITY_POLICY_COUNT);
	if (rc != KERN_SUCCESS) {
		mm_error(0, "failed to set thread affinity");
	}
}
#else
# define mm_thread_setaffinity(cpu_tag) ((void) cpu_tag)
#endif

static void *
mm_thread_entry(void *arg)
{
	ENTER();

	struct mm_thread *thread = arg;

	if (thread->name[0]) {
		mm_brief("start '%s' thread", thread->name);
	} else {
		mm_brief("start a thread");
	}

	// Set CPU affinity.
	mm_thread_setaffinity(thread->cpu_tag);

	// Set the thread-local pointer to the thread object.
	mm_thread = thread;

	// Run the required routine.
	thread->start(thread->start_arg);

	// Reset the thread pointer (just for balanced ENTER/LEAVE trace).
	mm_thread = NULL;

	LEAVE();
	return NULL;
}

struct mm_thread *
mm_thread_create(struct mm_thread_attr *attr,
		 mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	// Create a thread object.
	struct mm_thread *thread = mm_alloc(sizeof (struct mm_thread));
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

	// Set thread system attributes.
	pthread_attr_t pthr_attr;
	pthread_attr_init(&pthr_attr);
	if (attr != NULL) {
		mm_thread_setstack_attr(&pthr_attr, attr);
	}

	// Start the thread.
	int rc = pthread_create(&thread->system_thread, &pthr_attr,
				mm_thread_entry, thread);
	if (rc) {
		mm_fatal(rc, "pthread_create");
	}
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

	mm_free(thread);

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
	if (rc) {
		mm_error(rc, "pthread_cancel");
	}

	LEAVE();
}

/* Wait for a thread exit. */
void
mm_thread_join(struct mm_thread *thread)
{
	ENTER();

	int rc = pthread_join(thread->system_thread, NULL);
	if (rc) {
		mm_error(rc, "pthread_join");
	}

	LEAVE();
}

void
mm_thread_yield(void)
{
	ENTER();

	sched_yield();

	LEAVE();
}

/**********************************************************************
 * Thread information.
 **********************************************************************/

const char *
mm_thread_name(void)
{
	return mm_thread == NULL ? "main" : mm_thread->name;
}
