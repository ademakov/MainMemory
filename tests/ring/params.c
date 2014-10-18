#include "params.h"
#include "bitops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
info(void)
{
	fprintf(stderr,
		"producers: %d\n"
		"consumers: %d\n"
		"ring size: %d\n"
		"repeat count: %lu\n",
		PRODUCERS, CONSUMERS, RING_SIZE, DATA_SIZE);
}

#if SET_PARAMS

int g_producers = DEFAULT_PRODUCERS;
int g_consumers = DEFAULT_CONSUMERS;
int g_ring_size = DEFAULT_RING_SIZE;
unsigned long g_data_size = DEFAULT_DATA_SIZE;
unsigned long g_producer_data_size = DEFAULT_DATA_SIZE / DEFAULT_PRODUCERS;
unsigned long g_consumer_data_size = DEFAULT_DATA_SIZE / DEFAULT_CONSUMERS;

void
usage(char *prog_name, char *message)
{
	char *slash = strrchr(prog_name, '/');
	if (slash != NULL && *(slash + 1))
		prog_name = slash + 1;

	if (message != NULL)
		fprintf(stderr, "%s: %s\n", prog_name, message);

	fprintf(stderr,
		"Usage:\n\t"
		"%s [-p <producers>] [-c <consumers>] [-r <ring-size>] [-n <repeat-count>]\n",
		prog_name);

	exit(EXIT_FAILURE);
}

unsigned long
getnum(char *prog_name, const char *s, int is_int)
{
	char *end;
	unsigned long value = strtoul(s, &end, 0);
	if (value == 0 || *end != 0)
		usage(prog_name, "invalid value");
	if (is_int && value != ((long) ((int) value)))
		usage(prog_name, "too large value ");
	return value;
}

void
set_params(int ac, char **av)
{
	int c;
	while ((c = getopt (ac, av, ":p:c:r:n:")) != -1) {
		switch (c) {
		case 'p':
			g_producers = getnum(av[0], optarg, 1);
			break;
		case 'c':
			g_consumers = getnum(av[0], optarg, 1);
			break;
		case 'r':
			g_ring_size = getnum(av[0], optarg, 1);
			break;
		case 'n':
			g_data_size = getnum(av[0], optarg, 0);
			break;
		default:
			usage(av[0], "invalid option");
		}
	}

	info();

	if (!mm_is_pow2(g_ring_size))
		usage(av[0], "ring size must be a power of two");

	g_producer_data_size = g_data_size / g_producers;
	g_consumer_data_size = g_data_size / g_consumers;
	if (g_producer_data_size * g_producers != g_consumer_data_size * g_consumers)
		usage(av[0], "odd distribution between consumers and producers");
}

#else

void
set_params(int ac __attribute__((unused)), char **av __attribute__((unused)))
{
	info();
}

#endif
