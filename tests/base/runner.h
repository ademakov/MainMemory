
void test1(void *arg, void (*routine)(void*));
void test2(void *arg, void (*producer)(void*), void (*consumer)(void*));

void delay_producer(void);
void delay_consumer(void);
