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
			"producer %d", i);
	}
	for (i = 0; i < CONSUMERS; i++) {
		consumers[i].start = consumer;
		consumers[i].start_arg = arg;

		snprintf(consumers[i].name, sizeof consumers[i].name,
			"consumer %d", i);
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

	free(producers);
	free(consumers);
}
