#include "base/ring.h"
#include "base/mem/space.h"

#include "params.h"
#include "runner.h"

#include <stdlib.h>

#define LOCKS (MM_RING_LOCKED_PUT | MM_RING_LOCKED_GET)

#if TEST_STATIC_RING

MM_RING_SPSC(ring, RING_SIZE) _g_ring;
struct mm_ring_spsc *g_ring = &_g_ring.ring;

void
init(void)
{
	mm_ring_spsc_prepare(g_ring, RING_SIZE, LOCKS);
}

#else

struct mm_ring_spsc *g_ring;

void
init(void)
{
	g_ring = mm_ring_spsc_create(RING_SIZE, LOCKS);
}

#endif

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
	mm_common_space_init();
	init();
	test2(g_ring,
	      g_producers == 1 && g_optimize ? single_producer : producer,
	      g_consumers == 1 && g_optimize ? single_consumer : consumer);
	return EXIT_SUCCESS;
}
