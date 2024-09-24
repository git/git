/*
 * test-name-hash.c: Read a list of paths over stdin and report on their
 * name-hash and full name-hash.
 */

#include "test-tool.h"
#include "git-compat-util.h"
#include "pack-objects.h"
#include "strbuf.h"

int cmd__name_hash(int argc UNUSED, const char **argv UNUSED)
{
	struct strbuf line = STRBUF_INIT;

	while (!strbuf_getline(&line, stdin)) {
		uint32_t name_hash = pack_name_hash(line.buf);
		uint32_t full_hash = pack_full_name_hash(line.buf);

		printf("%10"PRIu32"\t%10"PRIu32"\t%s\n", name_hash, full_hash, line.buf);
	}

	strbuf_release(&line);
	return 0;
}
