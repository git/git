/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "merged.h"

#include "system.h"

#include "basics.h"
#include "blocksource.h"
#include "reader.h"
#include "record.h"
#include "test_framework.h"
#include "reftable-merged.h"
#include "reftable-tests.h"
#include "reftable-generic.h"
#include "reftable-writer.h"

static void write_test_table(struct strbuf *buf,
			     struct reftable_ref_record refs[], int n)
{
	uint64_t min = 0xffffffff;
	uint64_t max = 0;
	int i = 0;
	int err;

	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_writer *w = NULL;
	for (i = 0; i < n; i++) {
		uint64_t ui = refs[i].update_index;
		if (ui > max) {
			max = ui;
		}
		if (ui < min) {
			min = ui;
		}
	}

	w = reftable_new_writer(&strbuf_add_void, buf, &opts);
	reftable_writer_set_limits(w, min, max);

	for (i = 0; i < n; i++) {
		uint64_t before = refs[i].update_index;
		int n = reftable_writer_add_ref(w, &refs[i]);
		EXPECT(n == 0);
		EXPECT(before == refs[i].update_index);
	}

	err = reftable_writer_close(w);
	EXPECT_ERR(err);

	reftable_writer_free(w);
}

static void write_test_log_table(struct strbuf *buf,
				 struct reftable_log_record logs[], int n,
				 uint64_t update_index)
{
	int i = 0;
	int err;

	struct reftable_write_options opts = {
		.block_size = 256,
		.exact_log_message = 1,
	};
	struct reftable_writer *w = NULL;
	w = reftable_new_writer(&strbuf_add_void, buf, &opts);
	reftable_writer_set_limits(w, update_index, update_index);

	for (i = 0; i < n; i++) {
		int err = reftable_writer_add_log(w, &logs[i]);
		EXPECT_ERR(err);
	}

	err = reftable_writer_close(w);
	EXPECT_ERR(err);

	reftable_writer_free(w);
}

static struct reftable_merged_table *
merged_table_from_records(struct reftable_ref_record **refs,
			  struct reftable_block_source **source,
			  struct reftable_reader ***readers, int *sizes,
			  struct strbuf *buf, int n)
{
	int i = 0;
	struct reftable_merged_table *mt = NULL;
	int err;
	struct reftable_table *tabs =
		reftable_calloc(n * sizeof(struct reftable_table));
	*readers = reftable_calloc(n * sizeof(struct reftable_reader *));
	*source = reftable_calloc(n * sizeof(**source));
	for (i = 0; i < n; i++) {
		write_test_table(&buf[i], refs[i], sizes[i]);
		block_source_from_strbuf(&(*source)[i], &buf[i]);

		err = reftable_new_reader(&(*readers)[i], &(*source)[i],
					  "name");
		EXPECT_ERR(err);
		reftable_table_from_reader(&tabs[i], (*readers)[i]);
	}

	err = reftable_new_merged_table(&mt, tabs, n, GIT_SHA1_FORMAT_ID);
	EXPECT_ERR(err);
	return mt;
}

static void readers_destroy(struct reftable_reader **readers, size_t n)
{
	int i = 0;
	for (; i < n; i++)
		reftable_reader_free(readers[i]);
	reftable_free(readers);
}

static void test_merged_between(void)
{
	struct reftable_ref_record r1[] = { {
		.refname = "b",
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = { 1, 2, 3, 0 },
	} };
	struct reftable_ref_record r2[] = { {
		.refname = "a",
		.update_index = 2,
		.value_type = REFTABLE_REF_DELETION,
	} };

	struct reftable_ref_record *refs[] = { r1, r2 };
	int sizes[] = { 1, 1 };
	struct strbuf bufs[2] = { STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &readers, sizes, bufs, 2);
	int i;
	struct reftable_ref_record ref = { NULL };
	struct reftable_iterator it = { NULL };
	int err = reftable_merged_table_seek_ref(mt, &it, "a");
	EXPECT_ERR(err);

	err = reftable_iterator_next_ref(&it, &ref);
	EXPECT_ERR(err);
	EXPECT(ref.update_index == 2);
	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	readers_destroy(readers, 2);
	reftable_merged_table_free(mt);
	for (i = 0; i < ARRAY_SIZE(bufs); i++) {
		strbuf_release(&bufs[i]);
	}
	reftable_free(bs);
}

static void test_merged(void)
{
	struct reftable_ref_record r1[] = {
		{
			.refname = "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = "b",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		},
		{
			.refname = "c",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 1 },
		}
	};
	struct reftable_ref_record r2[] = { {
		.refname = "a",
		.update_index = 2,
		.value_type = REFTABLE_REF_DELETION,
	} };
	struct reftable_ref_record r3[] = {
		{
			.refname = "c",
			.update_index = 3,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 2 },
		},
		{
			.refname = "d",
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
	int sizes[3] = { 3, 1, 2 };
	struct strbuf bufs[3] = { STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt =
		merged_table_from_records(refs, &bs, &readers, sizes, bufs, 3);

	struct reftable_iterator it = { NULL };
	int err = reftable_merged_table_seek_ref(mt, &it, "a");
	struct reftable_ref_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	int i = 0;

	EXPECT_ERR(err);
	EXPECT(reftable_merged_table_hash_id(mt) == GIT_SHA1_FORMAT_ID);
	EXPECT(reftable_merged_table_min_update_index(mt) == 1);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_ref_record ref = { NULL };
		int err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0) {
			break;
		}
		if (len == cap) {
			cap = 2 * cap + 1;
			out = reftable_realloc(
				out, sizeof(struct reftable_ref_record) * cap);
		}
		out[len++] = ref;
	}
	reftable_iterator_destroy(&it);

	EXPECT(ARRAY_SIZE(want) == len);
	for (i = 0; i < len; i++) {
		EXPECT(reftable_ref_record_equal(want[i], &out[i],
						 GIT_SHA1_RAWSZ));
	}
	for (i = 0; i < len; i++) {
		reftable_ref_record_release(&out[i]);
	}
	reftable_free(out);

	for (i = 0; i < 3; i++) {
		strbuf_release(&bufs[i]);
	}
	readers_destroy(readers, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

static struct reftable_merged_table *
merged_table_from_log_records(struct reftable_log_record **logs,
			      struct reftable_block_source **source,
			      struct reftable_reader ***readers, int *sizes,
			      struct strbuf *buf, int n)
{
	int i = 0;
	struct reftable_merged_table *mt = NULL;
	int err;
	struct reftable_table *tabs =
		reftable_calloc(n * sizeof(struct reftable_table));
	*readers = reftable_calloc(n * sizeof(struct reftable_reader *));
	*source = reftable_calloc(n * sizeof(**source));
	for (i = 0; i < n; i++) {
		write_test_log_table(&buf[i], logs[i], sizes[i], i + 1);
		block_source_from_strbuf(&(*source)[i], &buf[i]);

		err = reftable_new_reader(&(*readers)[i], &(*source)[i],
					  "name");
		EXPECT_ERR(err);
		reftable_table_from_reader(&tabs[i], (*readers)[i]);
	}

	err = reftable_new_merged_table(&mt, tabs, n, GIT_SHA1_FORMAT_ID);
	EXPECT_ERR(err);
	return mt;
}

static void test_merged_logs(void)
{
	uint8_t hash1[GIT_SHA1_RAWSZ] = { 1 };
	uint8_t hash2[GIT_SHA1_RAWSZ] = { 2 };
	uint8_t hash3[GIT_SHA1_RAWSZ] = { 3 };
	struct reftable_log_record r1[] = {
		{
			.refname = "a",
			.update_index = 2,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = hash2,
				/* deletion */
				.name = "jane doe",
				.email = "jane@invalid",
				.message = "message2",
			}
		},
		{
			.refname = "a",
			.update_index = 1,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = hash1,
				.new_hash = hash2,
				.name = "jane doe",
				.email = "jane@invalid",
				.message = "message1",
			}
		},
	};
	struct reftable_log_record r2[] = {
		{
			.refname = "a",
			.update_index = 3,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.new_hash = hash3,
				.name = "jane doe",
				.email = "jane@invalid",
				.message = "message3",
			}
		},
	};
	struct reftable_log_record r3[] = {
		{
			.refname = "a",
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
	int sizes[3] = { 2, 1, 1 };
	struct strbuf bufs[3] = { STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };
	struct reftable_block_source *bs = NULL;
	struct reftable_reader **readers = NULL;
	struct reftable_merged_table *mt = merged_table_from_log_records(
		logs, &bs, &readers, sizes, bufs, 3);

	struct reftable_iterator it = { NULL };
	int err = reftable_merged_table_seek_log(mt, &it, "a");
	struct reftable_log_record *out = NULL;
	size_t len = 0;
	size_t cap = 0;
	int i = 0;

	EXPECT_ERR(err);
	EXPECT(reftable_merged_table_hash_id(mt) == GIT_SHA1_FORMAT_ID);
	EXPECT(reftable_merged_table_min_update_index(mt) == 1);

	while (len < 100) { /* cap loops/recursion. */
		struct reftable_log_record log = { NULL };
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			break;
		}
		if (len == cap) {
			cap = 2 * cap + 1;
			out = reftable_realloc(
				out, sizeof(struct reftable_log_record) * cap);
		}
		out[len++] = log;
	}
	reftable_iterator_destroy(&it);

	EXPECT(ARRAY_SIZE(want) == len);
	for (i = 0; i < len; i++) {
		EXPECT(reftable_log_record_equal(want[i], &out[i],
						 GIT_SHA1_RAWSZ));
	}

	err = reftable_merged_table_seek_log_at(mt, &it, "a", 2);
	EXPECT_ERR(err);
	reftable_log_record_release(&out[0]);
	err = reftable_iterator_next_log(&it, &out[0]);
	EXPECT_ERR(err);
	EXPECT(reftable_log_record_equal(&out[0], &r3[0], GIT_SHA1_RAWSZ));
	reftable_iterator_destroy(&it);

	for (i = 0; i < len; i++) {
		reftable_log_record_release(&out[i]);
	}
	reftable_free(out);

	for (i = 0; i < 3; i++) {
		strbuf_release(&bufs[i]);
	}
	readers_destroy(readers, 3);
	reftable_merged_table_free(mt);
	reftable_free(bs);
}

static void test_default_write_opts(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &buf, &opts);

	struct reftable_ref_record rec = {
		.refname = "master",
		.update_index = 1,
	};
	int err;
	struct reftable_block_source source = { NULL };
	struct reftable_table *tab = reftable_calloc(sizeof(*tab) * 1);
	uint32_t hash_id;
	struct reftable_reader *rd = NULL;
	struct reftable_merged_table *merged = NULL;

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_add_ref(w, &rec);
	EXPECT_ERR(err);

	err = reftable_writer_close(w);
	EXPECT_ERR(err);
	reftable_writer_free(w);

	block_source_from_strbuf(&source, &buf);

	err = reftable_new_reader(&rd, &source, "filename");
	EXPECT_ERR(err);

	hash_id = reftable_reader_hash_id(rd);
	EXPECT(hash_id == GIT_SHA1_FORMAT_ID);

	reftable_table_from_reader(&tab[0], rd);
	err = reftable_new_merged_table(&merged, tab, 1, GIT_SHA1_FORMAT_ID);
	EXPECT_ERR(err);

	reftable_reader_free(rd);
	reftable_merged_table_free(merged);
	strbuf_release(&buf);
}

/* XXX test refs_for(oid) */

int merged_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_merged_logs);
	RUN_TEST(test_merged_between);
	RUN_TEST(test_merged);
	RUN_TEST(test_default_write_opts);
	return 0;
}
