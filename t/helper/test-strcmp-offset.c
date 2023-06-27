#include "test-tool.h"
#include "read-cache-ll.h"

int cmd__strcmp_offset(int argc UNUSED, const char **argv)
{
	int result;
	size_t offset;

	if (!argv[1] || !argv[2])
		die("usage: %s <string1> <string2>", argv[0]);

	result = strcmp_offset(argv[1], argv[2], &offset);

	/*
	 * Because different CRTs behave differently, only rely on signs
	 * of the result values.
	 */
	result = (result < 0 ? -1 :
			  result > 0 ? 1 :
			  0);
	printf("%d %"PRIuMAX"\n", result, (uintmax_t)offset);
	return 0;
}
