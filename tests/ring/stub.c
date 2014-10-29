
#include "base/ring.c"
#include "base/backoff.c"
#include "base/barrier.c"

#include <sched.h>

__thread struct mm_core *mm_core = NULL;

void *
mm_global_aligned_alloc(size_t align, size_t size)
{
	void *ptr;
	posix_memalign(&ptr, align, size);
	return ptr;
}

void
mm_task_yield(void)
{
}

void
mm_thread_yield(void)
{
	sched_yield();
}

void
mm_abort_with_message(const char *restrict location,
		      const char *restrict function,
		      const char *restrict msg, ...)
{
	(void) location;
	(void) function;
	(void) msg;
	abort();
}
