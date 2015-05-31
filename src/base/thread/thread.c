/*
 * base/thread/thread.c - MainMemory threads.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "base/thread/thread.h"

#include "base/list.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/alloc.h"
#include "base/mem/space.h"
#include "base/thread/domain.h"

#include <pthread.h>
#include <sched.h>

#if HAVE_MACH_THREAD_POLICY_H
# include <mach/mach_init.h>
# include <mach/thread_act.h>
# include <mach/thread_policy.h>
#endif

struct mm_thread
{
#if ENABLE_SMP
	/* Private memory space. */
	struct mm_private_space space;
#endif

	/* The log message storage. */
	struct mm_queue log_queue;

	/* Thread domain. */
	struct mm_domain *domain;
	mm_core_t domain_index;
	struct mm_barrier_local domain_barrier;

	/* Underlying system thread. */
	pthread_t system_thread;

	/* The thread start routine and its argument. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* CPU affinity tag. */
	uint32_t cpu_tag;

	/* The thread name. */
	char name[MM_THREAD_NAME_SIZE];

#if ENABLE_TRACE
	/* Thread trace context. */
	struct mm_trace_context trace;
#endif
};

static struct mm_thread mm_thread_main = {
	.log_queue = {
		.head = { NULL },
		.tail = &mm_thread_main.log_queue.head
	},

	.name = "main",

#if ENABLE_TRACE
	.trace = {
		.owner = "[main]",
		.level = 0,
		.recur = 0
	}
#endif
};

__thread struct mm_thread *__mm_thread_self = &mm_thread_main;

/**********************************************************************
 * Global thread data initialization and termination.
 **********************************************************************/

// TODO: have a global thread list used for debugging/statistics.

void
mm_thread_init()
{
	mm_thread_main.system_thread = pthread_self();
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
mm_thread_attr_setdomain(struct mm_thread_attr *attr,
			 struct mm_domain *domain,
			 mm_core_t domain_index)
{
	attr->domain = domain;
	attr->domain_index = domain_index;
}

void
mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag)
{
	attr->cpu_tag = cpu_tag;
}

void __attribute__((nonnull(1)))
mm_thread_attr_setprivatespace(struct mm_thread_attr *attr, bool private_space)
{
	attr->private_space = private_space;
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
	struct mm_thread *thread = arg;

	// Set thread-specific data.
	__mm_thread_self = thread;
	__mm_domain_self = thread->domain;

#if ENABLE_TRACE
	mm_trace_context_prepare(&thread->trace, "[%s]", thread->name);
#endif
	ENTER();

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

	// Wait until all threads from the same domain start.
	// This ensures that domain thread data is complete and
	// threads might communicate.
	if (thread->domain != NULL) {
		mm_barrier_local_init(&thread->domain_barrier);
		mm_barrier_wait(&thread->domain->barrier,
				&thread->domain_barrier);
	}

	// Run the required routine.
	thread->start(thread->start_arg);

	LEAVE();
	mm_log_relay();

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
#if ENABLE_SMP
	bool private_space = false;
#endif
	if (attr == NULL) {
		thread->domain = NULL;
		thread->domain_index = 0;
		thread->cpu_tag = 0;
		strcpy(thread->name, "unnamed");
	} else {
		thread->domain = attr->domain;
		thread->domain_index = attr->domain_index;
		thread->cpu_tag = attr->cpu_tag;
#if ENABLE_SMP
		private_space = attr->private_space;
#endif
		if (attr->name[0])
			memcpy(thread->name, attr->name, MM_THREAD_NAME_SIZE);
		else
			strcpy(thread->name, "unnamed");
	}

	// Initialize private memory space if required.
#if ENABLE_SMP
	if (private_space) {
		mm_private_space_prepare(&thread->space);
	} else {
		thread->space.space.opaque = NULL;
	}
#endif

	// Initialize log message queue.
	mm_queue_init(&thread->log_queue);

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

#if ENABLE_SMP
	if (thread->space.space.opaque != NULL)
		mm_private_space_cleanup(&thread->space);
#endif

#if ENABLE_TRACE
	mm_trace_context_cleanup(&thread->trace);
#endif

	mm_global_free(thread);

	LEAVE();
}

/**********************************************************************
 * Thread information.
 **********************************************************************/

const char *
mm_thread_getname(const struct mm_thread *thread)
{
	return thread->name;
}

#if ENABLE_SMP
struct mm_private_space * __attribute__((nonnull(1)))
mm_thread_getspace(struct mm_thread *thread)
{
	return &thread->space;
}
#endif

struct mm_domain *
mm_thread_getdomain(const struct mm_thread *thread)
{
	return thread->domain;
}

mm_core_t 
mm_thread_getdomainindex(const struct mm_thread *thread)
{
	return thread->domain_index;
}

struct mm_queue *
mm_thread_getlog(struct mm_thread *thread)
{
	return &thread->log_queue;
}

#if ENABLE_TRACE
struct mm_trace_context *
mm_thread_gettracecontext(struct mm_thread *thread)
{
	return &thread->trace;
}
#endif

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
mm_thread_domain_barrier(void)
{
	ENTER();

	struct mm_thread *thread = mm_thread_self();
	if (thread->domain != NULL) {
		mm_log_relay();

		mm_barrier_wait(&thread->domain->barrier,
				&thread->domain_barrier);
	}

	LEAVE();
}
