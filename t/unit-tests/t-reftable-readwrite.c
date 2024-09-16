/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "lib-reftable.h"
#include "reftable/basics.h"
#include "reftable/blocksource.h"
#include "reftable/reader.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-writer.h"

static const int update_index = 5;

static void t_buffer(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_block out = { 0 };
	int n;
	uint8_t in[] = "hello";
	strbuf_add(&buf, in, sizeof(in));
	block_source_from_strbuf(&source, &buf);
	check_int(block_source_size(&source), ==, 6);
	n = block_source_read_block(&source, &out, 0, sizeof(in));
	check_int(n, ==, sizeof(in));
	check(!memcmp(in, out.data, n));
	reftable_block_done(&out);

	n = block_source_read_block(&source, &out, 1, 2);
	check_int(n, ==, 2);
	check(!memcmp(out.data, "el", 2));

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
	struct reftable_ref_record *refs;
	struct reftable_log_record *logs;
	int i;

	REFTABLE_CALLOC_ARRAY(*names, N + 1);
	REFTABLE_CALLOC_ARRAY(refs, N);
	REFTABLE_CALLOC_ARRAY(logs, N);

	for (i = 0; i < N; i++) {
		refs[i].refname = (*names)[i] = xstrfmt("refs/heads/branch%02d", i);
		refs[i].update_index = update_index;
		refs[i].value_type = REFTABLE_REF_VAL1;
		t_reftable_set_hash(refs[i].value.val1, i, GIT_SHA1_FORMAT_ID);
	}

	for (i = 0; i < N; i++) {
		logs[i].refname = (*names)[i];
		logs[i].update_index = update_index;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		t_reftable_set_hash(logs[i].value.update.new_hash, i,
				    GIT_SHA1_FORMAT_ID);
		logs[i].value.update.message = (char *) "message";
	}

	t_reftable_write_to_buf(buf, refs, N, logs, N, &opts);

	free(refs);
	free(logs);
}

static void t_log_buffer_size(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_write_options opts = {
		.block_size = 4096,
	};
	int err;
	int i;
	struct reftable_log_record
		log = { .refname = (char *) "refs/heads/master",
			.update_index = 0xa,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = { .update = {
					   .name = (char *) "Han-Wen Nienhuys",
					   .email = (char *) "hanwen@google.com",
					   .tz_offset = 100,
					   .time = 0x5e430672,
					   .message = (char *) "commit: 9\n",
				   } } };
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);

	/* This tests buffer extension for log compression. Must use a random
	   hash, to ensure that the compressed part is larger than the original.
	*/
	for (i = 0; i < GIT_SHA1_RAWSZ; i++) {
		log.value.update.old_hash[i] = (uint8_t)(git_rand() % 256);
		log.value.update.new_hash[i] = (uint8_t)(git_rand() % 256);
	}
	reftable_writer_set_limits(w, update_index, update_index);
	err = reftable_writer_add_log(w, &log);
	check(!err);
	err = reftable_writer_close(w);
	check(!err);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_log_overflow(void)
{
	struct strbuf buf = STRBUF_INIT;
	char msg[256] = { 0 };
	struct reftable_write_options opts = {
		.block_size = ARRAY_SIZE(msg),
	};
	int err;
	struct reftable_log_record log = {
		.refname = (char *) "refs/heads/master",
		.update_index = 0xa,
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
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);

	memset(msg, 'x', sizeof(msg) - 1);
	reftable_writer_set_limits(w, update_index, update_index);
	err = reftable_writer_add_log(w, &log);
	check_int(err, ==, REFTABLE_ENTRY_TOO_BIG_ERROR);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_log_write_read(void)
{
	int N = 2;
	char **names = reftable_calloc(N + 1, sizeof(*names));
	int err;
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { 0 };
	int i = 0;
	struct reftable_log_record log = { 0 };
	int n;
	struct reftable_iterator it = { 0 };
	struct reftable_reader *reader;
	struct reftable_block_source source = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	const struct reftable_stats *stats = NULL;
	reftable_writer_set_limits(w, 0, N);
	for (i = 0; i < N; i++) {
		char name[256];
		struct reftable_ref_record ref = { 0 };
		snprintf(name, sizeof(name), "b%02d%0*d", i, 130, 7);
		names[i] = xstrdup(name);
		ref.refname = name;
		ref.update_index = i;

		err = reftable_writer_add_ref(w, &ref);
		check(!err);
	}
	for (i = 0; i < N; i++) {
		struct reftable_log_record log = { 0 };

		log.refname = names[i];
		log.update_index = i;
		log.value_type = REFTABLE_LOG_UPDATE;
		t_reftable_set_hash(log.value.update.old_hash, i,
				    GIT_SHA1_FORMAT_ID);
		t_reftable_set_hash(log.value.update.new_hash, i + 1,
				    GIT_SHA1_FORMAT_ID);

		err = reftable_writer_add_log(w, &log);
		check(!err);
	}

	n = reftable_writer_close(w);
	check_int(n, ==, 0);

	stats = reftable_writer_stats(w);
	check_int(stats->log_stats.blocks, >, 0);
	reftable_writer_free(w);
	w = NULL;

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.log");
	check(!err);

	reftable_reader_init_ref_iterator(reader, &it);

	err = reftable_iterator_seek_ref(&it, names[N - 1]);
	check(!err);

	err = reftable_iterator_next_ref(&it, &ref);
	check(!err);

	/* end of iteration. */
	err = reftable_iterator_next_ref(&it, &ref);
	check_int(err, >, 0);

	reftable_iterator_destroy(&it);
	reftable_ref_record_release(&ref);

	reftable_reader_init_log_iterator(reader, &it);

	err = reftable_iterator_seek_log(&it, "");
	check(!err);

	for (i = 0; ; i++) {
		int err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;
		check(!err);
		check_str(names[i], log.refname);
		check_int(i, ==, log.update_index);
		reftable_log_record_release(&log);
	}

	check_int(i, ==, N);
	reftable_iterator_destroy(&it);

	/* cleanup. */
	strbuf_release(&buf);
	free_names(names);
	reftable_reader_decref(reader);
}

static void t_log_zlib_corruption(void)
{
	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_iterator it = { 0 };
	struct reftable_reader *reader;
	struct reftable_block_source source = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	const struct reftable_stats *stats = NULL;
	char message[100] = { 0 };
	int err, i, n;
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
		message[i] = (uint8_t)(git_rand() % 64 + ' ');

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_add_log(w, &log);
	check(!err);

	n = reftable_writer_close(w);
	check_int(n, ==, 0);

	stats = reftable_writer_stats(w);
	check_int(stats->log_stats.blocks, >, 0);
	reftable_writer_free(w);
	w = NULL;

	/* corrupt the data. */
	buf.buf[50] ^= 0x99;

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.log");
	check(!err);

	reftable_reader_init_log_iterator(reader, &it);
	err = reftable_iterator_seek_log(&it, "refname");
	check_int(err, ==, REFTABLE_ZLIB_ERROR);

	reftable_iterator_destroy(&it);

	/* cleanup. */
	reftable_reader_decref(reader);
	strbuf_release(&buf);
}

static void t_table_read_write_sequential(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_iterator it = { 0 };
	struct reftable_block_source source = { 0 };
	struct reftable_reader *reader;
	int err = 0;
	int j = 0;

	write_table(&names, &buf, N, 256, GIT_SHA1_FORMAT_ID);

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.ref");
	check(!err);

	reftable_reader_init_ref_iterator(reader, &it);
	err = reftable_iterator_seek_ref(&it, "");
	check(!err);

	for (j = 0; ; j++) {
		struct reftable_ref_record ref = { 0 };
		int r = reftable_iterator_next_ref(&it, &ref);
		check_int(r, >=, 0);
		if (r > 0)
			break;
		check_str(names[j], ref.refname);
		check_int(update_index, ==, ref.update_index);
		reftable_ref_record_release(&ref);
	}
	check_int(j, ==, N);

	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	strbuf_release(&buf);
	free_names(names);
}

static void t_table_write_small_table(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 1;
	write_table(&names, &buf, N, 4096, GIT_SHA1_FORMAT_ID);
	check_int(buf.len, <, 200);
	strbuf_release(&buf);
	free_names(names);
}

static void t_table_read_api(void)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_reader *reader;
	struct reftable_block_source source = { 0 };
	int err;
	struct reftable_log_record log = { 0 };
	struct reftable_iterator it = { 0 };

	write_table(&names, &buf, N, 256, GIT_SHA1_FORMAT_ID);

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.ref");
	check(!err);

	reftable_reader_init_ref_iterator(reader, &it);
	err = reftable_iterator_seek_ref(&it, names[0]);
	check(!err);

	err = reftable_iterator_next_log(&it, &log);
	check_int(err, ==, REFTABLE_API_ERROR);

	strbuf_release(&buf);
	free_names(names);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	strbuf_release(&buf);
}

static void t_table_read_write_seek(int index, int hash_id)
{
	char **names;
	struct strbuf buf = STRBUF_INIT;
	int N = 50;
	struct reftable_reader *reader;
	struct reftable_block_source source = { 0 };
	int err;
	int i = 0;

	struct reftable_iterator it = { 0 };
	struct strbuf pastLast = STRBUF_INIT;
	struct reftable_ref_record ref = { 0 };

	write_table(&names, &buf, N, 256, hash_id);

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.ref");
	check(!err);
	check_int(hash_id, ==, reftable_reader_hash_id(reader));

	if (!index) {
		reader->ref_offsets.index_offset = 0;
	} else {
		check_int(reader->ref_offsets.index_offset, >, 0);
	}

	for (i = 1; i < N; i++) {
		reftable_reader_init_ref_iterator(reader, &it);
		err = reftable_iterator_seek_ref(&it, names[i]);
		check(!err);
		err = reftable_iterator_next_ref(&it, &ref);
		check(!err);
		check_str(names[i], ref.refname);
		check_int(REFTABLE_REF_VAL1, ==, ref.value_type);
		check_int(i, ==, ref.value.val1[0]);

		reftable_ref_record_release(&ref);
		reftable_iterator_destroy(&it);
	}

	strbuf_addstr(&pastLast, names[N - 1]);
	strbuf_addstr(&pastLast, "/");

	reftable_reader_init_ref_iterator(reader, &it);
	err = reftable_iterator_seek_ref(&it, pastLast.buf);
	if (err == 0) {
		struct reftable_ref_record ref = { 0 };
		int err = reftable_iterator_next_ref(&it, &ref);
		check_int(err, >, 0);
	} else {
		check_int(err, >, 0);
	}

	strbuf_release(&pastLast);
	reftable_iterator_destroy(&it);

	strbuf_release(&buf);
	free_names(names);
	reftable_reader_decref(reader);
}

static void t_table_read_write_seek_linear(void)
{
	t_table_read_write_seek(0, GIT_SHA1_FORMAT_ID);
}

static void t_table_read_write_seek_linear_sha256(void)
{
	t_table_read_write_seek(0, GIT_SHA256_FORMAT_ID);
}

static void t_table_read_write_seek_index(void)
{
	t_table_read_write_seek(1, GIT_SHA1_FORMAT_ID);
}

static void t_table_refs_for(int indexed)
{
	int N = 50;
	char **want_names = reftable_calloc(N + 1, sizeof(*want_names));
	int want_names_len = 0;
	uint8_t want_hash[GIT_SHA1_RAWSZ];

	struct reftable_write_options opts = {
		.block_size = 256,
	};
	struct reftable_ref_record ref = { 0 };
	int i = 0;
	int n;
	int err;
	struct reftable_reader *reader;
	struct reftable_block_source source = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	struct reftable_iterator it = { 0 };
	int j;

	t_reftable_set_hash(want_hash, 4, GIT_SHA1_FORMAT_ID);

	for (i = 0; i < N; i++) {
		uint8_t hash[GIT_SHA1_RAWSZ];
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
		t_reftable_set_hash(ref.value.val2.value, i / 4,
				    GIT_SHA1_FORMAT_ID);
		t_reftable_set_hash(ref.value.val2.target_value, 3 + i / 4,
				    GIT_SHA1_FORMAT_ID);

		/* 80 bytes / entry, so 3 entries per block. Yields 17
		 */
		/* blocks. */
		n = reftable_writer_add_ref(w, &ref);
		check_int(n, ==, 0);

		if (!memcmp(ref.value.val2.value, want_hash, GIT_SHA1_RAWSZ) ||
		    !memcmp(ref.value.val2.target_value, want_hash, GIT_SHA1_RAWSZ))
			want_names[want_names_len++] = xstrdup(name);
	}

	n = reftable_writer_close(w);
	check_int(n, ==, 0);

	reftable_writer_free(w);
	w = NULL;

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&reader, &source, "file.ref");
	check(!err);
	if (!indexed)
		reader->obj_offsets.is_present = 0;

	reftable_reader_init_ref_iterator(reader, &it);
	err = reftable_iterator_seek_ref(&it, "");
	check(!err);
	reftable_iterator_destroy(&it);

	err = reftable_reader_refs_for(reader, &it, want_hash);
	check(!err);

	for (j = 0; ; j++) {
		int err = reftable_iterator_next_ref(&it, &ref);
		check_int(err, >=, 0);
		if (err > 0)
			break;
		check_int(j, <, want_names_len);
		check_str(ref.refname, want_names[j]);
		reftable_ref_record_release(&ref);
	}
	check_int(j, ==, want_names_len);

	strbuf_release(&buf);
	free_names(want_names);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
}

static void t_table_refs_for_no_index(void)
{
	t_table_refs_for(0);
}

static void t_table_refs_for_obj_index(void)
{
	t_table_refs_for(1);
}

static void t_write_empty_table(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	struct reftable_block_source source = { 0 };
	struct reftable_reader *rd = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	int err;

	reftable_writer_set_limits(w, 1, 1);

	err = reftable_writer_close(w);
	check_int(err, ==, REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);

	check_int(buf.len, ==, header_size(1) + footer_size(1));

	block_source_from_strbuf(&source, &buf);

	err = reftable_reader_new(&rd, &source, "filename");
	check(!err);

	reftable_reader_init_ref_iterator(rd, &it);
	err = reftable_iterator_seek_ref(&it, "");
	check(!err);

	err = reftable_iterator_next_ref(&it, &rec);
	check_int(err, >, 0);

	reftable_iterator_destroy(&it);
	reftable_reader_decref(rd);
	strbuf_release(&buf);
}

static void t_write_object_id_min_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
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
		check(!err);
	}

	err = reftable_writer_close(w);
	check(!err);
	check_int(reftable_writer_stats(w)->object_id_len, ==, 2);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_write_object_id_length(void)
{
	struct reftable_write_options opts = {
		.block_size = 75,
	};
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
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
		check(!err);
	}

	err = reftable_writer_close(w);
	check(!err);
	check_int(reftable_writer_stats(w)->object_id_len, ==, 16);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_write_empty_key(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
	struct reftable_ref_record ref = {
		.refname = (char *) "",
		.update_index = 1,
		.value_type = REFTABLE_REF_DELETION,
	};
	int err;

	reftable_writer_set_limits(w, 1, 1);
	err = reftable_writer_add_ref(w, &ref);
	check_int(err, ==, REFTABLE_API_ERROR);

	err = reftable_writer_close(w);
	check_int(err, ==, REFTABLE_EMPTY_TABLE_ERROR);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_write_key_order(void)
{
	struct reftable_write_options opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_writer *w = t_reftable_strbuf_writer(&buf, &opts);
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
	int err;

	reftable_writer_set_limits(w, 1, 1);
	err = reftable_writer_add_ref(w, &refs[0]);
	check(!err);
	err = reftable_writer_add_ref(w, &refs[1]);
	check_int(err, ==, REFTABLE_API_ERROR);

	refs[0].update_index = 2;
	err = reftable_writer_add_ref(w, &refs[0]);
	check_int(err, ==, REFTABLE_API_ERROR);

	reftable_writer_close(w);
	reftable_writer_free(w);
	strbuf_release(&buf);
}

static void t_write_multiple_indices(void)
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

	writer = t_reftable_strbuf_writer(&writer_buf, &opts);
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
		check(!err);
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

		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/heads/%04d", i);
		log.refname = buf.buf,

		err = reftable_writer_add_log(writer, &log);
		check(!err);
	}

	reftable_writer_close(writer);

	/*
	 * The written data should be sufficiently large to result in indices
	 * for each of the block types.
	 */
	stats = reftable_writer_stats(writer);
	check_int(stats->ref_stats.index_offset, >, 0);
	check_int(stats->obj_stats.index_offset, >, 0);
	check_int(stats->log_stats.index_offset, >, 0);

	block_source_from_strbuf(&source, &writer_buf);
	err = reftable_reader_new(&reader, &source, "filename");
	check(!err);

	/*
	 * Seeking the log uses the log index now. In case there is any
	 * confusion regarding indices we would notice here.
	 */
	reftable_reader_init_log_iterator(reader, &it);
	err = reftable_iterator_seek_log(&it, "");
	check(!err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_reader_decref(reader);
	strbuf_release(&writer_buf);
	strbuf_release(&buf);
}

static void t_write_multi_level_index(void)
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

	writer = t_reftable_strbuf_writer(&writer_buf, &opts);
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
		check(!err);
	}
	reftable_writer_close(writer);

	/*
	 * The written refs should be sufficiently large to result in a
	 * multi-level index.
	 */
	stats = reftable_writer_stats(writer);
	check_int(stats->ref_stats.max_index_level, ==, 2);

	block_source_from_strbuf(&source, &writer_buf);
	err = reftable_reader_new(&reader, &source, "filename");
	check(!err);

	/*
	 * Seeking the last ref should work as expected.
	 */
	reftable_reader_init_ref_iterator(reader, &it);
	err = reftable_iterator_seek_ref(&it, "refs/heads/199");
	check(!err);

	reftable_iterator_destroy(&it);
	reftable_writer_free(writer);
	reftable_reader_decref(reader);
	strbuf_release(&writer_buf);
	strbuf_release(&buf);
}

static void t_corrupt_table_empty(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_reader *reader;
	int err;

	block_source_from_strbuf(&source, &buf);
	err = reftable_reader_new(&reader, &source, "file.log");
	check_int(err, ==, REFTABLE_FORMAT_ERROR);
}

static void t_corrupt_table(void)
{
	uint8_t zeros[1024] = { 0 };
	struct strbuf buf = STRBUF_INIT;
	struct reftable_block_source source = { 0 };
	struct reftable_reader *reader;
	int err;
	strbuf_add(&buf, zeros, sizeof(zeros));

	block_source_from_strbuf(&source, &buf);
	err = reftable_reader_new(&reader, &source, "file.log");
	check_int(err, ==, REFTABLE_FORMAT_ERROR);

	strbuf_release(&buf);
}

int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	TEST(t_buffer(), "strbuf works as blocksource");
	TEST(t_corrupt_table(), "read-write on corrupted table");
	TEST(t_corrupt_table_empty(), "read-write on an empty table");
	TEST(t_log_buffer_size(), "buffer extension for log compression");
	TEST(t_log_overflow(), "log overflow returns expected error");
	TEST(t_log_write_read(), "read-write on log records");
	TEST(t_log_zlib_corruption(), "reading corrupted log record returns expected error");
	TEST(t_table_read_api(), "read on a table");
	TEST(t_table_read_write_seek_index(), "read-write on a table with index");
	TEST(t_table_read_write_seek_linear(), "read-write on a table without index (SHA1)");
	TEST(t_table_read_write_seek_linear_sha256(), "read-write on a table without index (SHA256)");
	TEST(t_table_read_write_sequential(), "sequential read-write on a table");
	TEST(t_table_refs_for_no_index(), "refs-only table with no index");
	TEST(t_table_refs_for_obj_index(), "refs-only table with index");
	TEST(t_table_write_small_table(), "write_table works");
	TEST(t_write_empty_key(), "write on refs with empty keys");
	TEST(t_write_empty_table(), "read-write on empty tables");
	TEST(t_write_key_order(), "refs must be written in increasing order");
	TEST(t_write_multi_level_index(), "table with multi-level index");
	TEST(t_write_multiple_indices(), "table with indices for multiple block types");
	TEST(t_write_object_id_length(), "prefix compression on writing refs");
	TEST(t_write_object_id_min_length(), "prefix compression on writing refs");

	return test_done();
}
