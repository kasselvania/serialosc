#ifndef SERIALOSC_TEST_CHECK_H
#define SERIALOSC_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Unlike assert(), test checks (including setup with side effects) must run
 * even in Release builds with NDEBUG. Evaluate the expression exactly once. */
#define TEST_CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

#endif
