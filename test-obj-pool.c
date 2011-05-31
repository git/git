/*
 * test-obj-pool.c: code to exercise the svn importer's object pool
 */

#include "cache.h"
#include "vcs-svn/obj_pool.h"

enum pool { POOL_ONE, POOL_TWO };
obj_pool_gen(one, int, 1)
obj_pool_gen(two, int, 4096)

static uint32_t strtouint32(const char *s)
{
	char *end;
	uintmax_t n = strtoumax(s, &end, 10);
	if (*s == '\0' || (*end != '\n' && *end != '\0'))
		die("invalid offset: %s", s);
	return (uint32_t) n;
}

static void handle_command(const char *command, enum pool pool, const char *arg)
{
	switch (*command) {
	case 'a':
		if (!prefixcmp(command, "alloc ")) {
			uint32_t n = strtouint32(arg);
			printf("%"PRIu32"\n",
				pool == POOL_ONE ?
				one_alloc(n) : two_alloc(n));
			return;
		}
	case 'c':
		if (!prefixcmp(command, "commit ")) {
			pool == POOL_ONE ? one_commit() : two_commit();
			return;
		}
		if (!prefixcmp(command, "committed ")) {
			printf("%"PRIu32"\n",
				pool == POOL_ONE ?
				one_pool.committed : two_pool.committed);
			return;
		}
	case 'f':
		if (!prefixcmp(command, "free ")) {
			uint32_t n = strtouint32(arg);
			pool == POOL_ONE ? one_free(n) : two_free(n);
			return;
		}
	case 'n':
		if (!prefixcmp(command, "null ")) {
			printf("%"PRIu32"\n",
				pool == POOL_ONE ?
				one_offset(NULL) : two_offset(NULL));
			return;
		}
	case 'o':
		if (!prefixcmp(command, "offset ")) {
			uint32_t n = strtouint32(arg);
			printf("%"PRIu32"\n",
				pool == POOL_ONE ?
				one_offset(one_pointer(n)) :
				two_offset(two_pointer(n)));
			return;
		}
	case 'r':
		if (!prefixcmp(command, "reset ")) {
			pool == POOL_ONE ? one_reset() : two_reset();
			return;
		}
	case 's':
		if (!prefixcmp(command, "set ")) {
			uint32_t n = strtouint32(arg);
			if (pool == POOL_ONE)
				*one_pointer(n) = 1;
			else
				*two_pointer(n) = 1;
			return;
		}
	case 't':
		if (!prefixcmp(command, "test ")) {
			uint32_t n = strtouint32(arg);
			printf("%d\n", pool == POOL_ONE ?
				*one_pointer(n) : *two_pointer(n));
			return;
		}
	default:
		die("unrecognized command: %s", command);
	}
}

static void handle_line(const char *line)
{
	const char *arg = strchr(line, ' ');
	enum pool pool;

	if (arg && !prefixcmp(arg + 1, "one"))
		pool = POOL_ONE;
	else if (arg && !prefixcmp(arg + 1, "two"))
		pool = POOL_TWO;
	else
		die("no pool specified: %s", line);

	handle_command(line, pool, arg + strlen("one "));
}

int main(int argc, char *argv[])
{
	struct strbuf sb = STRBUF_INIT;
	if (argc != 1)
		usage("test-obj-str < script");

	while (strbuf_getline(&sb, stdin, '\n') != EOF)
		handle_line(sb.buf);
	strbuf_release(&sb);
	return 0;
}
