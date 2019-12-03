#include <stdio.h>

#define MAX (64 * 1024)

size_t factors[] = {
	2, 4, 6, 8, 10,
	12, 14, 16, 18,
	20, 22, 24, 26,
	28, 30, 32
};

size_t n_factors = sizeof factors / sizeof factors[0];

int
main()
{
	for (size_t i = 0; i < n_factors; i++)
	{
		int newline = 0;
		size_t size = 16;
		size_t delta = 16;
		size_t factor = factors[i];
		size_t n = 0, x[7] = { 0, 0, 0, 0 };
		printf("--- %d:\n", factor);
		do {
			if (n++) {
				if (newline) {
					newline = 0;
					printf(",\n");
				} else {
					printf(", ");
				}
			}
			printf("%u", size);

			if (size == 1024)
				x[0] = n;
			else if (size == 2 * 1024)
				x[1] = n;
			else if (size == 4 * 1024)
				x[2] = n;
			else if (size == 8 * 1024)
				x[3] = n;
			else if (size == 16 * 1024)
				x[4] = n;
			else if (size == 32 * 1024)
				x[5] = n;
			else if (size == 64 * 1024)
				x[6] = n;

			if (size >= (delta * factor)) {
				delta += delta;
				newline = 1;
			}
			size += delta;

		} while (size <= MAX);

		printf("\n::: %u/%u %u/%u %u/%u %u/%u %u/%u %u/%u %u/%u\n",
		       1024, x[0],
		       2 * 1024, x[1],
		       4 * 1024, x[2],
		       8 * 1024, x[3],
		       16 * 1024, x[4],
		       32 * 1024, x[5],
		       64 * 1024, x[6]);
	}
	return 0;
}
