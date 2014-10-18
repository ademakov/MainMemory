
#define DEFAULT_PRODUCERS	4
#define DEFAULT_CONSUMERS	4
#define DEFAULT_RING_SIZE	128
#define DEFAULT_DATA_SIZE	((unsigned long) 100 * 1000 * 1000)

#define DEFAULT_PRODUCER_DELAY	250
#define DEFAULT_CONSUMER_DELAY	250

#ifndef TEST_STATIC_RING
# define TEST_STATIC_RING	0
#endif
#if TEST_STATIC_RING
#define RING_SIZE		DEFAULT_RING_SIZE
#else
#define RING_SIZE		g_ring_size
extern int g_ring_size;
#endif

extern int g_producers;
extern int g_consumers;

extern unsigned long g_data_size;
extern unsigned long g_producer_data_size;
extern unsigned long g_consumer_data_size;

extern unsigned long g_producer_delay;
extern unsigned long g_consumer_delay;

void set_params(int ac, char **av);
