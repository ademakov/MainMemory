#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "base/scan.h"

static int fail = 0;

#define TEST_INT_EXT(text, value, error, func, type, fmt)		\
	do {								\
		int x = 0;						\
		type v = 0;						\
		const char *ep = text + strlen(text);			\
		const char *rp = func(&v, &x, text, ep);		\
		if (x != error)	{					\
			fprintf(stderr, "# number: %s\n", text);	\
			if (rp != ep) {					\
				fprintf(stderr, "# stop at: %s\n", rp);	\
			}						\
			fprintf(stderr, "# expect: %d\n", error);	\
			fprintf(stderr, "# really: %d\n", x);		\
			fail++;						\
		}							\
		if (v != (type) value) {				\
			fprintf(stderr, "# number: %s\n", text);	\
			if (rp != ep) {					\
				fprintf(stderr, "# stop at: %s\n", rp);	\
			}						\
			fprintf(stderr, "# expect: %" fmt "\n", 	\
				(type) value);				\
			fprintf(stderr, "# really: %" fmt "\n",	v);	\
			fail++;						\
		}							\
	} while(0)

#define S(x)	#x
#define TEST_INT(value, func, type, fmt)				\
	TEST_INT_EXT(S(value), value, 0, func, type, fmt)

#define TEST_HEX(value, func, type, fmt)				\
	TEST_INT_EXT(S(value) + 2, value, 0, func, type, fmt)

#define TEST_INT_END(text, value, error, func, type, fmt, end)		\
	do {								\
		int x = 0;						\
		type v = 0;						\
		const char *ep = text + strlen(text);			\
		const char *rp = func(&v, &x, text, ep);		\
		if (strcmp(rp, end) != 0) {				\
			fprintf(stderr, "# number: %s\n", text);	\
			fprintf(stderr, "# expect: %s\n", end);		\
			fprintf(stderr, "# really: %s\n", rp);		\
			fail++;						\
		}							\
		if (x != error)	{					\
			fprintf(stderr, "# number: %s\n", text);	\
			fprintf(stderr, "# expect: %d\n", error);	\
			fprintf(stderr, "# really: %d\n", x);		\
			fail++;						\
		}							\
		if (v != (type) value) {				\
			fprintf(stderr, "# number: %s\n", text);	\
			fprintf(stderr, "# expect: %" fmt "\n", 	\
				(type) value);				\
			fprintf(stderr, "# really: %" fmt "\n",	v);	\
			fail++;						\
		}							\
	} while(0)

int
main()
{
	TEST_INT(0, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(1, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(2, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(3, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(4, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(5, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(6, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(7, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(8, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(9, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(11, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(99, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(111, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(999, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(1111, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(9999, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(1234567890, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(UINT32_MAX, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT(UINT32_MAX, mm_scan_u64, uint64_t, PRIu64);
	TEST_INT(UINT64_MAX, mm_scan_u64, uint64_t, PRIu64);

	TEST_HEX(0X0, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x0, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x1, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x2, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x3, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x4, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x5, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x6, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x7, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x8, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x9, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xA, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xB, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xC, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xD, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xE, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xF, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xa, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xb, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xc, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xd, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xe, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xf, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x12345678, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x9abcdef0, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0xffffffff, mm_scan_x32, uint32_t, PRIx32);
	TEST_HEX(0x0123456789abcdef, mm_scan_x64, uint64_t, PRIx64);
	TEST_HEX(0xffffffffffffffff, mm_scan_x64, uint64_t, PRIx64);

	TEST_INT(0, mm_scan_n32, uint32_t, PRIu32);
	TEST_INT(1, mm_scan_n32, uint32_t, PRIu32);
	TEST_INT(UINT32_MAX, mm_scan_n32, uint32_t, PRIu32);
	TEST_INT(0x0, mm_scan_n32, uint32_t, PRIx32);
	TEST_INT(0x1, mm_scan_n32, uint32_t, PRIx32);
	TEST_INT(0xffffffff, mm_scan_n32, uint32_t, PRIx32);

	TEST_INT(0, mm_scan_d32, int32_t, PRId32);
	TEST_INT(1, mm_scan_d32, int32_t, PRId32);
	TEST_INT(-1, mm_scan_d32, int32_t, PRId32);
	TEST_INT(+1, mm_scan_d32, int32_t, PRId32);
	TEST_INT(INT32_MAX, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("+2147483647", 2147483647, 0, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("-2147483648", -2147483648, 0, mm_scan_d32, int32_t, PRId32);

	TEST_INT(0, mm_scan_i32, int32_t, PRId32);
	TEST_INT(1, mm_scan_i32, int32_t, PRId32);
	TEST_INT(-1, mm_scan_i32, int32_t, PRId32);
	TEST_INT(+1, mm_scan_i32, int32_t, PRId32);
	TEST_INT(INT32_MAX, mm_scan_i32, int32_t, PRId32);
	TEST_INT_EXT("+2147483647", 2147483647, 0, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("-2147483648", -2147483648, 0, mm_scan_i32, int32_t, PRId32);
	TEST_INT(0x0, mm_scan_i32, int32_t, PRIx32);
	TEST_INT(0x1, mm_scan_i32, int32_t, PRIx32);
	TEST_INT(0x7fffffff, mm_scan_i32, int32_t, PRIx32);
	TEST_INT(0xffffffff, mm_scan_i32, int32_t, PRIx32);

	TEST_INT_EXT("100000000", UINT32_MAX, ERANGE, mm_scan_x32, uint32_t, PRIx32);
	TEST_INT_EXT("123456780", UINT32_MAX, ERANGE, mm_scan_x32, uint32_t, PRIx32);
	TEST_INT_EXT("fffffffff", UINT32_MAX, ERANGE, mm_scan_x32, uint32_t, PRIx32);
	TEST_INT_EXT("4294967296", UINT32_MAX, ERANGE, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT_EXT("4294967296", UINT32_MAX, ERANGE, mm_scan_n32, uint32_t, PRIu32);
	TEST_INT_EXT("5000000000", UINT32_MAX, ERANGE, mm_scan_u32, uint32_t, PRIu32);
	TEST_INT_EXT("5000000000", UINT32_MAX, ERANGE, mm_scan_n32, uint32_t, PRIu32);
	TEST_INT_EXT("0x100000000", UINT32_MAX, ERANGE, mm_scan_n32, uint32_t, PRIx32);

	TEST_INT_EXT("2147483648", INT32_MAX, ERANGE, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("2147483648", INT32_MAX, ERANGE, mm_scan_i32, int32_t, PRId32);
	TEST_INT_EXT("-2147483649", INT32_MIN, ERANGE, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("-2147483649", INT32_MIN, ERANGE, mm_scan_i32, int32_t, PRId32);

	TEST_INT_EXT("z", 0, EINVAL, mm_scan_u32, uint32_t, PRId32);
	TEST_INT_EXT("-", 0, EINVAL, mm_scan_u32, uint32_t, PRId32);
	TEST_INT_EXT("+", 0, EINVAL, mm_scan_u32, uint32_t, PRId32);

	TEST_INT_EXT("z", 0, EINVAL, mm_scan_x32, uint32_t, PRId32);
	TEST_INT_EXT("-", 0, EINVAL, mm_scan_x32, uint32_t, PRId32);
	TEST_INT_EXT("+", 0, EINVAL, mm_scan_x32, uint32_t, PRId32);

	TEST_INT_EXT("z", 0, EINVAL, mm_scan_n32, uint32_t, PRId32);
	TEST_INT_EXT("-", 0, EINVAL, mm_scan_n32, uint32_t, PRId32);
	TEST_INT_EXT("+", 0, EINVAL, mm_scan_n32, uint32_t, PRId32);

	TEST_INT_EXT("z", 0, EINVAL, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("-", 0, EINVAL, mm_scan_d32, int32_t, PRId32);
	TEST_INT_EXT("+", 0, EINVAL, mm_scan_d32, int32_t, PRId32);

	TEST_INT_EXT("z", 0, EINVAL, mm_scan_i32, int32_t, PRId32);
	TEST_INT_EXT("-", 0, EINVAL, mm_scan_i32, int32_t, PRId32);
	TEST_INT_EXT("+", 0, EINVAL, mm_scan_i32, int32_t, PRId32);

	TEST_INT_END("123abc", 123, 0, mm_scan_u32, uint32_t, PRId32, "abc");
	TEST_INT_END("123xyz", 123, 0, mm_scan_u32, uint32_t, PRId32, "xyz");
	TEST_INT_END("123xyz", 0x123, 0, mm_scan_x32, uint32_t, PRIx32, "xyz");
	TEST_INT_END("0x123xyz", 0x123, 0, mm_scan_n32, uint32_t, PRIx32, "xyz");
	TEST_INT_END("123abc", 123, 0, mm_scan_d32, int32_t, PRId32, "abc");
	TEST_INT_END("123xyz", 123, 0, mm_scan_d32, int32_t, PRId32, "xyz");
	TEST_INT_END("-123xyz", -123, 0, mm_scan_d32, int32_t, PRId32, "xyz");
	TEST_INT_END("123xyz", 123, 0, mm_scan_i32, int32_t, PRId32, "xyz");
	TEST_INT_END("-123xyz", -123, 0, mm_scan_i32, int32_t, PRId32, "xyz");
	TEST_INT_END("0x123xyz", 0x123, 0, mm_scan_i32, int32_t, PRIx32, "xyz");

	TEST_INT_END("0x", 0, 0, mm_scan_n32, uint32_t, PRIx32, "x");
	TEST_INT_END("0xy", 0, 0, mm_scan_n32, uint32_t, PRIx32, "xy");
	TEST_INT_END("0x", 0, 0, mm_scan_i32, int32_t, PRIx32, "x");
	TEST_INT_END("0xy", 0, 0, mm_scan_i32, int32_t, PRIx32, "xy");

	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
