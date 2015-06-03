#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "base/bitops.h"

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

int fail = 0;

int
main()
{
	test(mm_lower_pow2(0), 0);
	test(mm_upper_pow2(0), 0);
	test(mm_lower_pow2(1), 1);
	test(mm_upper_pow2(1), 1);
	test(mm_lower_pow2(2), 2);
	test(mm_upper_pow2(2), 2);
	test(mm_lower_pow2(3), 2);
	test(mm_upper_pow2(3), 4);
	test(mm_lower_pow2(4), 4);
	test(mm_upper_pow2(4), 4);
	test(mm_lower_pow2(5), 4);
	test(mm_upper_pow2(5), 8);
	test(mm_lower_pow2(7), 4);
	test(mm_upper_pow2(7), 8);
	test(mm_lower_pow2(8), 8);
	test(mm_upper_pow2(8), 8);

	test(mm_lower_pow2(7u), 4);
	test(mm_upper_pow2(7u), 8);
	test(mm_lower_pow2(7l), 4);
	test(mm_upper_pow2(7l), 8);
	test(mm_lower_pow2(7ul), 4);
	test(mm_upper_pow2(7ul), 8);
	test(mm_lower_pow2(7ll), 4);
	test(mm_upper_pow2(7ll), 8);
	test(mm_lower_pow2(7ull), 4);
	test(mm_upper_pow2(7ull), 8);

	test(mm_lower_pow2(0x70000000000001ull), 0x40000000000000ull);
	test(mm_upper_pow2(0x70000000000001ull), 0x80000000000000ull);
	test(mm_lower_pow2(0x7fffffffffffffull), 0x40000000000000ull);
	test(mm_upper_pow2(0x7fffffffffffffull), 0x80000000000000ull);

	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
