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
		printf("%10u ", pack_name_hash(line.buf));
		printf("%10u ", pack_name_hash_v2((unsigned const char *)line.buf));
		printf("%s\n", line.buf);
	}

	strbuf_release(&line);
	return 0;
}
