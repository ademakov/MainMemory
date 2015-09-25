#include "common.h"

enum {
	TEST_LOCK,
	TEST_RING,
	TEST_COMBINER,
};

#define DEFAULT_PRODUCERS	4
#define DEFAULT_CONSUMERS	4

#define DEFAULT_HANDOFF		16

#define DEFAULT_RING_SIZE	128
#define DEFAULT_DATA_SIZE	((unsigned long) 100 * 1000 * 1000)

#define DEFAULT_PRODUCER_DELAY	250
#define DEFAULT_CONSUMER_DELAY	250

extern int g_producers;
extern int g_consumers;

extern int g_handoff;

extern int g_ring_size;

extern unsigned long g_data_size;
extern unsigned long g_producer_data_size;
extern unsigned long g_consumer_data_size;

extern unsigned long g_producer_delay;
extern unsigned long g_consumer_delay;

extern int g_optimize;

void set_params(int ac, char **av, int test);
