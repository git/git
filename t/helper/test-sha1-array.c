#include "cache.h"
#include "sha1-array.h"

static int print_sha1(const unsigned char sha1[20], void *data)
{
	puts(sha1_to_hex(sha1));
	return 0;
}

int cmd_main(int argc, const char **argv)
{
	struct sha1_array array = SHA1_ARRAY_INIT;
	struct strbuf line = STRBUF_INIT;

	while (strbuf_getline(&line, stdin) != EOF) {
		const char *arg;
		struct object_id oid;

		if (skip_prefix(line.buf, "append ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die("not a hexadecimal SHA1: %s", arg);
			sha1_array_append(&array, &oid);
		} else if (skip_prefix(line.buf, "lookup ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die("not a hexadecimal SHA1: %s", arg);
			printf("%d\n", sha1_array_lookup(&array, oid.hash));
		} else if (!strcmp(line.buf, "clear"))
			sha1_array_clear(&array);
		else if (!strcmp(line.buf, "for_each_unique"))
			sha1_array_for_each_unique(&array, print_sha1, NULL);
		else
			die("unknown command: %s", line.buf);
	}
	return 0;
}
