#include "unit-test.h"
#include "lib-oid.h"
#include "strbuf.h"
#include "hex.h"

int cl_setup_hash_algo(void)
{
	static int algo = -1;

	if (algo < 0) {
		const char *algo_name = getenv("GIT_TEST_DEFAULT_HASH");
		algo = algo_name ? hash_algo_by_name(algo_name) : GIT_HASH_SHA1;

		cl_assert(algo != GIT_HASH_UNKNOWN);
	}
	return algo;
}

static void cl_parse_oid(const char *hex, struct object_id *oid,
				       const struct git_hash_algo *algop)
{
	size_t sz = strlen(hex);
	struct strbuf buf = STRBUF_INIT;

	cl_assert(sz <= algop->hexsz);

	strbuf_add(&buf, hex, sz);
	strbuf_addchars(&buf, '0', algop->hexsz - sz);

	cl_assert_equal_i(get_oid_hex_algop(buf.buf, oid, algop), 0);

	strbuf_release(&buf);
}


void cl_parse_any_oid(const char *hex, struct object_id *oid)
{
	int hash_algo = cl_setup_hash_algo();

	cl_assert(hash_algo != GIT_HASH_UNKNOWN);
	cl_parse_oid(hex, oid, &hash_algos[hash_algo]);
}
