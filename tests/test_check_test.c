#include "test_check.h"

#include <string.h>

int
main(int argc, char **argv)
{
	int evaluations = 0;

	if (argc == 2 && strcmp(argv[1], "--fail") == 0)
		TEST_CHECK(++evaluations == 0);

	TEST_CHECK(argc == 1);
	TEST_CHECK(++evaluations == 1);
	TEST_CHECK(evaluations == 1);
	return 0;
}
