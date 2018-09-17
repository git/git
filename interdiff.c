#include "cache.h"
#include "commit.h"
#include "revision.h"
#include "interdiff.h"

static struct strbuf *idiff_prefix_cb(struct diff_options *opt, void *data)
{
	return data;
}

void show_interdiff(struct rev_info *rev, int indent)
{
	struct diff_options opts;
	struct strbuf prefix = STRBUF_INIT;

	memcpy(&opts, &rev->diffopt, sizeof(opts));
	opts.output_format = DIFF_FORMAT_PATCH;
	opts.output_prefix = idiff_prefix_cb;
	strbuf_addchars(&prefix, ' ', indent);
	opts.output_prefix_data = &prefix;
	diff_setup_done(&opts);

	diff_tree_oid(rev->idiff_oid1, rev->idiff_oid2, "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);

	strbuf_release(&prefix);
}
