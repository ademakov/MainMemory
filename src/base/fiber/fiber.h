/*
 * base/fiber/fiber.h - MainMemory user-space threads.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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

#ifndef BASE_FIBER_FIBER_H
#define BASE_FIBER_FIBER_H

#include "common.h"
#include "base/context.h"
#include "base/cstack.h"
#include "base/list.h"
#if ENABLE_TRACE
#include "base/report.h"
#endif

#define ENABLE_FIBER_LOCATION	0

/* Maximal fiber name length (including terminating zero). */
#define MM_FIBER_NAME_SIZE	40

/* Fiber priority type. */
typedef int8_t			mm_priority_t;

/* The lowest and highest allowed priority values. */
#define MM_PRIO_LOWERMOST	((mm_priority_t) 31)
#define MM_PRIO_UPPERMOST	((mm_priority_t) 0)

/* Given a priority value get the value n levels below that. */
#define MM_PRIO_LOWER(p, n)	min(MM_PRIO_LOWERMOST, (mm_priority_t) ((p) + (n)))

/* Given a priority value get the value n levels above that. */
#define MM_PRIO_UPPER(p, n)	max(MM_PRIO_UPPERMOST, (mm_priority_t) ((p) - (n)))

/* Basic fiber priorities. */
#define MM_PRIO_BOOT		MM_PRIO_LOWERMOST
#define MM_PRIO_MASTER		MM_PRIO_UPPER(MM_PRIO_BOOT, 1)
#define MM_PRIO_WORKER		MM_PRIO_UPPER(MM_PRIO_MASTER, 1)

/* Fiber state values. */
typedef enum {
	MM_FIBER_INVALID = -1,
	MM_FIBER_BLOCKED,
	MM_FIBER_PENDING,
	MM_FIBER_RUNNING,
} mm_fiber_state_t;

/*
 * Fiber flags.
 */
/* Flags for cancellation handling. */
#define MM_FIBER_CANCEL_ENABLE		0x00
#define MM_FIBER_CANCEL_DISABLE		0x01
#define MM_FIBER_CANCEL_DEFERRED	0x00
#define MM_FIBER_CANCEL_REQUIRED	0x02
#define MM_FIBER_CANCEL_OCCURRED	0x04
/* Flags for fibers blocked for various reasons. */
#define MM_FIBER_WAITING		0x08
#define MM_FIBER_COMBINING		0x10

/* Fiber flags type. */
typedef uint8_t mm_fiber_flags_t;

/* Fiber creation attributes. */
struct mm_fiber_attr
{
	/* Fiber flags. */
	mm_fiber_flags_t flags;

	/* Fiber scheduling priority. */
	mm_priority_t priority;

	/* The fiber stack size. */
	uint32_t stack_size;

	/* The fiber name. */
	char name[MM_FIBER_NAME_SIZE];
};

/* A fiber cleanup handler record. */
struct mm_fiber_cleanup_rec
{
	struct mm_fiber_cleanup_rec *next;
	void (*const routine)(uintptr_t arg);
	uintptr_t routine_arg;
};

/* Register a cleanup handler. */
#define mm_fiber_cleanup_push(rtn, arg)					\
	do {								\
		struct mm_fiber *__fiber = mm_fiber_selfptr();		\
		struct mm_fiber_cleanup_rec __cleanup = {		\
				.next = __fiber->cleanup,		\
				.routine = (void (*)(uintptr_t)) (rtn),	\
				.routine_arg = (uintptr_t) (arg),	\
			};						\
		__fiber->cleanup = &__cleanup;				\
		do {

/* Unregister a cleanup handler optionally executing it. */
#define mm_fiber_cleanup_pop(execute)					\
		} while (0);						\
		__fiber->cleanup = __cleanup.next;			\
		if (execute) {						\
			__cleanup.routine(__cleanup.routine_arg);	\
		}							\
	} while (0)


/* A user-space (green) thread. */
struct mm_fiber
{
	/* A link in a run/dead queue. */
	struct mm_link queue;
	struct mm_link wait_queue;

	/* Blocked or pending fiber stack context. */
	mm_cstack_t stack_ctx;

	/* The fiber status. */
	mm_fiber_state_t state;
	mm_fiber_flags_t flags;

	/* Fiber scheduling priority. */
	mm_priority_t priority;
	mm_priority_t original_priority;

	/* The fiber strand. */
	struct mm_strand *strand;

	/* The list of fiber cleanup records. */
	struct mm_fiber_cleanup_rec *cleanup;

	/* The fiber execution result. */
	mm_value_t result;

	/* The fiber start routine and its argument. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* The fiber stack. */
	uint32_t stack_size;
	void *stack_base;

	/* The fiber name. */
	char name[MM_FIBER_NAME_SIZE];

#if ENABLE_FIBER_LOCATION
	const char *location;
	const char *function;
#endif

#if ENABLE_TRACE
	/* Thread trace context. */
	struct mm_trace_context trace;
#endif
};

/**********************************************************************
 * Fiber creation and destruction.
 **********************************************************************/

void NONNULL(1)
mm_fiber_attr_init(struct mm_fiber_attr *attr);
void NONNULL(1)
mm_fiber_attr_setflags(struct mm_fiber_attr *attr, mm_fiber_flags_t flags);
void NONNULL(1)
mm_fiber_attr_setpriority(struct mm_fiber_attr *attr, mm_priority_t priority);
void NONNULL(1)
mm_fiber_attr_setstacksize(struct mm_fiber_attr *attr, uint32_t stack_size);
void NONNULL(1)
mm_fiber_attr_setname(struct mm_fiber_attr *attr, const char *name);

struct mm_fiber * NONNULL(2)
mm_fiber_create(const struct mm_fiber_attr *attr, mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_fiber_destroy(struct mm_fiber *fiber);

/**********************************************************************
 * Fiber bootstrap.
 **********************************************************************/

struct mm_fiber *
mm_fiber_create_boot(struct mm_strand *strand);

/**********************************************************************
 * Fiber utilities.
 **********************************************************************/

static inline struct mm_fiber *
mm_fiber_selfptr(void)
{
	return mm_context_selfptr()->fiber;
}

static inline const char * NONNULL(1)
mm_fiber_getname(const struct mm_fiber *fiber)
{
	return fiber->name;
}

void NONNULL(1)
mm_fiber_print_status(const struct mm_fiber *fiber);

/**********************************************************************
 * Fiber execution.
 **********************************************************************/

void NONNULL(1)
mm_fiber_run(struct mm_fiber *fiber);

void NONNULL(1)
mm_fiber_hoist(struct mm_fiber *fiber, mm_priority_t priority);

static inline void NONNULL(1)
mm_fiber_restore_priority(struct mm_fiber *fiber)
{
	fiber->priority = fiber->original_priority;
}

#if ENABLE_FIBER_LOCATION

# define mm_fiber_yield(c) mm_fiber_yield_at(c, __LOCATION__, __FUNCTION__)
# define mm_fiber_block(c) mm_fiber_block_at(c, __LOCATION__, __FUNCTION__)
# define mm_fiber_pause(c, t) mm_fiber_pause_at(c, t, __LOCATION__, __FUNCTION__)

void NONNULL(1, 2, 3)
mm_fiber_yield_at(struct mm_context *context, const char *location, const char *function);

void NONNULL(1, 2, 3)
mm_fiber_block_at(struct mm_context *context, const char *location, const char *function);

void NONNULL(1, 3, 4)
mm_fiber_pause_at(struct mm_context *context, mm_timeout_t timeout, const char *location, const char *function);

#else /* !ENABLE_FIBER_LOCATION */

void NONNULL(1)
mm_fiber_yield(struct mm_context *context);

void NONNULL(1)
mm_fiber_block(struct mm_context *context);

void NONNULL(1)
mm_fiber_pause(struct mm_context *context, mm_timeout_t timeout);

#endif /* !ENABLE_FIBER_LOCATION */

void NORETURN
mm_fiber_exit(mm_value_t result);

/**********************************************************************
 * Fiber cancellation.
 **********************************************************************/

#define MM_FIBER_CANCEL_TEST(flags)		\
	((flags & (MM_FIBER_CANCEL_DISABLE	\
		   | MM_FIBER_CANCEL_REQUIRED	\
		   | MM_FIBER_CANCEL_OCCURRED))	\
	 == MM_FIBER_CANCEL_REQUIRED)

static inline void
mm_fiber_testcancel(void)
{
	struct mm_fiber *fiber = mm_fiber_selfptr();
	if (unlikely(MM_FIBER_CANCEL_TEST(fiber->flags))) {
		fiber->flags |= MM_FIBER_CANCEL_OCCURRED;
		mm_fiber_exit(MM_RESULT_CANCELED);
	}
}

void
mm_fiber_setcancelstate(int new_value, int *old_value_ptr);

void NONNULL(1)
mm_fiber_cancel(struct mm_fiber *fiber);

#endif /* BASE_FIBER_FIBER_H */
