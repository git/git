#include "cache.h"
#include "ewah/ewok.h"

int main(int argc, char **argv)
{
	do_read_index(&the_index, argv[1], 1);
	printf("last_update: %s\n", the_index.last_update ?
	       the_index.last_update : "(null)");

	/*
	 * For now, we just dump last_update, since it is not reasonable
	 * to populate the extension itself in tests.
	 */

	return 0;
}
