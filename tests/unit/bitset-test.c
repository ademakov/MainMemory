#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "base/bitset.h"
#include "base/memory/global.h"

#define test(v, e)						\
	do {							\
		typeof(v) _v = v;				\
		typeof(v) _e = e;				\
		if (_v != _e) {					\
			fprintf(stderr, "# expect: %llu\n",	\
				(unsigned long long) _e);	\
			fprintf(stderr, "# really: %llu\n", 	\
				(unsigned long long) _v);	\
			fail++;					\
		}						\
	} while(0)

static int fail = 0;

void
test_find_small(void)
{
	struct mm_bitset set;

	mm_bitset_prepare(&set, &mm_global_arena, 64);
	test(mm_bitset_find(&set, 0), MM_BITSET_NONE);

	mm_bitset_set(&set, 0);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), MM_BITSET_NONE);

	mm_bitset_set(&set, 1);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), 1);
	test(mm_bitset_find(&set, 2), MM_BITSET_NONE);

	mm_bitset_set(&set, 32);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), 1);
	test(mm_bitset_find(&set, 2), 32);
	test(mm_bitset_find(&set, 33), MM_BITSET_NONE);

	mm_bitset_cleanup(&set, &mm_global_arena);
}

void
test_find_large(void)
{
	struct mm_bitset set;

	mm_bitset_prepare(&set, &mm_global_arena, 1024);
	test(mm_bitset_find(&set, 0), MM_BITSET_NONE);

	mm_bitset_set(&set, 0);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), MM_BITSET_NONE);

	mm_bitset_set(&set, 1);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), 1);
	test(mm_bitset_find(&set, 2), MM_BITSET_NONE);

	mm_bitset_set(&set, 32);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), 1);
	test(mm_bitset_find(&set, 2), 32);
	test(mm_bitset_find(&set, 33), MM_BITSET_NONE);

	mm_bitset_set(&set, 900);
	test(mm_bitset_find(&set, 0), 0);
	test(mm_bitset_find(&set, 1), 1);
	test(mm_bitset_find(&set, 2), 32);
	test(mm_bitset_find(&set, 900), 900);
	test(mm_bitset_find(&set, 901), MM_BITSET_NONE);

	mm_bitset_cleanup(&set, &mm_global_arena);
}

int
main()
{
	test_find_small();
	test_find_large();
	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
