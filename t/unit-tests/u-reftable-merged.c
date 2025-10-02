/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/blocksource.h"
#include "reftable/constants.h"
#include "reftable/merged.h"
#include "reftable/table.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-merged.h"
#include "reftable/reftable-writer.h"

static struct reftable_merged_table *
merged_table_from_records(struct reftable_ref_record **refs,
			  struct reftable_block_source **source,
			  struct reftable_table ***tables, const size_t *sizes,
			  struct reftable_buf *buf, const size_t n)
{
	struct reftable_merged_table *mt = NULL;
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	int err;

	REFTABLE_CALLOC_ARRAY(*tables, n);
	cl_assert(*tables != NULL);
	REFTABLE_CALLOC_ARRAY(*source, n);
	cl_assert(*source != NULL);

	for (size_t i = 0; i < n; i++) {
		cl_reftable_write_to_buf(&buf[i], refs[i], sizes[i], NULL, 0, &opts);
		block_source_from_buf(&(*source)[i], &buf[i]);

		err = reftable_table_new(&(*tables)[i], &(*source)[i],
					 "name");
		cl_assert(!err);
	}

	err = reftable_merged_table_new(&mt, *tables, n, REFTABLE_HASH_SHA1);
	cl_assert(!err);
	return mt;
}

static void tables_destroy(struct reftable_table **tables, const size_t n)
{
	for (size_t i = 0; i < n; i++)
		reftable_table_decref(tables[i]);
	reftable_free(tables);
}

void test_reftable_merged__single_record(void)
{
	struct reftable_ref_record r1[] = { {
		.refname = (char *) "b",
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = { 1, 2, 3, 0 },
	} };
	struct reftable_ref_record r2[] = { {
		.refname = (char *) "a",
		.update_index = 2,
		.value_type = REFTABLE_REF_DELETION,
	} };
	struct reftable_ref_record r3[] = { {
		.refname = (char *) "c",
		.update_index = 3,
		.value_type = REFTABLE_REF_DELETION,
	} };

	struct reftable_ref_record *refs[] = { r1, r2, r3 };
	size_t sizes[] = { ARRAY_SIZE(r1), ARRAY_SIZE(r2), ARRAY_SIZE(r3) };
	struct reftable_buf bufs[3] = { REFTABLE_BUF_INIT, REFTABLE_BUF_INIT, REFTABLE_BUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_table **tables = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &tables, sizes, bufs, 3);
	struct reftable_ref_record ref = { 0 };
	struct reftable_iterator it = { 0 };
	int err;

	err = merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_REF);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "a");
	cl_assert(!err);

	err = reftable_iterator_next_ref(&it, &ref);
	cl_assert(!err);
	cl_assert(reftable_ref_record_equal(&r2[0], &ref,
					    REFTABLE_HASH_SIZE_SHA1) != 0);
	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	tables_destroy(tables, 3);
	reftable_merged_table_free(mt);
	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++)
		reftable_buf_release(&bufs[i]);
	reftable_free(bs);
}

void test_reftable_merged__refs(void)
{
	struct reftable_ref_record r1[] = {
		{
			.refname = (char *) "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = (char *) "b",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = (char *) "c",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		}
	};
	struct reftable_ref_record r2[] = { {
		.refname = (char *) "a",
		.update_index = 2,
		.value_type = REFTABLE_REF_DELETION,
	} };
	struct reftable_ref_record r3[] = {
		{
			.refname = (char *) "c",
			.update_index = 3,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 2 },
		},
		{
			.refname = (char *) "d",
			.update_index = 3,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
	};

	struct reftable_ref_record *want[] = {
		&r2[0],
		&r1[1],
		&r3[0],
		&r3[1],
	};

	struct reftable_ref_record *refs[] = { r1, r2, r3 };
	size_t sizes[3] = { ARRAY_SIZE(r1), ARRAY_SIZE(r2), ARRAY_SIZE(r3) };
	struct reftable_buf bufs[3] = { REFTABLE_BUF_INIT, REFTABLE_BUF_INIT, REFTABLE_BUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_table **tables = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &tables, sizes, bufs, 3);
	struct reftable_iterator it = { 0 };
	int err;
	struct reftable_ref_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	size_t i;

	err = merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_REF);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "a");
	cl_assert(err == 0);
	cl_assert_equal_i(reftable_merged_table_hash_id(mt), REFTABLE_HASH_SHA1);
	cl_assert_equal_i(reftable_merged_table_min_update_index(mt), 1);
	cl_assert_equal_i(reftable_merged_table_max_update_index(mt), 3);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_ref_record ref = { 0 };
		int err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0)
			break;

		cl_assert(REFTABLE_ALLOC_GROW(out, len + 1, cap) == 0);
		out[len++] = ref;
	}
	reftable_iterator_destroy(&it);

	cl_assert_equal_i(ARRAY_SIZE(want), len);
	for (i = 0; i < len; i++)
		cl_assert(reftable_ref_record_equal(want[i], &out[i],
						    REFTABLE_HASH_SIZE_SHA1) != 0);
	for (i = 0; i < len; i++)
		reftable_ref_record_release(&out[i]);
	reftable_free(out);

	for (i = 0; i < 3; i++)
		reftable_buf_release(&bufs[i]);
	tables_destroy(tables, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

void test_reftable_merged__seek_multiple_times(void)
{
	struct reftable_ref_record r1[] = {
		{
			.refname = (char *) "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = (char *) "c",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 2 },
		}
	};
	struct reftable_ref_record r2[] = {
		{
			.refname = (char *) "b",
			.update_index = 2,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 3 },
		},
		{
			.refname = (char *) "d",
			.update_index = 2,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 4 },
		},
	};
	struct reftable_ref_record *refs[] = {
		r1, r2,
	};
	size_t sizes[] = {
		ARRAY_SIZE(r1), ARRAY_SIZE(r2),
	};
	struct reftable_buf bufs[] = {
		REFTABLE_BUF_INIT, REFTABLE_BUF_INIT,
	};
	struct reftable_block_source *sources = NULL;
	struct reftable_table **tables = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_merged_table *mt;

	mt = merged_table_from_records(refs, &sources, &tables, sizes, bufs, 2);
	merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_REF);

	for (size_t i = 0; i < 5; i++) {
		int err = reftable_iterator_seek_ref(&it, "c");
		cl_assert(!err);

		cl_assert(reftable_iterator_next_ref(&it, &rec) == 0);
		cl_assert_equal_i(reftable_ref_record_equal(&rec, &r1[1],
							    REFTABLE_HASH_SIZE_SHA1), 1);

		cl_assert(reftable_iterator_next_ref(&it, &rec) == 0);
		cl_assert_equal_i(reftable_ref_record_equal(&rec, &r2[1],
							    REFTABLE_HASH_SIZE_SHA1), 1);

		cl_assert(reftable_iterator_next_ref(&it, &rec) > 0);
	}

	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++)
		reftable_buf_release(&bufs[i]);
	tables_destroy(tables, ARRAY_SIZE(refs));
	reftable_ref_record_release(&rec);
	reftable_iterator_destroy(&it);
	reftable_merged_table_free(mt);
	reftable_free(sources);
}

void test_reftable_merged__seek_multiple_times_no_drain(void)
{
	struct reftable_ref_record r1[] = {
		{
			.refname = (char *) "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = (char *) "c",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 2 },
		}
	};
	struct reftable_ref_record r2[] = {
		{
			.refname = (char *) "b",
			.update_index = 2,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 3 },
		},
		{
			.refname = (char *) "d",
			.update_index = 2,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 4 },
		},
	};
	struct reftable_ref_record *refs[] = {
		r1, r2,
	};
	size_t sizes[] = {
		ARRAY_SIZE(r1), ARRAY_SIZE(r2),
	};
	struct reftable_buf bufs[] = {
		REFTABLE_BUF_INIT, REFTABLE_BUF_INIT,
	};
	struct reftable_block_source *sources = NULL;
	struct reftable_table **tables = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_merged_table *mt;

	mt = merged_table_from_records(refs, &sources, &tables, sizes, bufs, 2);
	merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_REF);

	cl_assert(reftable_iterator_seek_ref(&it, "b") == 0);
	cl_assert(reftable_iterator_next_ref(&it, &rec) == 0);
	cl_assert_equal_i(reftable_ref_record_equal(&rec, &r2[0],
						    REFTABLE_HASH_SIZE_SHA1), 1);

	cl_assert(reftable_iterator_seek_ref(&it, "a") == 0);
	cl_assert(reftable_iterator_next_ref(&it, &rec) == 0);
	cl_assert_equal_i(reftable_ref_record_equal(&rec, &r1[0],
						    REFTABLE_HASH_SIZE_SHA1), 1);

	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++)
		reftable_buf_release(&bufs[i]);
	tables_destroy(tables, ARRAY_SIZE(refs));
	reftable_ref_record_release(&rec);
	reftable_iterator_destroy(&it);
	reftable_merged_table_free(mt);
	reftable_free(sources);
}

static struct reftable_merged_table *
merged_table_from_log_records(struct reftable_log_record **logs,
			      struct reftable_block_source **source,
			      struct reftable_table ***tables, const size_t *sizes,
			      struct reftable_buf *buf, const size_t n)
{
	struct reftable_merged_table *mt = NULL;
	struct reftable_write_options opts = {
		.block_size = 256,
		.exact_log_message = 1,
	};
	int err;

	REFTABLE_CALLOC_ARRAY(*tables, n);
	cl_assert(*tables != NULL);
	REFTABLE_CALLOC_ARRAY(*source, n);
	cl_assert(*source != NULL);

	for (size_t i = 0; i < n; i++) {
		cl_reftable_write_to_buf(&buf[i], NULL, 0, logs[i], sizes[i], &opts);
		block_source_from_buf(&(*source)[i], &buf[i]);

		err = reftable_table_new(&(*tables)[i], &(*source)[i],
					 "name");
		cl_assert(!err);
	}

	err = reftable_merged_table_new(&mt, *tables, n, REFTABLE_HASH_SHA1);
	cl_assert(!err);
	return mt;
}

void test_reftable_merged__logs(void)
{
	struct reftable_log_record r1[] = {
		{
			.refname = (char *) "a",
			.update_index = 2,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = { 2 },
				/* deletion */
				.name = (char *) "jane doe",
				.email = (char *) "jane@invalid",
				.message = (char *) "message2",
			}
		},
		{
			.refname = (char *) "a",
			.update_index = 1,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = { 1 },
				.new_hash = { 2 },
				.name = (char *) "jane doe",
				.email = (char *) "jane@invalid",
				.message = (char *) "message1",
			}
		},
	};
	struct reftable_log_record r2[] = {
		{
			.refname = (char *) "a",
			.update_index = 3,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.new_hash = { 3 },
				.name = (char *) "jane doe",
				.email = (char *) "jane@invalid",
				.message = (char *) "message3",
			}
		},
	};
	struct reftable_log_record r3[] = {
		{
			.refname = (char *) "a",
			.update_index = 2,
			.value_type = REFTABLE_LOG_DELETION,
		},
	};
	struct reftable_log_record *want[] = {
		&r2[0],
		&r3[0],
		&r1[1],
	};

	struct reftable_log_record *logs[] = { r1, r2, r3 };
	size_t sizes[3] = { ARRAY_SIZE(r1), ARRAY_SIZE(r2), ARRAY_SIZE(r3) };
	struct reftable_buf bufs[3] = { REFTABLE_BUF_INIT, REFTABLE_BUF_INIT, REFTABLE_BUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_table **tables = NULL;
	struct reftable_merged_table *mt = merged_table_from_log_records(
		logs, &bs, &tables, sizes, bufs, 3);
	struct reftable_iterator it = { 0 };
	struct reftable_log_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	size_t i;
	int err;

	err = merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_LOG);
	cl_assert(!err);
	err = reftable_iterator_seek_log(&it, "a");
	cl_assert(!err);
	cl_assert_equal_i(reftable_merged_table_hash_id(mt), REFTABLE_HASH_SHA1);
	cl_assert_equal_i(reftable_merged_table_min_update_index(mt), 1);
	cl_assert_equal_i(reftable_merged_table_max_update_index(mt), 3);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_log_record log = { 0 };
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;

		cl_assert(REFTABLE_ALLOC_GROW(out, len + 1, cap) == 0);
		out[len++] = log;
	}
	reftable_iterator_destroy(&it);

	cl_assert_equal_i(ARRAY_SIZE(want), len);
	for (i = 0; i < len; i++)
		cl_assert(reftable_log_record_equal(want[i], &out[i],
						    REFTABLE_HASH_SIZE_SHA1) != 0);

	err = merged_table_init_iter(mt, &it, REFTABLE_BLOCK_TYPE_LOG);
	cl_assert(!err);
	err = reftable_iterator_seek_log_at(&it, "a", 2);
	cl_assert(!err);
	reftable_log_record_release(&out[0]);
	cl_assert(reftable_iterator_next_log(&it, &out[0]) == 0);
	cl_assert(reftable_log_record_equal(&out[0], &r3[0],
					    REFTABLE_HASH_SIZE_SHA1) != 0);
	reftable_iterator_destroy(&it);

	for (i = 0; i < len; i++)
		reftable_log_record_release(&out[i]);
	reftable_free(out);

	for (i = 0; i < 3; i++)
		reftable_buf_release(&bufs[i]);
	tables_destroy(tables, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

void test_reftable_merged__default_write_opts(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record rec = {
		.refname = (char *) "master",
		.update_index = 1,
	};
	int err;
	struct reftable_block_source source = { 0 };
	uint32_t hash_id;
	struct reftable_table *table = NULL;
	struct reftable_merged_table *merged = NULL;

	reftable_writer_set_limits(w, 1, 1);

	cl_assert_equal_i(reftable_writer_add_ref(w, &rec), 0);

	cl_assert_equal_i(reftable_writer_close(w), 0);
	reftable_writer_free(w);

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "filename");
	cl_assert(!err);

	hash_id = reftable_table_hash_id(table);
	cl_assert_equal_i(hash_id, REFTABLE_HASH_SHA1);

	err = reftable_merged_table_new(&merged, &table, 1, REFTABLE_HASH_SHA256);
	cl_assert_equal_i(err, REFTABLE_FORMAT_ERROR);
	err = reftable_merged_table_new(&merged, &table, 1, REFTABLE_HASH_SHA1);
	cl_assert(!err);

	reftable_table_decref(table);
	reftable_merged_table_free(merged);
	reftable_buf_release(&buf);
}
