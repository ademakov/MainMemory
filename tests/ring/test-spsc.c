#include "ring.h"
#include "params.h"
#include "runner.h"
#include <stdlib.h>

MM_RING_SPSC(ring, RING_SIZE) g_ring;

void
init(void)
{
	mm_ring_prepare_locked(&g_ring.ring, RING_SIZE,
			       MM_RING_GLOBAL_PUT | MM_RING_GLOBAL_GET);
}

void
producer(void *arg)
{
	struct mm_ring_spsc *ring = arg;
	size_t i;

	for (i = 0; i < DATA_SIZE; i++) {
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

	for (i = 0; i < DATA_SIZE; i++) {
		void *data;
		while (!mm_ring_global_get(ring, &data))
			;
		result += (uintptr_t) data;
	}
}

int
main()
{
	init();
	test(&g_ring.ring, producer, consumer);
	return EXIT_SUCCESS;
}
