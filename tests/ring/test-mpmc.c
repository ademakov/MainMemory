#include "ring.h"
#include "params.h"
#include "runner.h"
#include <stdlib.h>

#if SET_PARAMS

struct mm_ring_mpmc *g_ring;

void
init(void)
{
	g_ring = mm_ring_mpmc_create(RING_SIZE);
}

#else

MM_RING_MPMC(ring, RING_SIZE) _g_ring;
struct mm_ring_mpmc *g_ring = &_g_ring.ring;

void
init(void)
{
	mm_ring_mpmc_prepare(g_ring, RING_SIZE);
}

#endif

void
producer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	size_t i;

	for (i = 0; i < PRODUCER_DATA_SIZE; i++) {
		mm_ring_mpmc_enqueue(ring, 1);
	}
}

void
consumer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < CONSUMER_DATA_SIZE; i++) {
		uintptr_t data;
		mm_ring_mpmc_dequeue(ring, &data);
		result += data;
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
