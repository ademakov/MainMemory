#include "base/ring.h"

#include "params.h"
#include "runner.h"

#include <stdlib.h>

#define LOCKS (MM_RING_LOCKED_PUT | MM_RING_LOCKED_GET)

struct mm_ring_spsc *g_ring;

void
init(void)
{
	g_ring = mm_ring_spsc_create(g_ring_size, LOCKS);
}

void
producer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	size_t i;

	for (i = 0; i < g_producer_data_size; i++) {
		delay_producer();
		while (!mm_ring_spsc_locked_put(ring, (void *) 1))
			;
	}
}

void
single_producer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	size_t i;

	for (i = 0; i < g_producer_data_size; i++) {
		delay_producer();
		while (!mm_ring_spsc_put(ring, (void *) 1))
			;
	}
}

void
consumer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	void *data = NULL;
	for (i = 0; i < g_consumer_data_size; i++) {
		while (!mm_ring_spsc_locked_get(ring, &data))
			;
		result += (uintptr_t) data;
		delay_consumer();
	}
}

void
single_consumer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < g_consumer_data_size; i++) {
		void *data;
		while (!mm_ring_spsc_get(ring, &data))
			;
		result += (uintptr_t) data;
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
