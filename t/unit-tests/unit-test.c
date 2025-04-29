#include "unit-test.h"
#include "hex.h"
#include "parse-options.h"
#include "reftable/constants.h"
#include "reftable/writer.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"

void cl_reftable_set_hash(uint8_t *p, int i, enum reftable_hash id)
{
	memset(p, (uint8_t)i, hash_size(id));
}

static ssize_t strbuf_writer_write(void *b, const void *data, size_t sz)
{
	strbuf_add(b, data, sz);
	return sz;
}

static int strbuf_writer_flush(void *arg UNUSED)
{
	return 0;
}

struct reftable_writer *cl_reftable_strbuf_writer(struct reftable_buf *buf,
						 struct reftable_write_options *opts)
{
	struct reftable_writer *writer;
	int ret = reftable_writer_new(&writer, &strbuf_writer_write, &strbuf_writer_flush,
				      buf, opts);
	cl_assert(ret == 0);
	return writer;
}

void cl_reftable_write_to_buf(struct reftable_buf *buf,
			     struct reftable_ref_record *refs,
			     size_t nrefs,
			     struct reftable_log_record *logs,
			     size_t nlogs,
			     struct reftable_write_options *_opts)
{
	struct reftable_write_options opts = { 0 };
	const struct reftable_stats *stats;
	struct reftable_writer *writer;
	uint64_t min = 0xffffffff;
	uint64_t max = 0;
	int ret;

	if (_opts)
		opts = *_opts;

	for (size_t i = 0; i < nrefs; i++) {
		uint64_t ui = refs[i].update_index;
		if (ui > max)
			max = ui;
		if (ui < min)
			min = ui;
	}
	for (size_t i = 0; i < nlogs; i++) {
		uint64_t ui = logs[i].update_index;
		if (ui > max)
			max = ui;
		if (ui < min)
			min = ui;
	}

	writer = cl_reftable_strbuf_writer(buf, &opts);
	reftable_writer_set_limits(writer, min, max);

	if (nrefs) {
		ret = reftable_writer_add_refs(writer, refs, nrefs);
		cl_assert_equal_i(ret, 0);
	}

	if (nlogs) {
		ret = reftable_writer_add_logs(writer, logs, nlogs);
		cl_assert_equal_i(ret, 0);
	}

	ret = reftable_writer_close(writer);
	cl_assert_equal_i(ret, 0);

	stats = reftable_writer_stats(writer);
	for (size_t i = 0; i < (size_t)stats->ref_stats.blocks; i++) {
		size_t off = i * (opts.block_size ? opts.block_size
						  : DEFAULT_BLOCK_SIZE);
		if (!off)
			off = header_size(opts.hash_id == REFTABLE_HASH_SHA256 ? 2 : 1);
		cl_assert(buf->buf[off] == 'r');
	}

	if (nrefs)
		cl_assert(stats->ref_stats.blocks > 0);
	if (nlogs)
		cl_assert(stats->log_stats.blocks > 0);

	reftable_writer_free(writer);
}

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
