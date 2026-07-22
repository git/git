#include "unit-test.h"
#include "lib-reftable.h"
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
	cl_assert(!ret);
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
	ret = reftable_writer_set_limits(writer, min, max);
	cl_assert(!ret);

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
