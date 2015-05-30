#include "base/lock.h"
#include "base/mem/space.h"

#include "params.h"
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>

mm_thread_lock_t g_lock = MM_THREAD_LOCK_INIT;
size_t g_nexec = 0;

void
execute(void *arg __mm_unused__)
{
	mm_thread_lock(&g_lock);
	delay_consumer();
	g_nexec++;
	mm_thread_unlock(&g_lock);
}

void
routine(void *arg __mm_unused__)
{
	size_t i;
	for (i = 0; i < g_consumer_data_size; i++) {
		delay_producer();
		execute(NULL);
	}
}

int
main(int ac, char **av)
{
	set_params(ac, av, TEST_LOCK);
	mm_common_space_init();
	test1(NULL, routine);
	printf("nexec: %zu\n", g_nexec);
	return EXIT_SUCCESS;
}
