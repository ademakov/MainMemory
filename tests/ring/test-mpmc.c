#include "ring.h"
#include "params.h"
#include "runner.h"
#include <stdlib.h>

MM_RING_MPMC(ring, RING_SIZE) g_ring;

void
init(void)
{
	mm_ring_mpmc_prepare(&g_ring.ring, RING_SIZE);
}

void
producer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	size_t i;

	for (i = 0; i < DATA_SIZE; i++) {
		mm_ring_mpmc_enqueue(ring, 1);
	}
}

void
consumer(void *arg)
{
	struct mm_ring_mpmc *ring = arg;
	volatile uintptr_t result = 0;
	size_t i;

	for (i = 0; i < DATA_SIZE; i++) {
		uintptr_t data;
		mm_ring_mpmc_dequeue(ring, &data);
		result += data;
	}
}

int
main()
{
	init();
	test(&g_ring.ring, producer, consumer);
	return EXIT_SUCCESS;
}
