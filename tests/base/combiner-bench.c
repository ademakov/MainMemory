#include "base/combiner.h"

#include "params.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

struct mm_combiner *g_combiner;
size_t g_nexec = 0;

void
execute(uintptr_t unused UNUSED)
{
	delay_consumer();
	g_nexec++;
}

void
routine(void *arg UNUSED)
{
	size_t i;
	for (i = 0; i < g_consumer_data_size; i++) {
		delay_producer();
		mm_combiner_execute(g_combiner, execute, 0);
	}
}

int
main(int ac, char **av)
{
	set_params(ac, av, TEST_COMBINER);
	g_combiner = mm_combiner_create(g_ring_size, g_handoff);
	test1(NULL, routine);
	printf("nexec: %zu\n", g_nexec);
	return EXIT_SUCCESS;
}
