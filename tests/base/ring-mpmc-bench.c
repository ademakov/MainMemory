#include "base/ring.h"

#include "params.h"
#include "runner.h"

#include <stdlib.h>

struct mm_ring_mpmc *g_ring;

void
init(void)
{
	g_ring = mm_ring_mpmc_create(g_ring_size);
}

void
producer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	size_t i;

	for (i = 0; i < g_producer_data_size; i++) {
		delay_producer();
		mm_ring_mpmc_enqueue(ring, 1);
	}
}

void
single_producer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	size_t i;

	for (i = 0; i < g_producer_data_size; i++) {
		delay_producer();
		mm_ring_relaxed_enqueue(ring, 1);
	}
}

void
consumer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < g_consumer_data_size; i++) {
		uintptr_t data;
		mm_ring_mpmc_dequeue(ring, &data);
		result += data;
		delay_consumer();
	}
}

void
single_consumer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < g_consumer_data_size; i++) {
		uintptr_t data;
		mm_ring_relaxed_dequeue(ring, &data);
		result += data;
		delay_consumer();
	}
}

int
main(int ac, char **av)
{
	set_params(ac, av, TEST_RING);
	init();
	test2(g_ring,
	      g_producers == 1 && g_optimize ? single_producer : producer,
	      g_consumers == 1 && g_optimize ? single_consumer : consumer);
	return EXIT_SUCCESS;
}
