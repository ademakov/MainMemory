#include "ring.h"
#include "params.h"
#include "runner.h"
#include <stdlib.h>

#define LOCKS (MM_RING_GLOBAL_PUT | MM_RING_GLOBAL_GET)

#if SET_PARAMS

struct mm_ring_spsc *g_ring;

void
init(void)
{
	g_ring = mm_ring_spsc_create(RING_SIZE, LOCKS);
}

#else

MM_RING_SPSC(ring, RING_SIZE) _g_ring;
struct mm_ring_spsc *g_ring = &_g_ring.ring;

void
init(void)
{
	mm_ring_spsc_prepare(g_ring, RING_SIZE, LOCKS);
}

#endif

void
producer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	size_t i;

	for (i = 0; i < PRODUCER_DATA_SIZE; i++) {
		while (!mm_ring_global_put(ring, (void *) 1))
			;
	}
}

void
consumer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < CONSUMER_DATA_SIZE; i++) {
		void *data;
		while (!mm_ring_global_get(ring, &data))
			;
		result += (uintptr_t) data;
	}
}

int
main(int ac, char **av)
{
	set_params(ac, av);
	init();
	test(g_ring, producer, consumer);
	return EXIT_SUCCESS;
}
