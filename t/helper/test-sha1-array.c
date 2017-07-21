#include "cache.h"
#include "sha1-array.h"

static int print_oid(const struct object_id *oid, void *data)
{
	puts(oid_to_hex(oid));
	return 0;
}

int cmd_main(int argc, const char **argv)
{
	struct oid_array array = OID_ARRAY_INIT;
	struct strbuf line = STRBUF_INIT;

	while (strbuf_getline(&line, stdin) != EOF) {
		const char *arg;
		struct object_id oid;

		if (skip_prefix(line.buf, "append ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die("not a hexadecimal SHA1: %s", arg);
			oid_array_append(&array, &oid);
		} else if (skip_prefix(line.buf, "lookup ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die("not a hexadecimal SHA1: %s", arg);
			printf("%d\n", oid_array_lookup(&array, &oid));
		} else if (!strcmp(line.buf, "clear"))
			oid_array_clear(&array);
		else if (!strcmp(line.buf, "for_each_unique"))
			oid_array_for_each_unique(&array, print_oid, NULL);
		else
			die("unknown command: %s", line.buf);
	}
	return 0;
}
