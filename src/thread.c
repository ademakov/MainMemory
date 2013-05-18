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

#include "thread.h"

#include "util.h"

#include <pthread.h>

struct mm_thread
{
	/* Underlying system thread. */
	pthread_t system_thread;

	/* The task start routine and its argument. */
	mm_routine_t start;
	uintptr_t start_arg;

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

static void *
mm_thread_entry(void *arg)
{
	ENTER();

	struct mm_thread *thread = arg;
	mm_print("start thread: %s", thread->name);

	// Set the thread-local pointer to the thread object.
	mm_thread = thread;

	// Run the required routine.
	thread->start(thread->start_arg);

	LEAVE();
	return NULL;
}

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

struct mm_thread *
mm_thread_create(struct mm_thread_attr *attr,
		 mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	// Create a thread object.
	struct mm_thread *thread = mm_alloc(sizeof (struct mm_thread));
	thread->start = start;
	thread->start_arg = start_arg;

	// Set thread name.
	if (attr == NULL) {
		memset(thread->name, 0, MM_THREAD_NAME_SIZE);
	} else {
		memcpy(thread->name, attr->name, MM_THREAD_NAME_SIZE);
	}

	// Set thread system attributes.
	pthread_attr_t pthr_attr;
	pthread_attr_init(&pthr_attr);
	if (attr != NULL) {
		mm_thread_setstack_attr(&pthr_attr, attr);

		// TODO: handle CPU tag.
	}

	int rc = pthread_create(&thread->system_thread, &pthr_attr,
				mm_thread_entry, thread);
	if (rc) {
		mm_fatal(rc, "pthread_create");
	}

	LEAVE();
	return thread;
}

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

/* Destroy a thread object. It is only safe to call this function upon
   the thread join. */
void
mm_thread_destroy(struct mm_thread *thread)
{
	ENTER();

	mm_free(thread);

	LEAVE();
}

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
	/* TODO: resolve <sched.h> vs "sched.h" conflict. */
	int sched_yield();

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
	return mm_thread == NULL ? "default" : mm_thread->name;
}
