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

static mm_atomic_uint32_t arrived;

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

	mm_atomic_uint32_inc(&arrived);
	while (mm_memory_load(arrived) < (PRODUCERS + CONSUMERS))
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
print_time(struct thread *thread)
{
	char *name = thread->name;
	uint64_t time = thread->time;
	printf("%s %u.%06u\n", name,
	       (unsigned) (time / _1M_),
	       (unsigned) (time % _1M_));
}

void
test(void *arg, void (*producer)(void *arg), void (*consumer)(void *arg))
{
	int i, ret;

	struct thread *producers = calloc(PRODUCERS, sizeof(struct thread));
	struct thread *consumers = calloc(CONSUMERS, sizeof(struct thread));
	if (producers == NULL || consumers == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}

	/* Init thread data. */
	for (i = 0; i < PRODUCERS; i++) {
		producers[i].start = producer;
		producers[i].start_arg = arg;

		snprintf(producers[i].name, sizeof producers[i].name,
			"producer #%02d", i);
	}
	for (i = 0; i < CONSUMERS; i++) {
		consumers[i].start = consumer;
		consumers[i].start_arg = arg;

		snprintf(consumers[i].name, sizeof consumers[i].name,
			"consumer #%02d", i);
	}

	/* Run threads. */
	for (i = 0; i < PRODUCERS; i++) {
		ret = pthread_create(&producers[i].thread, NULL,
				     thread_runner, &producers[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}
	for (i = 0; i < CONSUMERS; i++) {
		ret = pthread_create(&consumers[i].thread, NULL,
				     thread_runner, &consumers[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Wait threads. */
	for (i = 0; i < PRODUCERS; i++) {
		pthread_join(producers[i].thread, NULL);
	}
	for (i = 0; i < CONSUMERS; i++) {
		pthread_join(consumers[i].thread, NULL);
	}

	// Print stats.
	uint64_t producer_time_sum = 0;
	uint64_t consumer_time_sum = 0;
	for (i = 0; i < PRODUCERS; i++) {
		print_time(&producers[i]);
		producer_time_sum += producers[i].time;
	}
	for (i = 0; i < CONSUMERS; i++) {
		print_time(&consumers[i]);
		consumer_time_sum += consumers[i].time;
	}
	uint64_t producer_time_avg = producer_time_sum / PRODUCERS;
	uint64_t consumer_time_avg = consumer_time_sum / CONSUMERS;
	printf("producers total: %u.%06u\n",
	       (unsigned) (producer_time_sum / _1M_),
	       (unsigned) (producer_time_sum % _1M_));
	printf("producers average: %u.%06u\n",
	       (unsigned) (producer_time_avg / _1M_),
	       (unsigned) (producer_time_avg % _1M_));
	printf("consumers total: %u.%06u\n",
	       (unsigned) (consumer_time_sum / _1M_),
	       (unsigned) (consumer_time_sum % _1M_));
	printf("consumers average: %u.%06u\n",
	       (unsigned) (consumer_time_avg / _1M_),
	       (unsigned) (consumer_time_avg % _1M_));

	free(producers);
	free(consumers);
}
