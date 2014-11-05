#include "base/lock.h"

#include "params.h"
#include "runner.h"

#include <stdlib.h>

mm_thread_lock_t g_lock = MM_THREAD_LOCK_INIT;

void
execute(void *arg __attribute__((unused)))
{
	mm_thread_lock(&g_lock);
	delay_consumer();
	mm_thread_unlock(&g_lock);
}

void
routine(void *arg __attribute__((unused)))
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
	test1(NULL, routine);
	return EXIT_SUCCESS;
}
