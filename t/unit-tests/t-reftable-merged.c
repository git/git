/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "lib-reftable.h"
#include "reftable/blocksource.h"
#include "reftable/constants.h"
#include "reftable/merged.h"
#include "reftable/reader.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-merged.h"
#include "reftable/reftable-writer.h"

static struct reftable_merged_table *
merged_table_from_records(struct reftable_ref_record **refs,
			  struct reftable_block_source **source,
			  struct reftable_reader ***readers, const size_t *sizes,
			  struct strbuf *buf, const size_t n)
{
	struct reftable_merged_table *mt = NULL;
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	int err;

	REFTABLE_CALLOC_ARRAY(*readers, n);
	check(*readers != NULL);
	REFTABLE_CALLOC_ARRAY(*source, n);
	check(*source != NULL);

	for (size_t i = 0; i < n; i++) {
		t_reftable_write_to_buf(&buf[i], refs[i], sizes[i], NULL, 0, &opts);
		block_source_from_strbuf(&(*source)[i], &buf[i]);

		err = reftable_reader_new(&(*readers)[i], &(*source)[i],
					  "name");
		check(!err);
	}

	err = reftable_merged_table_new(&mt, *readers, n, GIT_SHA1_FORMAT_ID);
	check(!err);
	return mt;
}

static void readers_destroy(struct reftable_reader **readers, const size_t n)
{
	for (size_t i = 0; i < n; i++)
		reftable_reader_decref(readers[i]);
	reftable_free(readers);
}

static void t_merged_single_record(void)
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
	struct strbuf bufs[3] = { STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &readers, sizes, bufs, 3);
	struct reftable_ref_record ref = { 0 };
	struct reftable_iterator it = { 0 };
	int err;

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_REF);
	check(!err);
	err = reftable_iterator_seek_ref(&it, "a");
	check(!err);

	err = reftable_iterator_next_ref(&it, &ref);
	check(!err);
	check(reftable_ref_record_equal(&r2[0], &ref, GIT_SHA1_RAWSZ));
	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	readers_destroy(readers, 3);
	reftable_merged_table_free(mt);
	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++)
		strbuf_release(&bufs[i]);
	reftable_free(bs);
}

static void t_merged_refs(void)
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
	struct strbuf bufs[3] = { STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &readers, sizes, bufs, 3);
	struct reftable_iterator it = { 0 };
	int err;
	struct reftable_ref_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	size_t i;

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_REF);
	check(!err);
	err = reftable_iterator_seek_ref(&it, "a");
	check(!err);
	check_int(reftable_merged_table_hash_id(mt), ==, GIT_SHA1_FORMAT_ID);
	check_int(reftable_merged_table_min_update_index(mt), ==, 1);
	check_int(reftable_merged_table_max_update_index(mt), ==, 3);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_ref_record ref = { 0 };
		int err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0)
			break;

		REFTABLE_ALLOC_GROW(out, len + 1, cap);
		out[len++] = ref;
	}
	reftable_iterator_destroy(&it);

	check_int(ARRAY_SIZE(want), ==, len);
	for (i = 0; i < len; i++)
		check(reftable_ref_record_equal(want[i], &out[i],
						 GIT_SHA1_RAWSZ));
	for (i = 0; i < len; i++)
		reftable_ref_record_release(&out[i]);
	reftable_free(out);

	for (i = 0; i < 3; i++)
		strbuf_release(&bufs[i]);
	readers_destroy(readers, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

static void t_merged_seek_multiple_times(void)
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
	struct strbuf bufs[] = {
		STRBUF_INIT, STRBUF_INIT,
	};
	struct reftable_block_source *sources = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_merged_table *mt;

	mt = merged_table_from_records(refs, &sources, &readers, sizes, bufs, 2);
	merged_table_init_iter(mt, &it, BLOCK_TYPE_REF);

	for (size_t i = 0; i < 5; i++) {
		int err = reftable_iterator_seek_ref(&it, "c");
		check(!err);

		err = reftable_iterator_next_ref(&it, &rec);
		check(!err);
		err = reftable_ref_record_equal(&rec, &r1[1], GIT_SHA1_RAWSZ);
		check(err == 1);

		err = reftable_iterator_next_ref(&it, &rec);
		check(!err);
		err = reftable_ref_record_equal(&rec, &r2[1], GIT_SHA1_RAWSZ);
		check(err == 1);

		err = reftable_iterator_next_ref(&it, &rec);
		check(err > 0);
	}

	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++)
		strbuf_release(&bufs[i]);
	readers_destroy(readers, ARRAY_SIZE(refs));
	reftable_ref_record_release(&rec);
	reftable_iterator_destroy(&it);
	reftable_merged_table_free(mt);
	reftable_free(sources);
}

static struct reftable_merged_table *
merged_table_from_log_records(struct reftable_log_record **logs,
			      struct reftable_block_source **source,
			      struct reftable_reader ***readers, const size_t *sizes,
			      struct strbuf *buf, const size_t n)
{
	struct reftable_merged_table *mt = NULL;
	struct reftable_write_options opts = {
		.block_size = 256,
		.exact_log_message = 1,
	};
	int err;

	REFTABLE_CALLOC_ARRAY(*readers, n);
	check(*readers != NULL);
	REFTABLE_CALLOC_ARRAY(*source, n);
	check(*source != NULL);

	for (size_t i = 0; i < n; i++) {
		t_reftable_write_to_buf(&buf[i], NULL, 0, logs[i], sizes[i], &opts);
		block_source_from_strbuf(&(*source)[i], &buf[i]);

		err = reftable_reader_new(&(*readers)[i], &(*source)[i],
					  "name");
		check(!err);
	}

	err = reftable_merged_table_new(&mt, *readers, n, GIT_SHA1_FORMAT_ID);
	check(!err);
	return mt;
}

static void t_merged_logs(void)
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
	struct strbuf bufs[3] = { STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt = merged_table_from_log_records(
		logs, &bs, &readers, sizes, bufs, 3);
	struct reftable_iterator it = { 0 };
	int err;
	struct reftable_log_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	size_t i;

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_LOG);
	check(!err);
	err = reftable_iterator_seek_log(&it, "a");
	check(!err);
	check_int(reftable_merged_table_hash_id(mt), ==, GIT_SHA1_FORMAT_ID);
	check_int(reftable_merged_table_min_update_index(mt), ==, 1);
	check_int(reftable_merged_table_max_update_index(mt), ==, 3);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_log_record log = { 0 };
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;

		REFTABLE_ALLOC_GROW(out, len + 1, cap);
		out[len++] = log;
	}
	reftable_iterator_destroy(&it);

	check_int(ARRAY_SIZE(want), ==, len);
	for (i = 0; i < len; i++)
		check(reftable_log_record_equal(want[i], &out[i],
						 GIT_SHA1_RAWSZ));

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_LOG);
	check(!err);
	err = reftable_iterator_seek_log_at(&it, "a", 2);
	check(!err);
	reftable_log_record_release(&out[0]);
	err = reftable_iterator_next_log(&it, &out[0]);
	check(!err);
	check(reftable_log_record_equal(&out[0], &r3[0], GIT_SHA1_RAWSZ));
	reftable_iterator_destroy(&it);

	for (i = 0; i < len; i++)
		reftable_log_record_release(&out[i]);
	reftable_free(out);

	for (i = 0; i < 3; i++)
		strbuf_release(&bufs[i]);
	readers_destroy(readers, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

static void t_default_write_opts(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record rec = {
		.refname = (char *) "master",
		.update_index = 1,
	};
	int err;
	struct reftable_block_source source = { 0 };
	uint32_t hash_id;
	struct reftable_reader *rd = NULL;
	struct reftable_merged_table *merged = NULL;

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_add_ref(w, &rec);
	check(!err);

	err = reftable_writer_close(w);
	check(!err);
	reftable_writer_free(w);

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&rd, &source, "filename");
	check(!err);

	hash_id = reftable_reader_hash_id(rd);
	check_int(hash_id, ==, GIT_SHA1_FORMAT_ID);

	err = reftable_merged_table_new(&merged, &rd, 1, GIT_SHA256_FORMAT_ID);
	check_int(err, ==, REFTABLE_FORMAT_ERROR);
	err = reftable_merged_table_new(&merged, &rd, 1, GIT_SHA1_FORMAT_ID);
	check(!err);

	reftable_reader_decref(rd);
	reftable_merged_table_free(merged);
	strbuf_release(&buf);
}


int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	TEST(t_default_write_opts(), "merged table with default write opts");
	TEST(t_merged_logs(), "merged table with multiple log updates for same ref");
	TEST(t_merged_refs(), "merged table with multiple updates to same ref");
	TEST(t_merged_seek_multiple_times(), "merged table can seek multiple times");
	TEST(t_merged_single_record(), "ref occurring in only one record can be fetched");

	return test_done();
}
