#include "test-lib.h"
#include "lib-oid.h"
#include "strbuf.h"
#include "hex.h"

int init_hash_algo(void)
{
	static int algo = -1;

	if (algo < 0) {
		const char *algo_name = getenv("GIT_TEST_DEFAULT_HASH");
		algo = algo_name ? hash_algo_by_name(algo_name) : GIT_HASH_SHA1;

		if (!check(algo != GIT_HASH_UNKNOWN))
			test_msg("BUG: invalid GIT_TEST_DEFAULT_HASH value ('%s')",
				 algo_name);
	}
	return algo;
}

static int get_oid_arbitrary_hex_algop(const char *hex, struct object_id *oid,
				       const struct git_hash_algo *algop)
{
	int ret;
	size_t sz = strlen(hex);
	struct strbuf buf = STRBUF_INIT;

	if (!check(sz <= algop->hexsz)) {
		test_msg("BUG: hex string (%s) bigger than maximum allowed (%lu)",
			 hex, (unsigned long)algop->hexsz);
		return -1;
	}

	strbuf_add(&buf, hex, sz);
	strbuf_addchars(&buf, '0', algop->hexsz - sz);

	ret = get_oid_hex_algop(buf.buf, oid, algop);
	if (!check_int(ret, ==, 0))
		test_msg("BUG: invalid hex input (%s) provided", hex);

	strbuf_release(&buf);
	return ret;
}

int get_oid_arbitrary_hex(const char *hex, struct object_id *oid)
{
	int hash_algo = init_hash_algo();

	if (!check_int(hash_algo, !=, GIT_HASH_UNKNOWN))
		return -1;
	return get_oid_arbitrary_hex_algop(hex, oid, &hash_algos[hash_algo]);
}
