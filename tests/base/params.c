#include "params.h"

#include "base/bitops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_test = TEST_LOCK;

int g_producers = DEFAULT_PRODUCERS;
int g_consumers = DEFAULT_CONSUMERS;

int g_handoff = DEFAULT_HANDOFF;

int g_ring_size = DEFAULT_RING_SIZE;

unsigned long g_data_size = DEFAULT_DATA_SIZE;
unsigned long g_producer_data_size = DEFAULT_DATA_SIZE / DEFAULT_PRODUCERS;
unsigned long g_consumer_data_size = DEFAULT_DATA_SIZE / DEFAULT_CONSUMERS;

unsigned long g_producer_delay = DEFAULT_PRODUCER_DELAY;
unsigned long g_consumer_delay = DEFAULT_CONSUMER_DELAY;

int g_optimize = 0;

static void NORETURN
usage(char *prog_name, char *message)
{
	char *slash = strrchr(prog_name, '/');
	if (slash != NULL && *(slash + 1))
		prog_name = slash + 1;

	if (message != NULL)
		fprintf(stderr, "%s: %s\n", prog_name, message);

	if (g_test == TEST_RING)
		fprintf(stderr,
			"Usage:\n\t%s"
			" [-p <producers>]"
			" [-c <consumers>]"
			" [-e <producer-delay>]"
			" [-d <consumer-delay>]"
			" [-r <ring-size>]"
			" [-n <repeat-count>]"
			" [-o]\n",
			prog_name);
	else if (g_test == TEST_LOCK)
		fprintf(stderr,
			"Usage:\n\t%s"
			" [-c <concurrency>]"
			" [-e <producer-delay>]"
			" [-d <consumer-delay>]"
			" [-n <repeat-count>]\n",
			prog_name);
	else
		fprintf(stderr,
			"Usage:\n\t%s"
			" [-c <concurrency>]"
			" [-e <producer-delay>]"
			" [-d <consumer-delay>]"
			" [-r <combiner-size>]"
			" [-f <combiner-handoff>]"
			" [-n <repeat-count>]\n",
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
	if (is_int && value != (unsigned long) ((long) ((int) value)))
		usage(prog_name, "too large value ");
	return value;
}

void
set_params(int ac, char **av, int test)
{
	static const char *lock_options = ":c:n:e:d:";
	static const char *ring_options = ":p:c:r:n:e:d:o";
	static const char *combiner_options = ":c:r:f:n:e:d:";

	const char *options =
		test == TEST_LOCK ? lock_options :
			test == TEST_RING ? ring_options :
				combiner_options;
	int c;

	g_test = test;
	while ((c = getopt (ac, av, options)) != -1) {
		switch (c) {
		case 'p':
			g_producers = getnum(av[0], optarg, 1, 0);
			break;
		case 'c':
			g_consumers = getnum(av[0], optarg, 1, 0);
			break;
		case 'f':
			g_handoff = getnum(av[0], optarg, 1, 0);
			break;
		case 'r':
			g_ring_size = getnum(av[0], optarg, 1, 0);
			break;
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

	if (g_test == TEST_RING) {
		if (!mm_is_pow2(g_ring_size))
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
			g_producers, g_consumers,
			g_ring_size, g_data_size,
			g_producer_delay, g_consumer_delay,
			g_optimize ? "yes" : "no");
	} else if (test == TEST_LOCK) {
		g_consumer_data_size = g_data_size / g_consumers;
		fprintf(stderr,
			"concurrency: %d\n"
			"repeat count: %lu\n"
			"producer delay: %lu\n"
			"consumer delay: %lu\n",
			g_consumers, g_data_size,
			g_producer_delay, g_consumer_delay);
	} else {
		g_consumer_data_size = g_data_size / g_consumers;
		fprintf(stderr,
			"concurrency: %d\n"
			"combiner size: %d\n"
			"combiner handoff: %d\n"
			"repeat count: %lu\n"
			"producer delay: %lu\n"
			"consumer delay: %lu\n",
			g_consumers,
			g_ring_size, g_handoff,
			g_data_size,
			g_producer_delay, g_consumer_delay);
	}
}
