#include "base/combiner.h"
#include "base/mem/memory.h"

#include "params.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

struct mm_combiner *g_combiner;
size_t g_nexec = 0;

void
execute(uintptr_t unused __mm_unused__)
{
	delay_consumer();
	g_nexec++;
}

void
routine(void *arg __mm_unused__)
{
	size_t i;
	for (i = 0; i < g_consumer_data_size; i++) {
		delay_producer();
		mm_combiner_execute(g_combiner, 0);
	}
}

extern size_t xxx[1000];

int
main(int ac, char **av)
{
	mm_memory_init(NULL, NULL, NULL);
	set_params(ac, av, TEST_COMBINER);
	g_combiner = mm_combiner_create(execute, g_ring_size, g_handoff);
	test1(NULL, routine);
	printf("nexec: %zu\n", g_nexec);
	return EXIT_SUCCESS;
}
