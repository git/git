/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/basics.h"
#include "reftable/blocksource.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-writer.h"
#include "reftable/table.h"
#include "strbuf.h"

static const int update_index = 5;

void test_reftable_readwrite__buffer(void)
{
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_block_data out = { 0 };
	int n;
	uint8_t in[] = "hello";
	cl_assert_equal_i(reftable_buf_add(&buf, in, sizeof(in)), 0);
	block_source_from_buf(&source, &buf);
	cl_assert_equal_i(block_source_size(&source), 6);
	n = block_source_read_data(&source, &out, 0, sizeof(in));
	cl_assert_equal_i(n, sizeof(in));
	cl_assert(!memcmp(in, out.data, n));
	block_source_release_data(&out);

	n = block_source_read_data(&source, &out, 1, 2);
	cl_assert_equal_i(n, 2);
	cl_assert(!memcmp(out.data, "el", 2));

	block_source_release_data(&out);
	block_source_close(&source);
	reftable_buf_release(&buf);
}

static void write_table(char ***names, struct reftable_buf *buf, int N,
			int block_size, enum reftable_hash hash_id)
{
	struct reftable_write_options opts = {
		.block_size = block_size,
		.hash_id = hash_id,
	};
	struct reftable_ref_record *refs;
	struct reftable_log_record *logs;
	int i;

	REFTABLE_CALLOC_ARRAY(*names, N + 1);
	cl_assert(*names != NULL);
	REFTABLE_CALLOC_ARRAY(refs, N);
	cl_assert(refs != NULL);
	REFTABLE_CALLOC_ARRAY(logs, N);
	cl_assert(logs != NULL);

	for (i = 0; i < N; i++) {
		refs[i].refname = (*names)[i] = xstrfmt("refs/heads/branch%02d", i);
		refs[i].update_index = update_index;
		refs[i].value_type = REFTABLE_REF_VAL1;
		cl_reftable_set_hash(refs[i].value.val1, i,
				     REFTABLE_HASH_SHA1);
	}

	for (i = 0; i < N; i++) {
		logs[i].refname = (*names)[i];
		logs[i].update_index = update_index;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		cl_reftable_set_hash(logs[i].value.update.new_hash, i,
				     REFTABLE_HASH_SHA1);
		logs[i].value.update.message = (char *) "message";
	}

	cl_reftable_write_to_buf(buf, refs, N, logs, N, &opts);

	reftable_free(refs);
	reftable_free(logs);
}

void test_reftable_readwrite__log_buffer_size(void)
{
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_write_options opts = {
		.block_size = 4096,
	};
	int i;
	struct reftable_log_record
		log = { .refname = (char *) "refs/heads/master",
			.update_index = update_index,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = { .update = {
					   .name = (char *) "Han-Wen Nienhuys",
					   .email = (char *) "hanwen@google.com",
					   .tz_offset = 100,
					   .time = 0x5e430672,
					   .message = (char *) "commit: 9\n",
				   } } };
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf,
							      &opts);

	/* This tests buffer extension for log compression. Must use a random
	   hash, to ensure that the compressed part is larger than the original.
	*/
	for (i = 0; i < REFTABLE_HASH_SIZE_SHA1; i++) {
		log.value.update.old_hash[i] = (uint8_t)(git_rand(0) % 256);
		log.value.update.new_hash[i] = (uint8_t)(git_rand(0) % 256);
	}
	reftable_writer_set_limits(w, update_index, update_index);
	cl_assert_equal_i(reftable_writer_add_log(w, &log), 0);
	cl_assert_equal_i(reftable_writer_close(w), 0);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__log_overflow(void)
{
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	char msg[256] = { 0 };
	struct reftable_write_options opts = {
		.block_size = ARRAY_SIZE(msg),
	};
	struct reftable_log_record log = {
		.refname = (char *) "refs/heads/master",
		.update_index = update_index,
		.value_type = REFTABLE_LOG_UPDATE,
		.value = {
			.update = {
				.old_hash = { 1 },
				.new_hash = { 2 },
				.name = (char *) "Han-Wen Nienhuys",
				.email = (char *) "hanwen@google.com",
				.tz_offset = 100,
				.time = 0x5e430672,
				.message = msg,
			},
		},
	};
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf,
							      &opts);

	memset(msg, 'x', sizeof(msg) - 1);
	reftable_writer_set_limits(w, update_index, update_index);
	cl_assert_equal_i(reftable_writer_add_log(w, &log), REFTABLE_ENTRY_TOO_BIG_ERROR);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__log_write_limits(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf,
							      &opts);
	struct reftable_log_record log = {
		.refname = (char *)"refs/head/master",
		.update_index = 0,
		.value_type = REFTABLE_LOG_UPDATE,
		.value = {
			.update = {
				.old_hash = { 1 },
				.new_hash = { 2 },
				.name = (char *)"Han-Wen Nienhuys",
				.email = (char *)"hanwen@google.com",
				.tz_offset = 100,
				.time = 0x5e430672,
			},
		},
	};

	reftable_writer_set_limits(w, 1, 1);

	/* write with update_index (0) below set limits (1, 1) */
	cl_assert_equal_i(reftable_writer_add_log(w, &log), 0);

	/* write with update_index (1) in the set limits (1, 1) */
	log.update_index = 1;
	cl_assert_equal_i(reftable_writer_add_log(w, &log), 0);

	/* write with update_index (3) above set limits (1, 1) */
	log.update_index = 3;
	cl_assert_equal_i(reftable_writer_add_log(w, &log), REFTABLE_API_ERROR);

	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__log_write_read(void)
{
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { 0 };
	struct reftable_log_record log = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_table *table;
	struct reftable_block_source source = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	const struct reftable_stats *stats = NULL;
	int N = 2, i;
	char **names;
	int err;

	names = reftable_calloc(N + 1, sizeof(*names));
	cl_assert(names != NULL);

	reftable_writer_set_limits(w, 0, N);

	for (i = 0; i < N; i++) {
		char name[256];
		struct reftable_ref_record ref = { 0 };
		snprintf(name, sizeof(name), "b%02d%0*d", i, 130, 7);
		names[i] = xstrdup(name);
		ref.refname = name;
		ref.update_index = i;

		cl_assert_equal_i(reftable_writer_add_ref(w, &ref), 0);
	}

	for (i = 0; i < N; i++) {
		struct reftable_log_record log = { 0 };

		log.refname = names[i];
		log.update_index = i;
		log.value_type = REFTABLE_LOG_UPDATE;
		cl_reftable_set_hash(log.value.update.old_hash, i,
				     REFTABLE_HASH_SHA1);
		cl_reftable_set_hash(log.value.update.new_hash, i + 1,
				     REFTABLE_HASH_SHA1);

		cl_assert_equal_i(reftable_writer_add_log(w, &log), 0);
	}

	cl_assert_equal_i(reftable_writer_close(w), 0);

	stats = reftable_writer_stats(w);
	cl_assert(stats->log_stats.blocks > 0);
	reftable_writer_free(w);
	w = NULL;

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.log");
	cl_assert(!err);

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);

	err = reftable_iterator_seek_ref(&it, names[N - 1]);
	cl_assert(!err);

	err = reftable_iterator_next_ref(&it, &ref);
	cl_assert(!err);

	/* end of iteration. */
	cl_assert(reftable_iterator_next_ref(&it, &ref) > 0);

	reftable_iterator_destroy(&it);
	reftable_ref_record_release(&ref);

	err = reftable_table_init_log_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_log(&it, "");
	cl_assert(!err);

	for (i = 0; ; i++) {
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;
		cl_assert(!err);
		cl_assert_equal_s(names[i], log.refname);
		cl_assert_equal_i(i, log.update_index);
		reftable_log_record_release(&log);
	}

	cl_assert_equal_i(i, N);
	reftable_iterator_destroy(&it);

	/* cleanup. */
	reftable_buf_release(&buf);
	free_names(names);
	reftable_table_decref(table);
}

void test_reftable_readwrite__log_zlib_corruption(void)
{
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_iterator it = { 0 };
	struct reftable_table *table;
	struct reftable_block_source source = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf,
							      &opts);
	const struct reftable_stats *stats = NULL;
	char message[100] = { 0 };
	int i;
	int err;
	struct reftable_log_record log = {
		.refname = (char *) "refname",
		.value_type = REFTABLE_LOG_UPDATE,
		.value = {
			.update = {
				.new_hash = { 1 },
				.old_hash = { 2 },
				.name = (char *) "My Name",
				.email = (char *) "myname@invalid",
				.message = message,
			},
		},
	};

	for (i = 0; i < sizeof(message) - 1; i++)
		message[i] = (uint8_t)(git_rand(0) % 64 + ' ');

	reftable_writer_set_limits(w, 1, 1);

	cl_assert_equal_i(reftable_writer_add_log(w, &log), 0);
	cl_assert_equal_i(reftable_writer_close(w), 0);

	stats = reftable_writer_stats(w);
	cl_assert(stats->log_stats.blocks > 0);
	reftable_writer_free(w);
	w = NULL;

	/* corrupt the data. */
	buf.buf[50] ^= 0x99;

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.log");
	cl_assert(!err);

	err = reftable_table_init_log_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_log(&it, "refname");
	cl_assert_equal_i(err, REFTABLE_ZLIB_ERROR);

	reftable_iterator_destroy(&it);

	/* cleanup. */
	reftable_table_decref(table);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__table_read_write_sequential(void)
{
	char **names;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int N = 50;
	struct reftable_iterator it = { 0 };
	struct reftable_block_source source = { 0 };
	struct reftable_table *table;
	int err = 0;
	int j = 0;

	write_table(&names, &buf, N, 256, REFTABLE_HASH_SHA1);

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.ref");
	cl_assert(!err);

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "");
	cl_assert(!err);

	for (j = 0; ; j++) {
		struct reftable_ref_record ref = { 0 };
		int r = reftable_iterator_next_ref(&it, &ref);
		cl_assert(r >= 0);
		if (r > 0)
			break;
		cl_assert_equal_s(names[j], ref.refname);
		cl_assert_equal_i(update_index, ref.update_index);
		reftable_ref_record_release(&ref);
	}
	cl_assert_equal_i(j, N);

	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
	free_names(names);
}

void test_reftable_readwrite__table_write_small_table(void)
{
	char **names;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int N = 1;
	write_table(&names, &buf, N, 4096, REFTABLE_HASH_SHA1);
	cl_assert(buf.len < 200);
	reftable_buf_release(&buf);
	free_names(names);
}

void test_reftable_readwrite__table_read_api(void)
{
	char **names;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int N = 50;
	struct reftable_table *table;
	struct reftable_block_source source = { 0 };
	struct reftable_log_record log = { 0 };
	struct reftable_iterator it = { 0 };
	int err;

	write_table(&names, &buf, N, 256, REFTABLE_HASH_SHA1);

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.ref");
	cl_assert(!err);

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, names[0]);
	cl_assert(!err);

	err = reftable_iterator_next_log(&it, &log);
	cl_assert_equal_i(err, REFTABLE_API_ERROR);

	reftable_buf_release(&buf);
	free_names(names);
	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
}

static void t_table_read_write_seek(int index, enum reftable_hash hash_id)
{
	char **names;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int N = 50;
	struct reftable_table *table;
	struct reftable_block_source source = { 0 };
	int err;
	int i = 0;

	struct reftable_iterator it = { 0 };
	struct reftable_buf pastLast = REFTABLE_BUF_INIT;
	struct reftable_ref_record ref = { 0 };

	write_table(&names, &buf, N, 256, hash_id);

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.ref");
	cl_assert(!err);
	cl_assert_equal_i(hash_id, reftable_table_hash_id(table));

	if (!index) {
		table->ref_offsets.index_offset = 0;
	} else {
		cl_assert(table->ref_offsets.index_offset > 0);
	}

	for (i = 1; i < N; i++) {
		err = reftable_table_init_ref_iterator(table, &it);
		cl_assert(!err);
		err = reftable_iterator_seek_ref(&it, names[i]);
		cl_assert(!err);
		err = reftable_iterator_next_ref(&it, &ref);
		cl_assert(!err);
		cl_assert_equal_s(names[i], ref.refname);
		cl_assert_equal_i(REFTABLE_REF_VAL1, ref.value_type);
		cl_assert_equal_i(i, ref.value.val1[0]);

		reftable_ref_record_release(&ref);
		reftable_iterator_destroy(&it);
	}

	cl_assert_equal_i(reftable_buf_addstr(&pastLast, names[N - 1]),
					      0);
	cl_assert_equal_i(reftable_buf_addstr(&pastLast, "/"), 0);

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, pastLast.buf);
	if (err == 0) {
		struct reftable_ref_record ref = { 0 };
		int err = reftable_iterator_next_ref(&it, &ref);
		cl_assert(err > 0);
	} else {
		cl_assert(err > 0);
	}

	reftable_buf_release(&pastLast);
	reftable_iterator_destroy(&it);

	reftable_buf_release(&buf);
	free_names(names);
	reftable_table_decref(table);
}

void test_reftable_readwrite__table_read_write_seek_linear(void)
{
	t_table_read_write_seek(0, REFTABLE_HASH_SHA1);
}

void test_reftable_readwrite__table_read_write_seek_linear_sha256(void)
{
	t_table_read_write_seek(0, REFTABLE_HASH_SHA256);
}

void test_reftable_readwrite__table_read_write_seek_index(void)
{
	t_table_read_write_seek(1, REFTABLE_HASH_SHA1);
}

static void t_table_refs_for(int indexed)
{
	char **want_names;
	int want_names_len = 0;
	uint8_t want_hash[REFTABLE_HASH_SIZE_SHA1];

	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { 0 };
	struct reftable_table *table;
	struct reftable_block_source source = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf,
							      &opts);
	struct reftable_iterator it = { 0 };
	int N = 50, j, i;
	int err;

	want_names = reftable_calloc(N + 1, sizeof(*want_names));
	cl_assert(want_names != NULL);

	cl_reftable_set_hash(want_hash, 4, REFTABLE_HASH_SHA1);

	for (i = 0; i < N; i++) {
		uint8_t hash[REFTABLE_HASH_SIZE_SHA1];
		char fill[51] = { 0 };
		char name[100];
		struct reftable_ref_record ref = { 0 };

		memset(hash, i, sizeof(hash));
		memset(fill, 'x', 50);
		/* Put the variable part in the start */
		snprintf(name, sizeof(name), "br%02d%s", i, fill);
		name[40] = 0;
		ref.refname = name;

		ref.value_type = REFTABLE_REF_VAL2;
		cl_reftable_set_hash(ref.value.val2.value, i / 4,
				     REFTABLE_HASH_SHA1);
		cl_reftable_set_hash(ref.value.val2.target_value,
				     3 + i / 4, REFTABLE_HASH_SHA1);

		/* 80 bytes / entry, so 3 entries per block. Yields 17
		 */
		/* blocks. */
		cl_assert_equal_i(reftable_writer_add_ref(w, &ref), 0);

		if (!memcmp(ref.value.val2.value, want_hash, REFTABLE_HASH_SIZE_SHA1) ||
		    !memcmp(ref.value.val2.target_value, want_hash, REFTABLE_HASH_SIZE_SHA1))
			want_names[want_names_len++] = xstrdup(name);
	}

	cl_assert_equal_i(reftable_writer_close(w), 0);

	reftable_writer_free(w);
	w = NULL;

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "file.ref");
	cl_assert(!err);
	if (!indexed)
		table->obj_offsets.is_present = 0;

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "");
	cl_assert(!err);
	reftable_iterator_destroy(&it);

	err = reftable_table_refs_for(table, &it, want_hash);
	cl_assert(!err);

	for (j = 0; ; j++) {
		int err = reftable_iterator_next_ref(&it, &ref);
		cl_assert(err >= 0);
		if (err > 0)
			break;
		cl_assert(j < want_names_len);
		cl_assert_equal_s(ref.refname, want_names[j]);
		reftable_ref_record_release(&ref);
	}
	cl_assert_equal_i(j, want_names_len);

	reftable_buf_release(&buf);
	free_names(want_names);
	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
}

void test_reftable_readwrite__table_refs_for_no_index(void)
{
	t_table_refs_for(0);
}

void test_reftable_readwrite__table_refs_for_obj_index(void)
{
	t_table_refs_for(1);
}

void test_reftable_readwrite__write_empty_table(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_block_source source = { 0 };
	struct reftable_table *table = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	int err;

	reftable_writer_set_limits(w, 1, 1);

	cl_assert_equal_i(reftable_writer_close(w), REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);

	cl_assert_equal_i(buf.len, header_size(1) + footer_size(1));

	block_source_from_buf(&source, &buf);

	err = reftable_table_new(&table, &source, "filename");
	cl_assert(!err);

	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "");
	cl_assert(!err);

	err = reftable_iterator_next_ref(&it, &rec);
	cl_assert(err > 0);

	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__write_object_id_min_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record ref = {
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = {42},
	};
	int i;

	reftable_writer_set_limits(w, 1, 1);

	/* Write the same hash in many refs. If there is only 1 hash, the
	 * disambiguating prefix is length 0 */
	for (i = 0; i < 256; i++) {
		char name[256];
		snprintf(name, sizeof(name), "ref%05d", i);
		ref.refname = name;
		cl_assert_equal_i(reftable_writer_add_ref(w, &ref), 0);
	}

	cl_assert_equal_i(reftable_writer_close(w), 0);
	cl_assert_equal_i(reftable_writer_stats(w)->object_id_len, 2);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__write_object_id_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record ref = {
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = {42},
	};
	int i;

	reftable_writer_set_limits(w, 1, 1);

	/* Write the same hash in many refs. If there is only 1 hash, the
	 * disambiguating prefix is length 0 */
	for (i = 0; i < 256; i++) {
		char name[256];
		snprintf(name, sizeof(name), "ref%05d", i);
		ref.refname = name;
		ref.value.val1[15] = i;
		cl_assert(reftable_writer_add_ref(w, &ref) == 0);
	}

	cl_assert_equal_i(reftable_writer_close(w), 0);
	cl_assert_equal_i(reftable_writer_stats(w)->object_id_len, 16);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__write_empty_key(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record ref = {
		.refname = (char *) "",
		.update_index = 1,
		.value_type = REFTABLE_REF_DELETION,
	};

	reftable_writer_set_limits(w, 1, 1);
	cl_assert_equal_i(reftable_writer_add_ref(w, &ref), REFTABLE_API_ERROR);
	cl_assert_equal_i(reftable_writer_close(w),
			  REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__write_key_order(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_writer *w = cl_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record refs[2] = {
		{
			.refname = (char *) "b",
			.update_index = 1,
			.value_type = REFTABLE_REF_SYMREF,
			.value = {
				.symref = (char *) "target",
			},
		}, {
			.refname = (char *) "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_SYMREF,
			.value = {
				.symref = (char *) "target",
			},
		}
	};

	reftable_writer_set_limits(w, 1, 1);
	cl_assert_equal_i(reftable_writer_add_ref(w, &refs[0]), 0);
	cl_assert_equal_i(reftable_writer_add_ref(w, &refs[1]),
			  REFTABLE_API_ERROR);

	refs[0].update_index = 2;
	cl_assert_equal_i(reftable_writer_add_ref(w, &refs[0]), REFTABLE_API_ERROR);

	reftable_writer_close(w);
	reftable_writer_free(w);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__write_multiple_indices(void)
{
	struct reftable_write_options opts = {
		.block_size = 100,
	};
	struct reftable_buf writer_buf = REFTABLE_BUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_iterator it = { 0 };
	const struct reftable_stats *stats;
	struct reftable_writer *writer;
	struct reftable_table *table;
	char buf[128];
	int i;
	int err;

	writer = cl_reftable_strbuf_writer(&writer_buf, &opts);
	reftable_writer_set_limits(writer, 1, 1);
	for (i = 0; i < 100; i++) {
		struct reftable_ref_record ref = {
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = {i},
		};

		snprintf(buf, sizeof(buf), "refs/heads/%04d", i);
		ref.refname = buf;

		cl_assert_equal_i(reftable_writer_add_ref(writer, &ref), 0);
	}

	for (i = 0; i < 100; i++) {
		struct reftable_log_record log = {
			.update_index = 1,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = { i },
				.new_hash = { i },
			},
		};

		snprintf(buf, sizeof(buf), "refs/heads/%04d", i);
		log.refname = buf;

		cl_assert_equal_i(reftable_writer_add_log(writer, &log), 0);
	}

	reftable_writer_close(writer);

	/*
	 * The written data should be sufficiently large to result in indices
	 * for each of the block types.
	 */
	stats = reftable_writer_stats(writer);
	cl_assert(stats->ref_stats.index_offset > 0);
	cl_assert(stats->obj_stats.index_offset > 0);
	cl_assert(stats->log_stats.index_offset > 0);

	block_source_from_buf(&source, &writer_buf);
	err = reftable_table_new(&table, &source, "filename");
	cl_assert(!err);

	/*
	 * Seeking the log uses the log index now. In case there is any
	 * confusion regarding indices we would notice here.
	 */
	err = reftable_table_init_log_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_log(&it, "");
	cl_assert(!err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_table_decref(table);
	reftable_buf_release(&writer_buf);
}

void test_reftable_readwrite__write_multi_level_index(void)
{
	struct reftable_write_options opts = {
		.block_size = 100,
	};
	struct reftable_buf writer_buf = REFTABLE_BUF_INIT, buf = REFTABLE_BUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_iterator it = { 0 };
	const struct reftable_stats *stats;
	struct reftable_writer *writer;
	struct reftable_table *table;
	int err;

	writer = cl_reftable_strbuf_writer(&writer_buf, &opts);
	reftable_writer_set_limits(writer, 1, 1);
	for (size_t i = 0; i < 200; i++) {
		struct reftable_ref_record ref = {
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = {i},
		};
		char buf[128];

		snprintf(buf, sizeof(buf), "refs/heads/%03" PRIuMAX, (uintmax_t)i);
		ref.refname = buf;

		cl_assert_equal_i(reftable_writer_add_ref(writer, &ref), 0);
	}
	reftable_writer_close(writer);

	/*
	 * The written refs should be sufficiently large to result in a
	 * multi-level index.
	 */
	stats = reftable_writer_stats(writer);
	cl_assert_equal_i(stats->ref_stats.max_index_level, 2);

	block_source_from_buf(&source, &writer_buf);
	err = reftable_table_new(&table, &source, "filename");
	cl_assert(!err);

	/*
	 * Seeking the last ref should work as expected.
	 */
	err = reftable_table_init_ref_iterator(table, &it);
	cl_assert(!err);
	err = reftable_iterator_seek_ref(&it, "refs/heads/199");
	cl_assert(!err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_table_decref(table);
	reftable_buf_release(&writer_buf);
	reftable_buf_release(&buf);
}

void test_reftable_readwrite__corrupt_table_empty(void)
{
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_table *table;
	int err;

	block_source_from_buf(&source, &buf);
	err = reftable_table_new(&table, &source, "file.log");
	cl_assert_equal_i(err, REFTABLE_FORMAT_ERROR);
}

void test_reftable_readwrite__corrupt_table(void)
{
	uint8_t zeros[1024] = { 0 };
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_table *table;
	int err;

	cl_assert(!reftable_buf_add(&buf, zeros, sizeof(zeros)));

	block_source_from_buf(&source, &buf);
	err = reftable_table_new(&table, &source, "file.log");
	cl_assert_equal_i(err, REFTABLE_FORMAT_ERROR);

	reftable_buf_release(&buf);
}
