#define USE_THE_REPOSITORY_VARIABLE
#include "test-tool.h"
#include "read-cache-ll.h"

/*
 * Exercise the cache-entry name-length guard without having to construct a
 * multi-gigabyte pathname in memory.  make_empty_cache_entry()
 * dies on any length that would not survive being stored in the 32-bit
 * "ce_namelen" field, and it does so before allocating, so an out-of-range
 * length is cheap to test.
 */
int cmd__cache_entry(int argc, const char **argv)
{
	size_t len;
	char *end;
	struct cache_entry *ce;

	if (argc != 2)
		die("usage: test-tool cache-entry <namelen>");

	errno = 0;
	len = strtoumax(argv[1], &end, 10);
	if (errno || *end)
		die("cannot parse namelen '%s'", argv[1]);

	/* Dies here if 'len' is too large to be represented safely. */
	ce = make_empty_cache_entry(the_repository->index, len);
	if (!ce)
		die("failed to create cache entry");

	printf("ok: allocated cache entry for namelen %"PRIuMAX"\n",
	       (uintmax_t)len);
	discard_cache_entry(ce);
	return 0;
}
