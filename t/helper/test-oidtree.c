#include "test-tool.h"
#include "cache.h"
#include "hex.h"
#include "oidtree.h"
#include "setup.h"

static enum cb_next print_oid(const struct object_id *oid, void *data UNUSED)
{
	puts(oid_to_hex(oid));
	return CB_CONTINUE;
}

int cmd__oidtree(int argc UNUSED, const char **argv UNUSED)
{
	struct oidtree ot;
	struct strbuf line = STRBUF_INIT;
	int nongit_ok;
	int algo = GIT_HASH_UNKNOWN;

	oidtree_init(&ot);
	setup_git_directory_gently(&nongit_ok);

	while (strbuf_getline(&line, stdin) != EOF) {
		const char *arg;
		struct object_id oid;

		if (skip_prefix(line.buf, "insert ", &arg)) {
			if (get_oid_hex_any(arg, &oid) == GIT_HASH_UNKNOWN)
				die("insert not a hexadecimal oid: %s", arg);
			algo = oid.algo;
			oidtree_insert(&ot, &oid);
		} else if (skip_prefix(line.buf, "contains ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die("contains not a hexadecimal oid: %s", arg);
			printf("%d\n", oidtree_contains(&ot, &oid));
		} else if (skip_prefix(line.buf, "each ", &arg)) {
			char buf[GIT_MAX_HEXSZ + 1] = { '0' };
			memset(&oid, 0, sizeof(oid));
			memcpy(buf, arg, strlen(arg));
			buf[hash_algos[algo].hexsz] = '\0';
			get_oid_hex_any(buf, &oid);
			oid.algo = algo;
			oidtree_each(&ot, &oid, strlen(arg), print_oid, NULL);
		} else if (!strcmp(line.buf, "clear")) {
			oidtree_clear(&ot);
		} else {
			die("unknown command: %s", line.buf);
		}
	}

	strbuf_release(&line);

	return 0;
}
