#include "git-compat-util.h"
#include "reftable/basics.h"
#include "reftable/blocksource.h"
#include "reftable/reftable-blocksource.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-iterator.h"
#include "reftable/reftable-record.h"
#include "reftable/reftable-table.h"
#include "reftable/reftable-writer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct reftable_block_source source = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_table *table = NULL;
	int err;

	if (reftable_buf_add(&buf, (const char *)data, size) < 0)
		goto out;
	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "fuzz-input");
	if (err < 0)
		goto out;

	/*
	 * Exercise the ref, log and raw block iterators so that we cover as
	 * much of the parsing code as possible.
	 */
	{
		struct reftable_ref_record ref = { 0 };
		struct reftable_iterator it = { 0 };

		reftable_table_init_ref_iterator(table, &it);
		if (!reftable_iterator_seek_ref(&it, ""))
			while (!reftable_iterator_next_ref(&it, &ref))
				;

		reftable_ref_record_release(&ref);
		reftable_iterator_destroy(&it);
	}

	{
		struct reftable_log_record log = { 0 };
		struct reftable_iterator it = { 0 };

		reftable_table_init_log_iterator(table, &it);
		if (!reftable_iterator_seek_log(&it, ""))
			while (!reftable_iterator_next_log(&it, &log))
				;

		reftable_log_record_release(&log);
		reftable_iterator_destroy(&it);
	}

	{
		struct reftable_table_iterator it = { 0 };
		const struct reftable_block *block;

		if (!reftable_table_iterator_init(&it, table))
			while (!reftable_table_iterator_next(&it, &block))
				;

		reftable_table_iterator_release(&it);
	}

out:
	if (table)
		reftable_table_decref(table);
	reftable_buf_release(&buf);
	return 0;
}
