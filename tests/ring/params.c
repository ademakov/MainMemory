#include "params.h"
#include "bitops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int g_producers = DEFAULT_PRODUCERS;
int g_consumers = DEFAULT_CONSUMERS;

#if !TEST_STATIC_RING
int g_ring_size = DEFAULT_RING_SIZE;
#endif

unsigned long g_data_size = DEFAULT_DATA_SIZE;
unsigned long g_producer_data_size = DEFAULT_DATA_SIZE / DEFAULT_PRODUCERS;
unsigned long g_consumer_data_size = DEFAULT_DATA_SIZE / DEFAULT_CONSUMERS;

unsigned long g_producer_delay = DEFAULT_PRODUCER_DELAY;
unsigned long g_consumer_delay = DEFAULT_CONSUMER_DELAY;

int g_optimize = 0;

static void
usage(char *prog_name, char *message)
{
	char *slash = strrchr(prog_name, '/');
	if (slash != NULL && *(slash + 1))
		prog_name = slash + 1;

	if (message != NULL)
		fprintf(stderr, "%s: %s\n", prog_name, message);

	fprintf(stderr,
		"Usage:\n\t"
		"%s [-p <producers>] [-c <consumers>] [-n <repeat-count>]"
#if !TEST_STATIC_RING
		" [-r <ring-size>]"
#endif
		" [-e <producer-delay>] [-d <consumer-delay>]\n",
		prog_name);

	exit(EXIT_FAILURE);
}

static unsigned long
getnum(char *prog_name, const char *s, int is_int, int allow_zero)
{
	char *end;
	unsigned long value = strtoul(s, &end, 0);
	if (*end != 0)
		usage(prog_name, "invalid value");
	if (value == 0 && !allow_zero)
		usage(prog_name, "invalid value");
	if (is_int && value != ((long) ((int) value)))
		usage(prog_name, "too large value ");
	return value;
}

void
set_params(int ac, char **av)
{
#if TEST_STATIC_RING
	static const char *options = ":p:c:n:e:d:o";
#else
	static const char *options = ":p:c:r:n:e:d:o";
#endif
	int c;

	while ((c = getopt (ac, av, options)) != -1) {
		switch (c) {
		case 'p':
			g_producers = getnum(av[0], optarg, 1, 0);
			break;
		case 'c':
			g_consumers = getnum(av[0], optarg, 1, 0);
			break;
#if !TEST_STATIC_RING
		case 'r':
			g_ring_size = getnum(av[0], optarg, 1, 0);
			break;
#endif
		case 'n':
			g_data_size = getnum(av[0], optarg, 0, 0);
			break;
		case 'e':
			g_producer_delay = getnum(av[0], optarg, 0, 1);
			break;
		case 'd':
			g_consumer_delay = getnum(av[0], optarg, 0, 1);
			break;
		case 'o':
			g_optimize = 1;
			break;
		case ':':
			usage(av[0], "missing option value");
		default:
			usage(av[0], "invalid option");
		}
	}

	if (!mm_is_pow2(RING_SIZE))
		usage(av[0], "ring size must be a power of two");

	g_producer_data_size = g_data_size / g_producers;
	g_consumer_data_size = g_data_size / g_consumers;
	if (g_producer_data_size * g_producers != g_consumer_data_size * g_consumers)
		usage(av[0], "odd distribution between consumers and producers");

	fprintf(stderr,
		"producers: %d\n"
		"consumers: %d\n"
		"ring size: %d\n"
		"repeat count: %lu\n"
		"producer delay: %lu\n"
		"consumer delay: %lu\n"
		"optimize for single thread: %s\n",
		g_producers, g_consumers, RING_SIZE, g_data_size,
		g_producer_delay, g_consumer_delay,
		g_optimize ? "yes" : "no");
}
