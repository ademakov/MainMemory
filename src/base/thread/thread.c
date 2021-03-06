/*
 * base/thread/thread.c - MainMemory threads.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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

#include "base/logger.h"
#include "base/memory/alloc.h"
#include "base/thread/domain.h"
#include "base/thread/ident.h"

#include <sched.h>

#if HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif

#if HAVE_MACH_THREAD_POLICY_H
# include <mach/mach_init.h>
# include <mach/thread_act.h>
# include <mach/thread_policy.h>
#endif

static struct mm_thread mm_thread_main = {
	.domain_index = MM_THREAD_NONE,
	.thread_ident = MM_THREAD_NONE,

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

void
mm_thread_init()
{
	mm_thread_main.system_thread = pthread_self();
}

/**********************************************************************
 * Thread creation attributes.
 **********************************************************************/

void NONNULL(1)
mm_thread_attr_prepare(struct mm_thread_attr *attr)
{
	memset(attr, 0, sizeof *attr);
}

void NONNULL(1, 2)
mm_thread_attr_setdomain(struct mm_thread_attr *attr, struct mm_domain *domain, mm_thread_t index)
{
	attr->domain = domain;
	attr->domain_index = index;
}

void NONNULL(1)
mm_thread_attr_setcputag(struct mm_thread_attr *attr, uint32_t cpu_tag)
{
	attr->cpu_tag = cpu_tag;
}

void NONNULL(1)
mm_thread_attr_setstacksize(struct mm_thread_attr *attr, uint32_t size)
{
	attr->stack_size = size;
}

void NONNULL(1)
mm_thread_attr_setguardsize(struct mm_thread_attr *attr, uint32_t size)
{
	attr->guard_size = size;
}

void NONNULL(1)
mm_thread_attr_setstack(struct mm_thread_attr *attr, void *base, uint32_t size)
{
	attr->stack_base = base;
	attr->stack_size = size;
}

void NONNULL(1)
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
	if (attr == NULL) {
		// no-op
	} else if (attr->stack_base != NULL) {
		if (attr->stack_size == 0)
			mm_fatal(0, "invalid thread attributes");
		int rc = pthread_attr_setstack(pthr_attr,
					       attr->stack_base,
					       attr->stack_size);
		if (rc)
			mm_fatal(rc, "pthread_attr_setstack");
	} else {
		if (attr->stack_size != 0) {
			int rc = pthread_attr_setstacksize(pthr_attr,
							   attr->stack_size);
			if (rc)
				mm_fatal(rc, "pthread_attr_setstacksize");
		}
		if (attr->guard_size != 0) {
			int rc = pthread_attr_setguardsize(pthr_attr,
							   attr->guard_size);
			if (rc)
				mm_fatal(rc, "pthread_attr_setguardsize");
		}
	}
}

#if ENABLE_SMP && HAVE_PTHREAD_SETAFFINITY_NP
static void
mm_thread_setaffinity(uint32_t cpu_tag)
{
#if HAVE_SYS_CPUSET_H
	cpuset_t cpu_set; // FreeBSD CPU set declaration.
#else
	cpu_set_t cpu_set; // Linux CPU set declaration.
#endif
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
	kern_return_t kr;
	thread_t tid = mach_thread_self();

	thread_extended_policy_data_t epolicy = { .timeshare = FALSE };
	kr = thread_policy_set(tid, THREAD_EXTENDED_POLICY,
			       (thread_policy_t) &epolicy,
			       THREAD_EXTENDED_POLICY_COUNT);
	if (kr != KERN_SUCCESS)
		mm_error(0, "failed to set thread extended policy");

	thread_affinity_policy_data_t apolicy = { .affinity_tag = cpu_tag + 1 };
	kr = thread_policy_set(tid, THREAD_AFFINITY_POLICY,
			       (thread_policy_t) &apolicy,
			       THREAD_AFFINITY_POLICY_COUNT);
	if (kr != KERN_SUCCESS)
		mm_error(0, "failed to set thread affinity policy");
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
	if (thread->cpu_tag != MM_THREAD_CPU_ANY)
		mm_thread_setaffinity(thread->cpu_tag);

#if HAVE_PTHREAD_SETNAME_NP
	// Let the system know the thread name.
# ifdef __APPLE__
	pthread_setname_np(thread->name);
# else
	pthread_setname_np(pthread_self(), thread->name);
# endif
#endif

	// Run the required routine.
	thread->start(thread->start_arg);

	LEAVE();
	mm_log_relay();

	return NULL;
}

struct mm_thread * NONNULL(2)
mm_thread_create(struct mm_thread_attr *attr, mm_routine_t start, mm_value_t start_arg)
{
	ENTER();
	int rc;

	// Create a thread object.
	struct mm_thread *thread = mm_memory_xalloc(sizeof(struct mm_thread));
	thread->start = start;
	thread->start_arg = start_arg;

	// Set basic thread attributes.
	if (attr == NULL) {
		thread->domain = NULL;
		thread->domain_index = 0;
		thread->cpu_tag = 0;
	} else {
		thread->domain = attr->domain;
		thread->domain_index = attr->domain_index;
		thread->cpu_tag = attr->cpu_tag;
	}
	if (thread->domain == NULL) {
		struct mm_thread_ident_pair id_pair = mm_thread_ident_alloc(0, 1);
		VERIFY(id_pair.domain == MM_THREAD_NONE && id_pair.thread != MM_THREAD_NONE);
		thread->thread_ident = id_pair.thread;
	} else {
		thread->thread_ident = thread->domain->thread_ident_base + thread->domain_index;
	}

	// Set the thread name.
	if (attr != NULL && attr->name[0])
		memcpy(thread->name, attr->name, MM_THREAD_NAME_SIZE);
	else
		strcpy(thread->name, "unnamed");

	// Initialize log message queue.
	mm_queue_prepare(&thread->log_queue);

	// Set thread system attributes.
	pthread_attr_t pthr_attr;
	pthread_attr_init(&pthr_attr);
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
void NONNULL(1)
mm_thread_destroy(struct mm_thread *thread)
{
	ENTER();

#if ENABLE_TRACE
	mm_trace_context_cleanup(&thread->trace);
#endif

	mm_memory_free(thread);

	LEAVE();
}

/**********************************************************************
 * Thread control routines.
 **********************************************************************/

/* Cancel a running thread. */
void NONNULL(1)
mm_thread_cancel(struct mm_thread *thread)
{
	ENTER();

	int rc = pthread_cancel(thread->system_thread);
	if (rc)
		mm_error(rc, "pthread_cancel");

	LEAVE();
}

/* Wait for a thread exit. */
void NONNULL(1)
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

#if HAVE_PTHREAD_YIELD
	pthread_yield();
#elif HAVE_PTHREAD_YIELD_NP
	pthread_yield_np();
#else
	sched_yield();
#endif

	LEAVE();
}
