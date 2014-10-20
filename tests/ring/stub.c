
#include "ring.c"
#include "backoff.c"

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
