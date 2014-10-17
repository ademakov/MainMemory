
#include "ring.c"
#include "backoff.c"

#include <sched.h>

__thread struct mm_core *mm_core = NULL;

void *
mm_global_alloc(size_t size)
{
	return malloc(size);
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
