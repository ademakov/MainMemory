#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/memory/cache.h"

static int fail = 0;

void *
allocate(struct mm_memory_cache *cache, size_t size)
{
	void *data = mm_memory_cache_alloc(cache, size);
	if (data == NULL) {
		fail = 1;
		fprintf(stderr, "failed to allocate a memory chunk of size %zu\n", size);
	}
	return data;
}

void
test_trivial()
{
	printf("trivial case\n");

	struct mm_memory_cache cache;
	mm_memory_cache_prepare(&cache, NULL);
	mm_memory_cache_cleanup(&cache);
}

void
test_alloc(const char *title, const size_t size)
{
	printf("%s\n", title);

	struct mm_memory_cache cache;
	mm_memory_cache_prepare(&cache, NULL);

	void *data = allocate(&cache, size);
	memset(data, 0, size);
	mm_memory_cache_free(&cache, data);

	mm_memory_cache_cleanup(&cache);
}

void
test_alloc_2(const char *title, const size_t size)
{
	printf("%s\n", title);

	struct mm_memory_cache cache;
	mm_memory_cache_prepare(&cache, NULL);

	char *data = allocate(&cache, size);
	memset(data, 0x01, size);

	char *data2 = allocate(&cache, size);
	memset(data2, 0x10, size);

	for (size_t i = 0; i < size; i++) {
		if (data[i] != 0x01) {
			fail = 1;
			fprintf(stderr, "content corruption\n");
			return;
		}
		if (data2[i] != 0x10) {
			fail = 1;
			fprintf(stderr, "content corruption\n");
			return;
		}
	}

	mm_memory_cache_free(&cache, data);
	mm_memory_cache_free(&cache, data2);

	mm_memory_cache_cleanup(&cache);
}

int
main()
{
	test_trivial();

	test_alloc("huge allocation case", 8 * 1024 * 1024);
	test_alloc_2("huge allocation case 2", 8 * 1024 * 1024);

	test_alloc("large allocation case", 256 * 1024);
	test_alloc_2("large allocation case 2", 256 * 1024);

	test_alloc("medium allocation case", 512);
	test_alloc_2("medium allocation case 2", 512);

	test_alloc("small allocation case", 16);
	test_alloc_2("small allocation case 2", 16);

	printf("finished\n");

	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
