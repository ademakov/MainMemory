#include "params.h"
#include "runner.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "common.h"
#include "arch/atomic.h"
#include "arch/memory.h"
#include "arch/spin.h"

#define _1M_	1000000

static uint32_t g_threads;
static mm_atomic_uint32_t g_arrived;

struct thread
{
	pthread_t thread;

	void (*start)(void *);
	void *start_arg;

	uint64_t time;

	char name[128];
};

static void *
thread_runner(void *arg)
{
	struct thread *thr = arg;
	struct timeval start_time;
	struct timeval finish_time;

	mm_atomic_uint32_inc(&g_arrived);
	while (mm_memory_load(g_arrived) < g_threads)
		mm_spin_pause();

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
test(void *arg, void (*producer)(void *arg), void (*consumer)(void *arg))
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
