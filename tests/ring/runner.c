#include "params.h"
#include "runner.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct thread
{
	pthread_t thread;

	void (*start)(void *);
	void *start_arg;

	char name[128];
};

struct thread g_producers[PRODUCERS];
struct thread g_consumers[CONSUMERS];

void *
thread_runner(void *arg)
{
	struct thread *thr = arg;
	printf("start %s\n", thr->name);

	thr->start(thr->start_arg);

	printf("stop %s\n", thr->name);
	return NULL;
}

void
test(void *arg, void (*producer)(void *arg), void (*consumer)(void *arg))
{
	size_t i;
	int ret;

	/* Init thread data. */
	for (i = 0; i < PRODUCERS; i++) {
		g_producers[i].start = producer;
		g_producers[i].start_arg = arg;

		snprintf(g_producers[i].name, sizeof g_producers[i].name,
			"producer %d", i);
	}
	for (i = 0; i < CONSUMERS; i++) {
		g_consumers[i].start = consumer;
		g_consumers[i].start_arg = arg;

		snprintf(g_consumers[i].name, sizeof g_consumers[i].name,
			"consumer %d", i);
	}

	/* Run threads. */
	for (i = 0; i < PRODUCERS; i++) {
		ret = pthread_create(&g_producers[i].thread, NULL,
				     thread_runner, &g_producers[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}
	for (i = 0; i < CONSUMERS; i++) {
		ret = pthread_create(&g_consumers[i].thread, NULL,
				     thread_runner, &g_consumers[i]);
		if (ret) {
			fprintf(stderr, "failed to create a thread\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Wait threads. */
	for (i = 0; i < PRODUCERS; i++) {
		pthread_join(g_producers[i].thread, NULL);
	}
	for (i = 0; i < CONSUMERS; i++) {
		pthread_join(g_consumers[i].thread, NULL);
	}
}
