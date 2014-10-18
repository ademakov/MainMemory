
#define SET_PARAMS		1

#define DEFAULT_PRODUCERS	4
#define DEFAULT_CONSUMERS	4
#define DEFAULT_RING_SIZE	128
#define DEFAULT_DATA_SIZE	((unsigned long) 100 * 1000 * 1000)

#if SET_PARAMS

#define PRODUCERS		g_producers
#define CONSUMERS		g_consumers
#define RING_SIZE		g_ring_size
#define DATA_SIZE		g_data_size

#define PRODUCER_DATA_SIZE	g_producer_data_size
#define CONSUMER_DATA_SIZE	g_consumer_data_size

extern int g_producers;
extern int g_consumers;
extern int g_ring_size;
extern unsigned long g_data_size;
extern unsigned long g_producer_data_size;
extern unsigned long g_consumer_data_size;
#else

#define PRODUCERS		DEFAULT_PRODUCERS
#define CONSUMERS		DEFAULT_CONSUMERS
#define RING_SIZE		DEFAULT_RING_SIZE
#define DATA_SIZE		DEFAULT_DATA_SIZE

#define PRODUCER_DATA_SIZE	(DATA_SIZE / PRODUCERS)
#define CONSUMER_DATA_SIZE	(DATA_SIZE / CONSUMERS)

#endif

void set_params(int ac, char **av);
