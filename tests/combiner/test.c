
#include "common.h"
#include <stdlib.h>

#define USE_LOCK	0
#define REPEAT_NUM	6000
#define MM_CORE_NUM	4

#define CORE_H

struct mm_core
{
	mm_core_t id;
	struct mm_task *task;
	void *arena;

	struct mm_thread *thread;
};

__thread struct mm_core *mm_core;

struct mm_chunk;

void mm_core_reclaim_chunk(struct mm_chunk *chunk);

mm_core_t
mm_core_getnum(void)
{
	return MM_CORE_NUM;
}

mm_core_t
mm_core_selfid(void)
{
	if (mm_core == NULL)
		return MM_CORE_NONE;
	return mm_core->id;
}

void
mm_core_post(mm_core_t core, mm_routine_t routine, mm_value_t routine_arg)
{
	routine(routine_arg);
}

void
mm_core_reclaim_chunk(struct mm_chunk *chunk)
{
}

#define TASK_H

#define MM_TASK_CANCEL_DISABLE		0x01
#define MM_TASK_COMBINING		0x80

/* Task flags type. */
typedef uint8_t			mm_task_flags_t;

struct mm_task
{
	mm_task_flags_t flags;
};

struct mm_task *
mm_task_self(void)
{
	return mm_core->task;
}

void
mm_task_yield(void)
{
}

void
mm_task_block(void)
{
}

void
mm_task_run(struct mm_task *task)
{
	(void) task;
}

void
mm_task_setcancelstate(int new, int *old)
{
	(void) new; (void) old;
}

#define MALLOC_280_H
#define MSPACES		1
#define USE_DL_PREFIX	1
#include "dlmalloc/malloc.c"
#undef ABORT
#undef DEBUG

#include "alloc.c"
#include "backoff.c"
#include "cdata.c"
#include "chunk.c"
#include "hook.c"
#include "exit.c"
#include "log.c"
#include "lock.c"
#include "ring.c"
#include "thread.c"
#include "trace.c"
#include "util.c"

#include "combiner.c"

struct mm_combiner *g_combiner;
mm_task_lock_t g_lock = MM_TASK_LOCK_INIT;

struct mm_task g_tasks[MM_CORE_NUM];
struct mm_core g_cores[MM_CORE_NUM];

mm_atomic_uint32_t g_ncalls;

void
routine(uintptr_t data)
{
	mm_atomic_uint32_inc(&g_ncalls);
}

void
locked_routine(uintptr_t data)
{
	mm_task_lock(&g_lock);
	routine(data);
	mm_task_unlock(&g_lock);
}

mm_value_t
testing_thread(mm_value_t arg)
{
	mm_core = &g_cores[arg];
	mm_log_relay();

	for (int i = 0; i < REPEAT_NUM; i++)
#if USE_LOCK
		locked_routine(i);
#else
		mm_combiner_execute(g_combiner, i, true);
#endif

	return 0;
}

int
main()
{
	mm_alloc_init();
	mm_cdata_init();
	mm_thread_init();

	g_combiner = mm_combiner_create("combiner", routine, 32, 4);

	for (mm_core_t i = 0; i < MM_CORE_NUM; i++) {
		g_tasks[i].flags = 0;
		g_cores[i].id = i;
		g_cores[i].task = &g_tasks[i];
		g_cores[i].arena = create_mspace(0, 0);
		g_cores[i].thread = mm_thread_create(NULL, testing_thread, i);
	}

	mm_log_flush();

	for (mm_core_t i = 0; i < MM_CORE_NUM; i++) {
		mm_thread_join(g_cores[i].thread);
	}

	mm_combiner_destroy(g_combiner);

	mm_log_flush();

	printf("calls: %d\n", g_ncalls);

	return EXIT_SUCCESS;
}
