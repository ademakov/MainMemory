/*
 * base/fiber/fiber.c - MainMemory user-space threads.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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

#include "base/fiber/fiber.h"

#include "base/async.h"
#include "base/bitops.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/fiber/strand.h"
#include "base/memory/memory.h"
#include "base/thread/thread.h"

/* Regular fiber stack size. */
#define MM_FIBER_STACK_DEFAULT		(7 * MM_PAGE_SIZE)

/* Minimum fiber stack size. */
#define MM_FIBER_STACK_MIN		(1 * MM_PAGE_SIZE)

/**********************************************************************
 * Fiber creation attributes.
 **********************************************************************/

void NONNULL(1)
mm_fiber_attr_init(struct mm_fiber_attr *attr)
{
	memset(attr, 0, sizeof(*attr));
	attr->priority = MM_PRIO_WORKER;
}

void NONNULL(1)
mm_fiber_attr_setflags(struct mm_fiber_attr *attr, mm_fiber_flags_t flags)
{
	attr->flags = flags;
}

void NONNULL(1)
mm_fiber_attr_setpriority(struct mm_fiber_attr *attr, mm_priority_t priority)
{
	ASSERT(priority <= MM_PRIO_LOWERMOST);
	ASSERT(priority >= MM_PRIO_UPPERMOST);
	attr->priority = priority;
}

void NONNULL(1)
mm_fiber_attr_setstacksize(struct mm_fiber_attr *attr, uint32_t stack_size)
{
	attr->stack_size = stack_size;
}

void NONNULL(1)
mm_fiber_attr_setname(struct mm_fiber_attr *attr, const char *name)
{
	ENTER();

	size_t len = 0;
	if (likely(name != NULL)) {
		len = strlen(name);
		if (len >= sizeof attr->name)
			len = sizeof attr->name - 1;

		memcpy(attr->name, name, len);
	}
	attr->name[len] = 0;

	LEAVE();
}

/**********************************************************************
 * Fiber creation and destruction.
 **********************************************************************/

/* Entry point for a fiber. */
static void
mm_fiber_entry(void)
{
	struct mm_fiber *fiber = mm_fiber_selfptr();

#if ENABLE_TRACE
	mm_trace_context_prepare(&fiber->trace, "[%s %s]",
				 mm_thread_getname(mm_thread_selfptr()),
				 mm_fiber_getname(fiber));
#endif

	TRACE("enter fiber %s", mm_fiber_getname(fiber));

	// Execute the fiber routine on an empty stack.
	mm_value_t result = fiber->start(fiber->start_arg);

	// Finish the fiber making sure there is no return from this point
	// as there is no valid stack frame above it.
	mm_fiber_exit(result);
}

/* Execute fiber cleanup routines. */
static void
mm_fiber_cleanup(struct mm_fiber *fiber)
{
	ENTER();

	while (fiber->cleanup != NULL) {
		void (*routine)(uintptr_t) = fiber->cleanup->routine;
		uintptr_t routine_arg = fiber->cleanup->routine_arg;
		fiber->cleanup = fiber->cleanup->next;
		routine(routine_arg);
	}

	LEAVE();
}

/* Create a new fiber. */
static struct mm_fiber *
mm_fiber_new(struct mm_strand *strand)
{
	// Allocate a fiber.
	struct mm_fiber *fiber = mm_common_alloc(sizeof(struct mm_fiber));

	// Store the strand that owns the fiber.
	fiber->strand = strand;

	// Initialize the fiber stack info.
	fiber->stack_size = 0;
	fiber->stack_base = NULL;

	// Initialize the cleanup handler list.
	fiber->cleanup = NULL;

	return fiber;
}

static uint32_t
mm_fiber_attr_getstacksize(const struct mm_fiber_attr *attr)
{
	/* Handle default stack size cases. */
	if (attr == NULL || attr->stack_size == 0)
		return MM_FIBER_STACK_DEFAULT;

	/* Sanitize specified stack size value. */
	if (attr->stack_size < MM_FIBER_STACK_MIN)
		return MM_FIBER_STACK_MIN;
	return mm_round_up(attr->stack_size, MM_PAGE_SIZE);
}

/* Initialize a fiber. */
static void
mm_fiber_set_attr(struct mm_fiber *fiber, const struct mm_fiber_attr *attr)
{
	fiber->result = MM_RESULT_NOTREADY;

	if (unlikely(attr == NULL)) {
		fiber->flags = 0;
		fiber->original_priority = MM_PRIO_WORKER;
		strcpy(fiber->name, "unnamed");
	} else {
		fiber->flags = attr->flags;
		fiber->original_priority = attr->priority;
		if (attr->name[0])
			memcpy(fiber->name, attr->name, MM_FIBER_NAME_SIZE);
		else
			strcpy(fiber->name, "unnamed");
	}

	fiber->priority = fiber->original_priority;

#if ENABLE_FIBER_LOCATION
	fiber->location = "<not set yet>";
	fiber->function = "<not set yet>";
#endif
}

/* Create a new fiber. */
struct mm_fiber * NONNULL(2)
mm_fiber_create(const struct mm_fiber_attr *attr, mm_routine_t start, mm_value_t start_arg)
{
	ENTER();
	struct mm_fiber *fiber = NULL;

	// Determine the required stack size.
	uint32_t stack_size = mm_fiber_attr_getstacksize(attr);

	// Try to reuse a dead fiber.
	struct mm_strand *strand = mm_context_strand();
	if (!mm_list_empty(&strand->dead)) {
		// Get the last dead fiber.
		struct mm_link *link = mm_list_head(&strand->dead);
		struct mm_fiber *dead = containerof(link, struct mm_fiber, queue);

		// Check it against the required stack size.
		if (dead->stack_size == stack_size) {
			// The dead fiber is just good.
			mm_list_delete(link);
			fiber = dead;
		} else if (dead->stack_size != MM_FIBER_STACK_DEFAULT) {
			// The dead fiber has an unusual stack, free it.
			mm_cstack_destroy(dead->stack_base, dead->stack_size);
			dead->stack_size = 0;
			dead->stack_base = NULL;
			// Now use that fiber.
			mm_list_delete(link);
			fiber = dead;
		} else {
			// A fiber with unusual stack size is requested, leave
			// the dead fiber alone, it is likely to be reused the
			// next time.
		}
	}

	// Allocate a new fiber if needed.
	if (fiber == NULL)
		fiber = mm_fiber_new(strand);

	// Initialize the fiber info.
	mm_fiber_set_attr(fiber, attr);
	fiber->start = start;
	fiber->start_arg = start_arg;

	// Add it to the blocked fiber list.
	fiber->state = MM_FIBER_BLOCKED;
	mm_list_append(&strand->block, &fiber->queue);

	// Determine combined stack and guard page size.
	uint32_t total_size = stack_size + MM_PAGE_SIZE;

	// Allocate a new stack if needed.
	if (fiber->stack_base == NULL)
		fiber->stack_base = mm_cstack_create(total_size, MM_PAGE_SIZE);
	fiber->stack_size = stack_size;

	// Setup the fiber entry point on the stack and queue
	// it for execution.
	mm_cstack_prepare(&fiber->stack_ctx, mm_fiber_entry,
			  fiber->stack_base, total_size);
	mm_fiber_run(fiber);

	LEAVE();
	return fiber;
}

/* Destroy a fiber. The fiber should not run at the moment and it
 * should be absolutely guaranteed from being used afterwards. */
void
mm_fiber_destroy(struct mm_fiber *fiber)
{
	ENTER();
	ASSERT(fiber->state == MM_FIBER_INVALID || fiber->state == MM_FIBER_BLOCKED);
	ASSERT((fiber->flags & MM_FIBER_WAITING) == 0 || fiber->strand->stop);

	// Free the stack.
	if (fiber->stack_base != NULL)
		mm_cstack_destroy(fiber->stack_base, fiber->stack_size);

	// At last free the fiber struct.
	mm_common_free(fiber);

	LEAVE();
}

/**********************************************************************
 * Fiber bootstrap.
 **********************************************************************/

/* Create a dummy fiber without its own stack. It is used to switch from a
   stack that belongs to a kernel thread to a properly created fiber (that
   is with the mm_fiber_create() function). It might be possible to use a
   bare stack context for the same purpose. However at the expense of few
   extra bytes the dummy fiber approach saves from the need to handle any
   special cases on the very hot code path -- during fiber switch. */
struct mm_fiber *
mm_fiber_create_boot(struct mm_strand *strand)
{
	ENTER();

	// Allocate a fiber.
	struct mm_fiber *fiber = mm_fiber_new(strand);

	// Prepare the bootstrap fiber attributes.
	struct mm_fiber_attr attr;
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setflags(&attr, MM_FIBER_CANCEL_DISABLE);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_BOOT);
	mm_fiber_attr_setname(&attr, "boot");

	// Initialize the bootstrap fiber.
	mm_fiber_set_attr(fiber, &attr);
	fiber->start = NULL;
	fiber->start_arg = 0;
	fiber->state = MM_FIBER_INVALID;

	LEAVE();
	return fiber;
}

/**********************************************************************
 * Fiber utilities.
 **********************************************************************/

void NONNULL(1)
mm_fiber_print_status(const struct mm_fiber *fiber)
{
	static char *state[] = { "blocked", "pending", "running", "invalid" };
	mm_log_fmt("  %s %s", fiber->name, state[fiber->state]);
#if ENABLE_FIBER_LOCATION
	if (fiber->state == MM_FIBER_BLOCKED || fiber->state == MM_FIBER_PENDING)
		mm_log_fmt(" at %s(%s)", fiber->function, fiber->location);
#endif
	mm_log_fmt("\n");
}

/**********************************************************************
 * Fiber execution.
 **********************************************************************/

/* Switch to the next fiber in the run queue. */
static void
mm_fiber_switch(struct mm_context *const context, const mm_fiber_state_t state)
{
	// Bail out if the strand is not in the normal running state.
	struct mm_strand *const strand = context->strand;
	if (unlikely(strand->state != MM_STRAND_RUNNING))
		return;

	// Get the currently running fiber.
	struct mm_fiber *const old_fiber = context->fiber;
	ASSERT(old_fiber->state == MM_FIBER_RUNNING);

	// Bring it to the requested state.
	old_fiber->state = state;
	if (unlikely(state == MM_FIBER_INVALID)) {
		// Add it to the dead fiber list.
		mm_list_append(&strand->dead, &old_fiber->queue);
	} else {
		// Reset the priority that could have been temporary raised.
		old_fiber->priority = old_fiber->original_priority;
		if (state == MM_FIBER_BLOCKED) {
			// Add it to the blocked fiber list.
			mm_list_append(&strand->block, &old_fiber->queue);
		} else {
			// Add it to the run queue.
			mm_runq_put(&strand->runq, old_fiber);
		}
	}

	// Handle any pending async calls. Sometimes this might touch the
	// currently running fiber. For instance, it might be put to the
	// run queue after just being blocked. So at this point the fiber
	// must already be in completely consistent state. That is no
	// manipulation with old_fiber is allowed below this point.
	mm_async_handle_calls(context);

	// Get the next fiber from the run queue.  As long as this function
	// is called there is at least a boot fiber in the run queue.  So
	// there should never be a NULL value returned.
	struct mm_fiber *const new_fiber = mm_runq_get(&strand->runq);
	new_fiber->state = MM_FIBER_RUNNING;
	context->fiber = new_fiber;

	// Count the context switch.
	strand->cswitch_count++;

	// Switch to the new fiber relinquishing CPU control for a while.
	mm_cstack_switch(&old_fiber->stack_ctx, &new_fiber->stack_ctx);
}

/* Queue a fiber for execution. */
void NONNULL(1)
mm_fiber_run(struct mm_fiber *fiber)
{
	ENTER();
	TRACE("queue fiber: [%s], state: %d, priority: %d",
	      mm_fiber_getname(fiber), fiber->state, fiber->priority);
	ASSERT(fiber->strand == mm_context_strand());
	ASSERT(fiber->priority < MM_PRIO_BOOT);

	if (fiber->state == MM_FIBER_BLOCKED) {
		// Remove it from the blocked fiber list.
		mm_list_delete(&fiber->queue);
		// Add it to the run queue.
		fiber->state = MM_FIBER_PENDING;
		mm_runq_put(&fiber->strand->runq, fiber);
	}

	LEAVE();
}

/* Queue a fiber for execution with temporary raised priority. */
void NONNULL(1)
mm_fiber_hoist(struct mm_fiber *fiber, mm_priority_t priority)
{
	ENTER();
	TRACE("hoist fiber: [%s], state: %d, priority: %d, %d",
	      mm_fiber_getname(fiber), fiber->state, fiber->priority, priority);
	ASSERT(fiber->strand == mm_context_strand());
	ASSERT(fiber->priority < MM_PRIO_BOOT);

	if (fiber->state == MM_FIBER_BLOCKED
	    || (fiber->state == MM_FIBER_PENDING && fiber->priority > priority)) {

		if (fiber->state == MM_FIBER_BLOCKED) {
			// Remove it from the blocked fiber list.
			mm_list_delete(&fiber->queue);
			fiber->state = MM_FIBER_PENDING;
		} else {
			// Remove it from the run queue.
			mm_runq_delete(&fiber->strand->runq, fiber);
		}

		if (fiber->priority > priority)
			fiber->priority = priority;

		// Add it to the run queue with raised priority.
		mm_runq_put(&fiber->strand->runq, fiber);
	}

	LEAVE();
}

#if ENABLE_FIBER_LOCATION

void NONNULL(1, 2, 3)
mm_fiber_yield_at(struct mm_context *context, const char *location, const char *function)
{
	ENTER();

	struct mm_fiber *const fiber = context->fiber;
	fiber->location = location;
	fiber->function = function;

	mm_fiber_switch(context, MM_FIBER_PENDING);

	LEAVE();
}

void NONNULL(1, 2, 3)
mm_fiber_block_at(struct mm_context *context, const char *location, const char *function)
{
	ENTER();

	struct mm_fiber *const fiber = context->fiber;
	fiber->location = location;
	fiber->function = function;

	mm_fiber_switch(context, MM_FIBER_BLOCKED);

	LEAVE();
}

#else

void NONNULL(1)
mm_fiber_yield(struct mm_context *context)
{
	ENTER();

	mm_fiber_switch(context, MM_FIBER_PENDING);

	LEAVE();
}

void NONNULL(1)
mm_fiber_block(struct mm_context *context)
{
	ENTER();

	mm_fiber_switch(context, MM_FIBER_BLOCKED);

	LEAVE();
}

#endif

static void
mm_fiber_pause_cleanup(uintptr_t arg)
{
	struct mm_event_timer *timer = (struct mm_event_timer *) arg;
	mm_event_disarm_timer(mm_context_selfptr(), timer);
}

#if ENABLE_FIBER_LOCATION
void NONNULL(1, 3, 4)
mm_fiber_pause_at(struct mm_context *context, mm_timeout_t timeout, const char *location, const char *function)
#else
void NONNULL(1)
mm_fiber_pause(struct mm_context *context, mm_timeout_t timeout)
#endif
{
	ENTER();

	struct mm_fiber *const fiber = context->fiber;

	struct mm_event_timer timer;
	mm_event_prepare_fiber_timer(&timer, fiber);
	mm_event_arm_timer(context, &timer, timeout);

#if ENABLE_FIBER_LOCATION
	fiber->location = location;
	fiber->function = function;
#endif

	mm_fiber_cleanup_push(mm_fiber_pause_cleanup, &timer);
	mm_fiber_switch(context, MM_FIBER_BLOCKED);
	mm_fiber_cleanup_pop(false);

	if (mm_event_timer_armed(&timer))
		mm_event_disarm_timer(context, &timer);

	LEAVE();
}

/* Finish the current fiber. */
void
mm_fiber_exit(mm_value_t result)
{
	struct mm_fiber *fiber = mm_fiber_selfptr();
	TRACE("exiting fiber '%s' with status %lu", fiber->name, (unsigned long) result);

	// Set the fiber execution result.
	fiber->result = result;

	// Call the cleanup handlers.
	mm_fiber_cleanup(fiber);

	// At this point the fiber must not be in any queue.
	ASSERT((fiber->flags & MM_FIBER_WAITING) == 0);

	// Give the control to still running fibers.
	mm_fiber_switch(mm_context_selfptr(), MM_FIBER_INVALID);

	// Must never get here after the switch above.
	ABORT();
}

/**********************************************************************
 * Fiber cancellation.
 **********************************************************************/

void
mm_fiber_setcancelstate(int new_value, int *old_value_ptr)
{
	ENTER();
	ASSERT(new_value == MM_FIBER_CANCEL_ENABLE || new_value == MM_FIBER_CANCEL_DISABLE);

	struct mm_fiber *fiber = mm_fiber_selfptr();
	int old_value = (fiber->flags & MM_FIBER_CANCEL_DISABLE);
	if (likely(old_value != new_value)) {
		if (new_value) {
			fiber->flags |= MM_FIBER_CANCEL_DISABLE;
		} else {
			fiber->flags &= ~MM_FIBER_CANCEL_DISABLE;
		}
	}

	if (old_value_ptr != NULL)
		*old_value_ptr = old_value;

	LEAVE();
}

void NONNULL(1)
mm_fiber_cancel(struct mm_fiber *fiber)
{
	ENTER();

	fiber->flags |= MM_FIBER_CANCEL_REQUIRED;
	if (unlikely(fiber->state == MM_FIBER_RUNNING)) {
		ASSERT(fiber == mm_fiber_selfptr());
	} else {
		mm_fiber_run(fiber);
	}

	LEAVE();
}
