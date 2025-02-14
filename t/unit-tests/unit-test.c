#include "unit-test.h"
#include "hex.h"
#include "parse-options.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"

static const char * const unit_test_usage[] = {
	N_("unit-test [<options>]"),
	NULL,
};

int cmd_main(int argc, const char **argv)
{
	struct string_list run_args = STRING_LIST_INIT_NODUP;
	struct string_list exclude_args = STRING_LIST_INIT_NODUP;
	int immediate = 0;
	struct option options[] = {
		OPT_BOOL('i', "immediate", &immediate,
			 N_("immediately exit upon the first failed test")),
		OPT_STRING_LIST('r', "run", &run_args, N_("suite[::test]"),
				N_("run only test suite or individual test <suite[::test]>")),
		OPT_STRING_LIST(0, "exclude", &exclude_args, N_("suite"),
				N_("exclude test suite <suite>")),
		/*
		 * Compatibility wrappers so that we don't have to filter
		 * options understood by integration tests.
		 */
		OPT_NOOP_NOARG('d', "debug"),
		OPT_NOOP_NOARG(0, "github-workflow-markup"),
		OPT_NOOP_NOARG(0, "no-bin-wrappers"),
		OPT_NOOP_ARG(0, "root"),
		OPT_NOOP_ARG(0, "stress"),
		OPT_NOOP_NOARG(0, "tee"),
		OPT_NOOP_NOARG(0, "with-dashes"),
		OPT_NOOP_ARG(0, "valgrind"),
		OPT_NOOP_ARG(0, "valgrind-only"),
		OPT_NOOP_NOARG('v', "verbose"),
		OPT_NOOP_NOARG('V', "verbose-log"),
		OPT_NOOP_ARG(0, "verbose-only"),
		OPT_NOOP_NOARG('x', NULL),
		OPT_END(),
	};
	struct strvec args = STRVEC_INIT;
	int ret;

	argc = parse_options(argc, argv, NULL, options,
			     unit_test_usage, PARSE_OPT_KEEP_ARGV0);
	if (argc > 1)
		usagef(_("extra command line parameter '%s'"), argv[0]);

	strvec_push(&args, argv[0]);
	strvec_push(&args, "-t");
	if (immediate)
		strvec_push(&args, "-Q");
	for (size_t i = 0; i < run_args.nr; i++)
		strvec_pushf(&args, "-s%s", run_args.items[i].string);
	for (size_t i = 0; i < exclude_args.nr; i++)
		strvec_pushf(&args, "-x%s", exclude_args.items[i].string);

	ret = clar_test(args.nr, (char **) args.v);

	string_list_clear(&run_args, 0);
	strvec_clear(&args);
	return ret;
}

int init_hash_algo(void)
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
	int ret;
	size_t sz = strlen(hex);
	struct strbuf buf = STRBUF_INIT;

	cl_assert(sz <= algop->hexsz);

	strbuf_add(&buf, hex, sz);
	strbuf_addchars(&buf, '0', algop->hexsz - sz);

	ret = get_oid_hex_algop(buf.buf, oid, algop);
	cl_assert_equal_i(ret, 0);

	strbuf_release(&buf);
}


void cl_parse_any_oid(const char *hex, struct object_id *oid)
{
	int hash_algo = init_hash_algo();

	cl_assert(hash_algo != GIT_HASH_UNKNOWN);
	cl_parse_oid(hex, oid, &hash_algos[hash_algo]);
}
