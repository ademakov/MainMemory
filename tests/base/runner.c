#include "params.h"
#include "runner.h"

#include "common.h"
#include "base/atomic.h"
#include "base/lock.h"
#include "base/thread/barrier.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define _1M_	1000000

static int g_threads;
static struct mm_thread_barrier g_barrier;

struct thread
{
	pthread_t thread;

	struct mm_thread_barrier_local barrier;

	void (*start)(void *);
	void *start_arg;

	uint64_t time;

#if ENABLE_LOCK_STATS
	struct mm_lock_stat lock_stat;
#endif

	char name[128];
};

#if ENABLE_LOCK_STATS
__thread struct thread *g_this_thread;

struct mm_lock_stat *
mm_lock_getstat(struct mm_lock_stat_info *info __attribute((unused)))
{
	return &g_this_thread->lock_stat;
}
#endif

static void *
thread_runner(void *arg)
{
	struct thread *thr = arg;
	struct timeval start_time;
	struct timeval finish_time;

#if ENABLE_LOCK_STATS
	g_this_thread = thr;
	thr->lock_stat.lock_count = 0;
	thr->lock_stat.fail_count = 0;
#endif

	mm_thread_barrier_local_prepare(&thr->barrier);
	mm_thread_barrier_wait(&g_barrier, &thr->barrier);

	gettimeofday(&start_time, NULL);
	thr->start(thr->start_arg);
	gettimeofday(&finish_time, NULL);

	uint64_t start = start_time.tv_sec * _1M_ + start_time.tv_usec;
	uint64_t finish = finish_time.tv_sec * _1M_+ finish_time.tv_usec;
	thr->time = finish - start;

	return NULL;
}

static void
print_time(const char *name, uint64_t time)
{
	printf("%s: %u.%06u\n", name,
	       (unsigned) (time / _1M_),
	       (unsigned) (time % _1M_));
}

static void
print_thread_time(struct thread *thread)
{
	print_time(thread->name, thread->time);
}

void
test1(void *arg, void (*routine)(void*))
{
	int i, ret;

	g_threads = g_consumers;

	// Allocate thread table.
	struct thread *tt = calloc(g_threads, sizeof(struct thread));
	if (tt == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}

	// Initialize thread table.
	for (i = 0; i < g_consumers; i++) {
		struct thread *t = &tt[i];
		t->start = routine;
		t->start_arg = arg;
		snprintf(t->name, sizeof t->name, "thread #%02d", i);
	}

	/* Run threads. */
	mm_thread_barrier_prepare(&g_barrier, g_threads);
	for (i = 0; i < g_threads; i++) {
		ret = pthread_create(&tt[i].thread, NULL, thread_runner, &tt[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}
	for (i = 0; i < g_threads; i++) {
		pthread_join(tt[i].thread, NULL);
	}

	// Collect and print stats.
	uint64_t time_sum = 0;
	for (i = 0; i < g_threads; i++) {
		print_thread_time(&tt[i]);
		time_sum += tt[i].time;
	}
	print_time("total", time_sum);
	print_time("average", time_sum / g_threads);

	free(tt);
}

void
test2(void *arg, void (*producer)(void *arg), void (*consumer)(void *arg))
{
	int i, ret;

	g_threads = g_producers + g_consumers;

	// Allocate thread table.
	struct thread *tt = calloc(g_threads, sizeof(struct thread));
	if (tt == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}

	// Initialize thread table.
	for (i = 0; i < g_producers; i++) {
		struct thread *t = &tt[i];
		t->start = producer;
		t->start_arg = arg;
		snprintf(t->name, sizeof t->name, "producer #%02d", i);
	}
	for (i = 0; i < g_consumers; i++) {
		struct thread *t = &tt[i + g_producers];
		t->start = consumer;
		t->start_arg = arg;
		snprintf(t->name, sizeof t->name, "consumer #%02d", i);
	}

	/* Run threads. */
	mm_thread_barrier_prepare(&g_barrier, g_threads);
	for (i = 0; i < g_threads; i++) {
		ret = pthread_create(&tt[i].thread, NULL, thread_runner, &tt[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}
	for (i = 0; i < g_threads; i++) {
		pthread_join(tt[i].thread, NULL);
	}

	// Collect and print stats.
	uint64_t producer_time_sum = 0;
	uint64_t consumer_time_sum = 0;
	for (i = 0; i < g_threads; i++) {
		print_thread_time(&tt[i]);
		if (i < g_producers)
			producer_time_sum += tt[i].time;
		else
			consumer_time_sum += tt[i].time;
	}
	print_time("producers total", producer_time_sum);
	print_time("consumers total", consumer_time_sum);
	print_time("producers average", producer_time_sum / g_producers);
	print_time("consumers average", consumer_time_sum / g_consumers);

	free(tt);
}

void
delay_producer(void)
{
	for (unsigned long i = 0; i < g_producer_delay; i++)
		mm_compiler_barrier();
}

void
delay_consumer(void)
{
	for (unsigned long i = 0; i < g_consumer_delay; i++)
		mm_compiler_barrier();
}
