/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"

#include "basics.h"
#include "block.h"
#include "blocksource.h"
#include "reader.h"
#include "record.h"
#include "test_framework.h"
#include "reftable-tests.h"
#include "reftable-writer.h"

static const int update_index = 5;

static void test_buffer(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { NULL };
	struct reftable_block out = { NULL };
	int n;
	uint8_t in[] = "hello";
	strbuf_add(&buf, in, sizeof(in));
	block_source_from_strbuf(&source, &buf);
	EXPECT(block_source_size(&source) == 6);
	n = block_source_read_block(&source, &out, 0, sizeof(in));
	EXPECT(n == sizeof(in));
	EXPECT(!memcmp(in, out.data, n));
	reftable_block_done(&out);

	n = block_source_read_block(&source, &out, 1, 2);
	EXPECT(n == 2);
	EXPECT(!memcmp(out.data, "el", 2));

	reftable_block_done(&out);
	block_source_close(&source);
	strbuf_release(&buf);
}

static void write_table(char ***names, struct strbuf *buf, int N,
			int block_size, uint32_t hash_id)
{
	struct reftable_write_options opts = {
		.block_size = block_size,
		.hash_id = hash_id,
	};
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, buf, &opts);
	struct reftable_ref_record ref = { NULL };
	int i = 0, n;
	struct reftable_log_record log = { NULL };
	const struct reftable_stats *stats = NULL;

	REFTABLE_CALLOC_ARRAY(*names, N + 1);

	reftable_writer_set_limits(w, update_index, update_index);
	for (i = 0; i < N; i++) {
		char name[100];
		int n;

		snprintf(name, sizeof(name), "refs/heads/branch%02d", i);

		ref.refname = name;
		ref.update_index = update_index;
		ref.value_type = REFTABLE_REF_VAL1;
		set_test_hash(ref.value.val1, i);
		(*names)[i] = xstrdup(name);

		n = reftable_writer_add_ref(w, &ref);
		EXPECT(n == 0);
	}

	for (i = 0; i < N; i++) {
		uint8_t hash[GIT_SHA256_RAWSZ] = { 0 };
		char name[100];
		int n;

		set_test_hash(hash, i);

		snprintf(name, sizeof(name), "refs/heads/branch%02d", i);

		log.refname = name;
		log.update_index = update_index;
		log.value_type = REFTABLE_LOG_UPDATE;
		log.value.update.new_hash = hash;
		log.value.update.message = "message";

		n = reftable_writer_add_log(w, &log);
		EXPECT(n == 0);
	}

	n = reftable_writer_close(w);
	EXPECT(n == 0);

	stats = reftable_writer_stats(w);
	for (i = 0; i < stats->ref_stats.blocks; i++) {
		int off = i * opts.block_size;
		if (off == 0) {
			off = header_size(
				(hash_id == GIT_SHA256_FORMAT_ID) ? 2 : 1);
		}
		EXPECT(buf->buf[off] == 'r');
	}

	EXPECT(stats->log_stats.blocks > 0);
	reftable_writer_free(w);
}

static void test_log_buffer_size(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_write_options opts = {
		.block_size = 4096,
	};
	int err;
	int i;
	struct reftable_log_record
		log = { .refname = "refs/heads/master",
			.update_index = 0xa,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = { .update = {
					   .name = "Han-Wen Nienhuys",
					   .email = "hanwen@google.com",
					   .tz_offset = 100,
					   .time = 0x5e430672,
					   .message = "commit: 9\n",
				   } } };
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);

	/* This tests buffer extension for log compression. Must use a random
	   hash, to ensure that the compressed part is larger than the original.
	*/
	uint8_t hash1[GIT_SHA1_RAWSZ], hash2[GIT_SHA1_RAWSZ];
	for (i = 0; i < GIT_SHA1_RAWSZ; i++) {
		hash1[i] = (uint8_t)(git_rand() % 256);
		hash2[i] = (uint8_t)(git_rand() % 256);
	}
	log.value.update.old_hash = hash1;
	log.value.update.new_hash = hash2;
	reftable_writer_set_limits(w, update_index, update_index);
	err = reftable_writer_add_log(w, &log);
	EXPECT_ERR(err);
	err = reftable_writer_close(w);
	EXPECT_ERR(err);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_log_overflow(void)
{
	struct strbuf buf = STRBUF_INIT;
	char msg[256] = { 0 };
	struct reftable_write_options opts = {
		.block_size = ARRAY_SIZE(msg),
	};
	int err;
	struct reftable_log_record
		log = { .refname = "refs/heads/master",
			.update_index = 0xa,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = { .update = {
					   .name = "Han-Wen Nienhuys",
					   .email = "hanwen@google.com",
					   .tz_offset = 100,
					   .time = 0x5e430672,
					   .message = msg,
				   } } };
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);

	uint8_t hash1[GIT_SHA1_RAWSZ]  = {1}, hash2[GIT_SHA1_RAWSZ] = { 2 };

	memset(msg, 'x', sizeof(msg) - 1);
	log.value.update.old_hash = hash1;
	log.value.update.new_hash = hash2;
	reftable_writer_set_limits(w, update_index, update_index);
	err = reftable_writer_add_log(w, &log);
	EXPECT(err == REFTABLE_ENTRY_TOO_BIG_ERROR);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_log_write_read(void)
{
	int N = 2;
	char **names = reftable_calloc(N + 1, sizeof(*names));
	int err;
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { NULL };
	int i = 0;
	struct reftable_log_record log = { NULL };
	int n;
	struct reftable_iterator it = { NULL };
	struct reftable_reader rd = { NULL };
	struct reftable_block_source source = { NULL };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	const struct reftable_stats *stats = NULL;
	reftable_writer_set_limits(w, 0, N);
	for (i = 0; i < N; i++) {
		char name[256];
		struct reftable_ref_record ref = { NULL };
		snprintf(name, sizeof(name), "b%02d%0*d", i, 130, 7);
		names[i] = xstrdup(name);
		ref.refname = name;
		ref.update_index = i;

		err = reftable_writer_add_ref(w, &ref);
		EXPECT_ERR(err);
	}
	for (i = 0; i < N; i++) {
		uint8_t hash1[GIT_SHA1_RAWSZ], hash2[GIT_SHA1_RAWSZ];
		struct reftable_log_record log = { NULL };
		set_test_hash(hash1, i);
		set_test_hash(hash2, i + 1);

		log.refname = names[i];
		log.update_index = i;
		log.value_type = REFTABLE_LOG_UPDATE;
		log.value.update.old_hash = hash1;
		log.value.update.new_hash = hash2;

		err = reftable_writer_add_log(w, &log);
		EXPECT_ERR(err);
	}

	n = reftable_writer_close(w);
	EXPECT(n == 0);

	stats = reftable_writer_stats(w);
	EXPECT(stats->log_stats.blocks > 0);
	reftable_writer_free(w);
	w = NULL;

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.log");
	EXPECT_ERR(err);

	err = reftable_reader_seek_ref(&rd, &it, names[N - 1]);
	EXPECT_ERR(err);

	err = reftable_iterator_next_ref(&it, &ref);
	EXPECT_ERR(err);

	/* end of iteration. */
	err = reftable_iterator_next_ref(&it, &ref);
	EXPECT(0 < err);

	reftable_iterator_destroy(&it);
	reftable_ref_record_release(&ref);

	err = reftable_reader_seek_log(&rd, &it, "");
	EXPECT_ERR(err);

	i = 0;
	while (1) {
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			break;
		}

		EXPECT_ERR(err);
		EXPECT_STREQ(names[i], log.refname);
		EXPECT(i == log.update_index);
		i++;
		reftable_log_record_release(&log);
	}

	EXPECT(i == N);
	reftable_iterator_destroy(&it);

	/* cleanup. */
	strbuf_release(&buf);
	free_names(names);
	reader_close(&rd);
}

static void test_log_zlib_corruption(void)
{
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_iterator it = { 0 };
	struct reftable_reader rd = { 0 };
	struct reftable_block_source source = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	const struct reftable_stats *stats = NULL;
	uint8_t hash1[GIT_SHA1_RAWSZ] = { 1 };
	uint8_t hash2[GIT_SHA1_RAWSZ] = { 2 };
	char message[100] = { 0 };
	int err, i, n;

	struct reftable_log_record log = {
		.refname = "refname",
		.value_type = REFTABLE_LOG_UPDATE,
		.value = {
			.update = {
				.new_hash = hash1,
				.old_hash = hash2,
				.name = "My Name",
				.email = "myname@invalid",
				.message = message,
			},
		},
	};

	for (i = 0; i < sizeof(message) - 1; i++)
		message[i] = (uint8_t)(git_rand() % 64 + ' ');

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_add_log(w, &log);
	EXPECT_ERR(err);

	n = reftable_writer_close(w);
	EXPECT(n == 0);

	stats = reftable_writer_stats(w);
	EXPECT(stats->log_stats.blocks > 0);
	reftable_writer_free(w);
	w = NULL;

	/* corrupt the data. */
	buf.buf[50] ^= 0x99;

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.log");
	EXPECT_ERR(err);

	err = reftable_reader_seek_log(&rd, &it, "refname");
	EXPECT(err == REFTABLE_ZLIB_ERROR);

	reftable_iterator_destroy(&it);

	/* cleanup. */
	strbuf_release(&buf);
	reader_close(&rd);
}

static void test_table_read_write_sequential(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_iterator it = { NULL };
	struct reftable_block_source source = { NULL };
	struct reftable_reader rd = { NULL };
	int err = 0;
	int j = 0;

	write_table(&names, &buf, N, 256, GIT_SHA1_FORMAT_ID);

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.ref");
	EXPECT_ERR(err);

	err = reftable_reader_seek_ref(&rd, &it, "");
	EXPECT_ERR(err);

	while (1) {
		struct reftable_ref_record ref = { NULL };
		int r = reftable_iterator_next_ref(&it, &ref);
		EXPECT(r >= 0);
		if (r > 0) {
			break;
		}
		EXPECT(0 == strcmp(names[j], ref.refname));
		EXPECT(update_index == ref.update_index);

		j++;
		reftable_ref_record_release(&ref);
	}
	EXPECT(j == N);
	reftable_iterator_destroy(&it);
	strbuf_release(&buf);
	free_names(names);

	reader_close(&rd);
}

static void test_table_write_small_table(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 1;
	write_table(&names, &buf, N, 4096, GIT_SHA1_FORMAT_ID);
	EXPECT(buf.len < 200);
	strbuf_release(&buf);
	free_names(names);
}

static void test_table_read_api(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_reader rd = { NULL };
	struct reftable_block_source source = { NULL };
	int err;
	int i;
	struct reftable_log_record log = { NULL };
	struct reftable_iterator it = { NULL };

	write_table(&names, &buf, N, 256, GIT_SHA1_FORMAT_ID);

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.ref");
	EXPECT_ERR(err);

	err = reftable_reader_seek_ref(&rd, &it, names[0]);
	EXPECT_ERR(err);

	err = reftable_iterator_next_log(&it, &log);
	EXPECT(err == REFTABLE_API_ERROR);

	strbuf_release(&buf);
	for (i = 0; i < N; i++) {
		reftable_free(names[i]);
	}
	reftable_iterator_destroy(&it);
	reftable_free(names);
	reader_close(&rd);
	strbuf_release(&buf);
}

static void test_table_read_write_seek(int index, int hash_id)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_reader rd = { NULL };
	struct reftable_block_source source = { NULL };
	int err;
	int i = 0;

	struct reftable_iterator it = { NULL };
	struct strbuf pastLast = STRBUF_INIT;
	struct reftable_ref_record ref = { NULL };

	write_table(&names, &buf, N, 256, hash_id);

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.ref");
	EXPECT_ERR(err);
	EXPECT(hash_id == reftable_reader_hash_id(&rd));

	if (!index) {
		rd.ref_offsets.index_offset = 0;
	} else {
		EXPECT(rd.ref_offsets.index_offset > 0);
	}

	for (i = 1; i < N; i++) {
		int err = reftable_reader_seek_ref(&rd, &it, names[i]);
		EXPECT_ERR(err);
		err = reftable_iterator_next_ref(&it, &ref);
		EXPECT_ERR(err);
		EXPECT(0 == strcmp(names[i], ref.refname));
		EXPECT(REFTABLE_REF_VAL1 == ref.value_type);
		EXPECT(i == ref.value.val1[0]);

		reftable_ref_record_release(&ref);
		reftable_iterator_destroy(&it);
	}

	strbuf_addstr(&pastLast, names[N - 1]);
	strbuf_addstr(&pastLast, "/");

	err = reftable_reader_seek_ref(&rd, &it, pastLast.buf);
	if (err == 0) {
		struct reftable_ref_record ref = { NULL };
		int err = reftable_iterator_next_ref(&it, &ref);
		EXPECT(err > 0);
	} else {
		EXPECT(err > 0);
	}

	strbuf_release(&pastLast);
	reftable_iterator_destroy(&it);

	strbuf_release(&buf);
	for (i = 0; i < N; i++) {
		reftable_free(names[i]);
	}
	reftable_free(names);
	reader_close(&rd);
}

static void test_table_read_write_seek_linear(void)
{
	test_table_read_write_seek(0, GIT_SHA1_FORMAT_ID);
}

static void test_table_read_write_seek_linear_sha256(void)
{
	test_table_read_write_seek(0, GIT_SHA256_FORMAT_ID);
}

static void test_table_read_write_seek_index(void)
{
	test_table_read_write_seek(1, GIT_SHA1_FORMAT_ID);
}

static void test_table_refs_for(int indexed)
{
	int N = 50;
	char **want_names = reftable_calloc(N + 1, sizeof(*want_names));
	int want_names_len = 0;
	uint8_t want_hash[GIT_SHA1_RAWSZ];

	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { NULL };
	int i = 0;
	int n;
	int err;
	struct reftable_reader rd;
	struct reftable_block_source source = { NULL };

	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);

	struct reftable_iterator it = { NULL };
	int j;

	set_test_hash(want_hash, 4);

	for (i = 0; i < N; i++) {
		uint8_t hash[GIT_SHA1_RAWSZ];
		char fill[51] = { 0 };
		char name[100];
		struct reftable_ref_record ref = { NULL };

		memset(hash, i, sizeof(hash));
		memset(fill, 'x', 50);
		/* Put the variable part in the start */
		snprintf(name, sizeof(name), "br%02d%s", i, fill);
		name[40] = 0;
		ref.refname = name;

		ref.value_type = REFTABLE_REF_VAL2;
		set_test_hash(ref.value.val2.value, i / 4);
		set_test_hash(ref.value.val2.target_value, 3 + i / 4);

		/* 80 bytes / entry, so 3 entries per block. Yields 17
		 */
		/* blocks. */
		n = reftable_writer_add_ref(w, &ref);
		EXPECT(n == 0);

		if (!memcmp(ref.value.val2.value, want_hash, GIT_SHA1_RAWSZ) ||
		    !memcmp(ref.value.val2.target_value, want_hash, GIT_SHA1_RAWSZ)) {
			want_names[want_names_len++] = xstrdup(name);
		}
	}

	n = reftable_writer_close(w);
	EXPECT(n == 0);

	reftable_writer_free(w);
	w = NULL;

	block_source_from_strbuf(&source, &buf);

	err = init_reader(&rd, &source, "file.ref");
	EXPECT_ERR(err);
	if (!indexed) {
		rd.obj_offsets.is_present = 0;
	}

	err = reftable_reader_seek_ref(&rd, &it, "");
	EXPECT_ERR(err);
	reftable_iterator_destroy(&it);

	err = reftable_reader_refs_for(&rd, &it, want_hash);
	EXPECT_ERR(err);

	j = 0;
	while (1) {
		int err = reftable_iterator_next_ref(&it, &ref);
		EXPECT(err >= 0);
		if (err > 0) {
			break;
		}

		EXPECT(j < want_names_len);
		EXPECT(0 == strcmp(ref.refname, want_names[j]));
		j++;
		reftable_ref_record_release(&ref);
	}
	EXPECT(j == want_names_len);

	strbuf_release(&buf);
	free_names(want_names);
	reftable_iterator_destroy(&it);
	reader_close(&rd);
}

static void test_table_refs_for_no_index(void)
{
	test_table_refs_for(0);
}

static void test_table_refs_for_obj_index(void)
{
	test_table_refs_for(1);
}

static void test_write_empty_table(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	struct reftable_block_source source = { NULL };
	struct reftable_reader *rd = NULL;
	struct reftable_ref_record rec = { NULL };
	struct reftable_iterator it = { NULL };
	int err;

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_close(w);
	EXPECT(err == REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);

	EXPECT(buf.len == header_size(1) + footer_size(1));

	block_source_from_strbuf(&source, &buf);

	err = reftable_new_reader(&rd, &source, "filename");
	EXPECT_ERR(err);

	err = reftable_reader_seek_ref(rd, &it, "");
	EXPECT_ERR(err);

	err = reftable_iterator_next_ref(&it, &rec);
	EXPECT(err > 0);

	reftable_iterator_destroy(&it);
	reftable_reader_free(rd);
	strbuf_release(&buf);
}

static void test_write_object_id_min_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	struct reftable_ref_record ref = {
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = {42},
	};
	int err;
	int i;

	reftable_writer_set_limits(w, 1, 1);

	/* Write the same hash in many refs. If there is only 1 hash, the
	 * disambiguating prefix is length 0 */
	for (i = 0; i < 256; i++) {
		char name[256];
		snprintf(name, sizeof(name), "ref%05d", i);
		ref.refname = name;
		err = reftable_writer_add_ref(w, &ref);
		EXPECT_ERR(err);
	}

	err = reftable_writer_close(w);
	EXPECT_ERR(err);
	EXPECT(reftable_writer_stats(w)->object_id_len == 2);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_write_object_id_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	struct reftable_ref_record ref = {
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = {42},
	};
	int err;
	int i;

	reftable_writer_set_limits(w, 1, 1);

	/* Write the same hash in many refs. If there is only 1 hash, the
	 * disambiguating prefix is length 0 */
	for (i = 0; i < 256; i++) {
		char name[256];
		snprintf(name, sizeof(name), "ref%05d", i);
		ref.refname = name;
		ref.value.val1[15] = i;
		err = reftable_writer_add_ref(w, &ref);
		EXPECT_ERR(err);
	}

	err = reftable_writer_close(w);
	EXPECT_ERR(err);
	EXPECT(reftable_writer_stats(w)->object_id_len == 16);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_write_empty_key(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	struct reftable_ref_record ref = {
		.refname = "",
		.update_index = 1,
		.value_type = REFTABLE_REF_DELETION,
	};
	int err;

	reftable_writer_set_limits(w, 1, 1);
	err = reftable_writer_add_ref(w, &ref);
	EXPECT(err == REFTABLE_API_ERROR);

	err = reftable_writer_close(w);
	EXPECT(err == REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_write_key_order(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w =
		reftable_new_writer(&strbuf_add_void, &noop_flush, &buf, &opts);
	struct reftable_ref_record refs[2] = {
		{
			.refname = "b",
			.update_index = 1,
			.value_type = REFTABLE_REF_SYMREF,
			.value = {
				.symref = "target",
			},
		}, {
			.refname = "a",
			.update_index = 1,
			.value_type = REFTABLE_REF_SYMREF,
			.value = {
				.symref = "target",
			},
		}
	};
	int err;

	reftable_writer_set_limits(w, 1, 1);
	err = reftable_writer_add_ref(w, &refs[0]);
	EXPECT_ERR(err);
	err = reftable_writer_add_ref(w, &refs[1]);
	EXPECT(err == REFTABLE_API_ERROR);
	reftable_writer_close(w);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void test_write_multiple_indices(void)
{
	struct reftable_write_options opts = {
		.block_size = 100,
	};
	struct strbuf writer_buf = STRBUF_INIT, buf = STRBUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_iterator it = { 0 };
	const struct reftable_stats *stats;
	struct reftable_writer *writer;
	struct reftable_reader *reader;
	int err, i;

	writer = reftable_new_writer(&strbuf_add_void, &noop_flush, &writer_buf, &opts);
	reftable_writer_set_limits(writer, 1, 1);
	for (i = 0; i < 100; i++) {
		struct reftable_ref_record ref = {
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = {i},
		};

		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/heads/%04d", i);
		ref.refname = buf.buf,

		err = reftable_writer_add_ref(writer, &ref);
		EXPECT_ERR(err);
	}

	for (i = 0; i < 100; i++) {
		unsigned char hash[GIT_SHA1_RAWSZ] = {i};
		struct reftable_log_record log = {
			.update_index = 1,
			.value_type = REFTABLE_LOG_UPDATE,
			.value.update = {
				.old_hash = hash,
				.new_hash = hash,
			},
		};

		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/heads/%04d", i);
		log.refname = buf.buf,

		err = reftable_writer_add_log(writer, &log);
		EXPECT_ERR(err);
	}

	reftable_writer_close(writer);

	/*
	 * The written data should be sufficiently large to result in indices
	 * for each of the block types.
	 */
	stats = reftable_writer_stats(writer);
	EXPECT(stats->ref_stats.index_offset > 0);
	EXPECT(stats->obj_stats.index_offset > 0);
	EXPECT(stats->log_stats.index_offset > 0);

	block_source_from_strbuf(&source, &writer_buf);
	err = reftable_new_reader(&reader, &source, "filename");
	EXPECT_ERR(err);

	/*
	 * Seeking the log uses the log index now. In case there is any
	 * confusion regarding indices we would notice here.
	 */
	err = reftable_reader_seek_log(reader, &it, "");
	EXPECT_ERR(err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_reader_free(reader);
	strbuf_release(&writer_buf);
	strbuf_release(&buf);
}

static void test_write_multi_level_index(void)
{
	struct reftable_write_options opts = {
		.block_size = 100,
	};
	struct strbuf writer_buf = STRBUF_INIT, buf = STRBUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_iterator it = { 0 };
	const struct reftable_stats *stats;
	struct reftable_writer *writer;
	struct reftable_reader *reader;
	int err;

	writer = reftable_new_writer(&strbuf_add_void, &noop_flush, &writer_buf, &opts);
	reftable_writer_set_limits(writer, 1, 1);
	for (size_t i = 0; i < 200; i++) {
		struct reftable_ref_record ref = {
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = {i},
		};

		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/heads/%03" PRIuMAX, (uintmax_t)i);
		ref.refname = buf.buf,

		err = reftable_writer_add_ref(writer, &ref);
		EXPECT_ERR(err);
	}
	reftable_writer_close(writer);

	/*
	 * The written refs should be sufficiently large to result in a
	 * multi-level index.
	 */
	stats = reftable_writer_stats(writer);
	EXPECT(stats->ref_stats.max_index_level == 2);

	block_source_from_strbuf(&source, &writer_buf);
	err = reftable_new_reader(&reader, &source, "filename");
	EXPECT_ERR(err);

	/*
	 * Seeking the last ref should work as expected.
	 */
	err = reftable_reader_seek_ref(reader, &it, "refs/heads/199");
	EXPECT_ERR(err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_reader_free(reader);
	strbuf_release(&writer_buf);
	strbuf_release(&buf);
}

static void test_corrupt_table_empty(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { NULL };
	struct reftable_reader rd = { NULL };
	int err;

	block_source_from_strbuf(&source, &buf);
	err = init_reader(&rd, &source, "file.log");
	EXPECT(err == REFTABLE_FORMAT_ERROR);
}

static void test_corrupt_table(void)
{
	uint8_t zeros[1024] = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { NULL };
	struct reftable_reader rd = { NULL };
	int err;
	strbuf_add(&buf, zeros, sizeof(zeros));

	block_source_from_strbuf(&source, &buf);
	err = init_reader(&rd, &source, "file.log");
	EXPECT(err == REFTABLE_FORMAT_ERROR);
	strbuf_release(&buf);
}

int readwrite_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_log_zlib_corruption);
	RUN_TEST(test_corrupt_table);
	RUN_TEST(test_corrupt_table_empty);
	RUN_TEST(test_log_write_read);
	RUN_TEST(test_write_key_order);
	RUN_TEST(test_table_read_write_seek_linear_sha256);
	RUN_TEST(test_log_buffer_size);
	RUN_TEST(test_table_write_small_table);
	RUN_TEST(test_buffer);
	RUN_TEST(test_table_read_api);
	RUN_TEST(test_table_read_write_sequential);
	RUN_TEST(test_table_read_write_seek_linear);
	RUN_TEST(test_table_read_write_seek_index);
	RUN_TEST(test_table_refs_for_no_index);
	RUN_TEST(test_table_refs_for_obj_index);
	RUN_TEST(test_write_empty_key);
	RUN_TEST(test_write_empty_table);
	RUN_TEST(test_log_overflow);
	RUN_TEST(test_write_object_id_length);
	RUN_TEST(test_write_object_id_min_length);
	RUN_TEST(test_write_multiple_indices);
	RUN_TEST(test_write_multi_level_index);
	return 0;
}
