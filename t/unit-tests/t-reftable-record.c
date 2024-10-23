/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "reftable/constants.h"
#include "reftable/record.h"

static void t_copy(struct reftable_record *rec)
{
	struct reftable_record copy;
	uint8_t typ;

	typ = reftable_record_type(rec);
	reftable_record_init(&copy, typ);
	reftable_record_copy_from(&copy, rec, GIT_SHA1_RAWSZ);
	/* do it twice to catch memory leaks */
	reftable_record_copy_from(&copy, rec, GIT_SHA1_RAWSZ);
	check(reftable_record_equal(rec, &copy, GIT_SHA1_RAWSZ));

	reftable_record_release(&copy);
}

static void t_varint_roundtrip(void)
{
	uint64_t inputs[] = { 0,
			      1,
			      27,
			      127,
			      128,
			      257,
			      4096,
			      ((uint64_t)1 << 63),
			      ((uint64_t)1 << 63) + ((uint64_t)1 << 63) - 1 };

	for (size_t i = 0; i < ARRAY_SIZE(inputs); i++) {
		uint8_t dest[10];

		struct string_view out = {
			.buf = dest,
			.len = sizeof(dest),
		};
		uint64_t in = inputs[i];
		int n = put_var_int(&out, in);
		uint64_t got = 0;

		check_int(n, >, 0);
		out.len = n;
		n = get_var_int(&got, &out);
		check_int(n, >, 0);

		check_int(got, ==, in);
	}
}

static void set_hash(uint8_t *h, int j)
{
	for (int i = 0; i < hash_size(GIT_SHA1_FORMAT_ID); i++)
		h[i] = (j >> i) & 0xff;
}

static void t_reftable_ref_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "refs/heads/master",
			.u.ref.value_type = REFTABLE_REF_VAL1,
		},
		{
			.type = BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "refs/heads/master",
			.u.ref.value_type = REFTABLE_REF_DELETION,
		},
		{
			.type = BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "HEAD",
			.u.ref.value_type = REFTABLE_REF_SYMREF,
			.u.ref.value.symref = (char *) "refs/heads/master",
		},
	};

	check(!reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));

	check(!reftable_record_equal(&in[1], &in[2], GIT_SHA1_RAWSZ));
	check_int(reftable_record_cmp(&in[1], &in[2]), >, 0);

	in[1].u.ref.value_type = in[0].u.ref.value_type;
	check(reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));
}

static void t_reftable_ref_record_compare_name(void)
{
	struct reftable_ref_record recs[3] = {
		{
			.refname = (char *) "refs/heads/a"
		},
		{
			.refname = (char *) "refs/heads/b"
		},
		{
			.refname = (char *) "refs/heads/a"
		},
	};

	check_int(reftable_ref_record_compare_name(&recs[0], &recs[1]), <, 0);
	check_int(reftable_ref_record_compare_name(&recs[1], &recs[0]), >, 0);
	check_int(reftable_ref_record_compare_name(&recs[0], &recs[2]), ==, 0);
}

static void t_reftable_ref_record_roundtrip(void)
{
	struct reftable_buf scratch = REFTABLE_BUF_INIT;

	for (int i = REFTABLE_REF_DELETION; i < REFTABLE_NR_REF_VALUETYPES; i++) {
		struct reftable_record in = {
			.type = BLOCK_TYPE_REF,
			.u.ref.value_type = i,
		};
		struct reftable_record out = { .type = BLOCK_TYPE_REF };
		struct reftable_buf key = REFTABLE_BUF_INIT;
		uint8_t buffer[1024] = { 0 };
		struct string_view dest = {
			.buf = buffer,
			.len = sizeof(buffer),
		};
		int n, m;

		in.u.ref.value_type = i;
		switch (i) {
		case REFTABLE_REF_DELETION:
			break;
		case REFTABLE_REF_VAL1:
			set_hash(in.u.ref.value.val1, 1);
			break;
		case REFTABLE_REF_VAL2:
			set_hash(in.u.ref.value.val2.value, 1);
			set_hash(in.u.ref.value.val2.target_value, 2);
			break;
		case REFTABLE_REF_SYMREF:
			in.u.ref.value.symref = xstrdup("target");
			break;
		}
		in.u.ref.refname = xstrdup("refs/heads/master");

		t_copy(&in);

		check_int(reftable_record_val_type(&in), ==, i);
		check_int(reftable_record_is_deletion(&in), ==, i == REFTABLE_REF_DELETION);

		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
		check_int(n, >, 0);

		/* decode into a non-zero reftable_record to test for leaks. */
		m = reftable_record_decode(&out, key, i, dest, GIT_SHA1_RAWSZ, &scratch);
		check_int(n, ==, m);

		check(reftable_ref_record_equal(&in.u.ref, &out.u.ref,
						 GIT_SHA1_RAWSZ));
		reftable_record_release(&in);

		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

static void t_reftable_log_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/master",
			.u.log.update_index = 42,
		},
		{
			.type = BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/master",
			.u.log.update_index = 22,
		},
		{
			.type = BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/main",
			.u.log.update_index = 22,
		},
	};

	check(!reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_equal(&in[1], &in[2], GIT_SHA1_RAWSZ));
	check_int(reftable_record_cmp(&in[1], &in[2]), >, 0);
	/* comparison should be reversed for equal keys, because
	 * comparison is now performed on the basis of update indices */
	check_int(reftable_record_cmp(&in[0], &in[1]), <, 0);

	in[1].u.log.update_index = in[0].u.log.update_index;
	check(reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));
}

static void t_reftable_log_record_compare_key(void)
{
	struct reftable_log_record logs[3] = {
		{
			.refname = (char *) "refs/heads/a",
			.update_index = 1,
		},
		{
			.refname = (char *) "refs/heads/b",
			.update_index = 2,
		},
		{
			.refname = (char *) "refs/heads/a",
			.update_index = 3,
		},
	};

	check_int(reftable_log_record_compare_key(&logs[0], &logs[1]), <, 0);
	check_int(reftable_log_record_compare_key(&logs[1], &logs[0]), >, 0);

	logs[1].update_index = logs[0].update_index;
	check_int(reftable_log_record_compare_key(&logs[0], &logs[1]), <, 0);

	check_int(reftable_log_record_compare_key(&logs[0], &logs[2]), >, 0);
	check_int(reftable_log_record_compare_key(&logs[2], &logs[0]), <, 0);
	logs[2].update_index = logs[0].update_index;
	check_int(reftable_log_record_compare_key(&logs[0], &logs[2]), ==, 0);
}

static void t_reftable_log_record_roundtrip(void)
{
	struct reftable_log_record in[] = {
		{
			.refname = xstrdup("refs/heads/master"),
			.update_index = 42,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = {
				.update = {
					.name = xstrdup("han-wen"),
					.email = xstrdup("hanwen@google.com"),
					.message = xstrdup("test"),
					.time = 1577123507,
					.tz_offset = 100,
				},
			}
		},
		{
			.refname = xstrdup("refs/heads/master"),
			.update_index = 22,
			.value_type = REFTABLE_LOG_DELETION,
		},
		{
			.refname = xstrdup("branch"),
			.update_index = 33,
			.value_type = REFTABLE_LOG_UPDATE,
		}
	};
	struct reftable_buf scratch = REFTABLE_BUF_INIT;
	set_hash(in[0].value.update.new_hash, 1);
	set_hash(in[0].value.update.old_hash, 2);
	set_hash(in[2].value.update.new_hash, 3);
	set_hash(in[2].value.update.old_hash, 4);

	check(!reftable_log_record_is_deletion(&in[0]));
	check(reftable_log_record_is_deletion(&in[1]));
	check(!reftable_log_record_is_deletion(&in[2]));

	for (size_t i = 0; i < ARRAY_SIZE(in); i++) {
		struct reftable_record rec = { .type = BLOCK_TYPE_LOG };
		struct reftable_buf key = REFTABLE_BUF_INIT;
		uint8_t buffer[1024] = { 0 };
		struct string_view dest = {
			.buf = buffer,
			.len = sizeof(buffer),
		};
		/* populate out, to check for leaks. */
		struct reftable_record out = {
			.type = BLOCK_TYPE_LOG,
			.u.log = {
				.refname = xstrdup("old name"),
				.value_type = REFTABLE_LOG_UPDATE,
				.value = {
					.update = {
						.name = xstrdup("old name"),
						.email = xstrdup("old@email"),
						.message = xstrdup("old message"),
					},
				},
			},
		};
		int n, m, valtype;

		rec.u.log = in[i];

		t_copy(&rec);

		reftable_record_key(&rec, &key);

		n = reftable_record_encode(&rec, dest, GIT_SHA1_RAWSZ);
		check_int(n, >=, 0);
		valtype = reftable_record_val_type(&rec);
		m = reftable_record_decode(&out, key, valtype, dest,
					   GIT_SHA1_RAWSZ, &scratch);
		check_int(n, ==, m);

		check(reftable_log_record_equal(&in[i], &out.u.log,
						 GIT_SHA1_RAWSZ));
		reftable_log_record_release(&in[i]);
		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

static void t_key_roundtrip(void)
{
	uint8_t buffer[1024] = { 0 };
	struct string_view dest = {
		.buf = buffer,
		.len = sizeof(buffer),
	};
	struct reftable_buf last_key = REFTABLE_BUF_INIT;
	struct reftable_buf key = REFTABLE_BUF_INIT;
	struct reftable_buf roundtrip = REFTABLE_BUF_INIT;
	int restart;
	uint8_t extra;
	int n, m;
	uint8_t rt_extra;

	check(!reftable_buf_addstr(&last_key, "refs/heads/master"));
	check(!reftable_buf_addstr(&key, "refs/tags/bla"));
	extra = 6;
	n = reftable_encode_key(&restart, dest, last_key, key, extra);
	check(!restart);
	check_int(n, >, 0);

	check(!reftable_buf_addstr(&roundtrip, "refs/heads/master"));
	m = reftable_decode_key(&roundtrip, &rt_extra, dest);
	check_int(n, ==, m);
	check(!reftable_buf_cmp(&key, &roundtrip));
	check_int(rt_extra, ==, extra);

	reftable_buf_release(&last_key);
	reftable_buf_release(&key);
	reftable_buf_release(&roundtrip);
}

static void t_reftable_obj_record_comparison(void)
{

	uint8_t id_bytes[] = { 0, 1, 2, 3, 4, 5, 6 };
	uint64_t offsets[] = { 0, 16, 32, 48, 64, 80, 96, 112};
	struct reftable_record in[3] = {
		{
			.type = BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 7,
			.u.obj.offsets = offsets,
			.u.obj.offset_len = 8,
		},
		{
			.type = BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 7,
			.u.obj.offsets = offsets,
			.u.obj.offset_len = 5,
		},
		{
			.type = BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 5,
		},
	};

	check(!reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));

	check(!reftable_record_equal(&in[1], &in[2], GIT_SHA1_RAWSZ));
	check_int(reftable_record_cmp(&in[1], &in[2]), >, 0);

	in[1].u.obj.offset_len = in[0].u.obj.offset_len;
	check(reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));
}

static void t_reftable_obj_record_roundtrip(void)
{
	uint8_t testHash1[GIT_SHA1_RAWSZ] = { 1, 2, 3, 4, 0 };
	uint64_t till9[] = { 1, 2, 3, 4, 500, 600, 700, 800, 9000 };
	struct reftable_obj_record recs[3] = {
		{
			.hash_prefix = testHash1,
			.hash_prefix_len = 5,
			.offsets = till9,
			.offset_len = 3,
		},
		{
			.hash_prefix = testHash1,
			.hash_prefix_len = 5,
			.offsets = till9,
			.offset_len = 9,
		},
		{
			.hash_prefix = testHash1,
			.hash_prefix_len = 5,
		},
	};
	struct reftable_buf scratch = REFTABLE_BUF_INIT;

	for (size_t i = 0; i < ARRAY_SIZE(recs); i++) {
		uint8_t buffer[1024] = { 0 };
		struct string_view dest = {
			.buf = buffer,
			.len = sizeof(buffer),
		};
		struct reftable_record in = {
			.type = BLOCK_TYPE_OBJ,
			.u = {
				.obj = recs[i],
			},
		};
		struct reftable_buf key = REFTABLE_BUF_INIT;
		struct reftable_record out = { .type = BLOCK_TYPE_OBJ };
		int n, m;
		uint8_t extra;

		check(!reftable_record_is_deletion(&in));
		t_copy(&in);
		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
		check_int(n, >, 0);
		extra = reftable_record_val_type(&in);
		m = reftable_record_decode(&out, key, extra, dest,
					   GIT_SHA1_RAWSZ, &scratch);
		check_int(n, ==, m);

		check(reftable_record_equal(&in, &out, GIT_SHA1_RAWSZ));
		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

static void t_reftable_index_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = BLOCK_TYPE_INDEX,
			.u.idx.offset = 22,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
		{
			.type = BLOCK_TYPE_INDEX,
			.u.idx.offset = 32,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
		{
			.type = BLOCK_TYPE_INDEX,
			.u.idx.offset = 32,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
	};
	check(!reftable_buf_addstr(&in[0].u.idx.last_key, "refs/heads/master"));
	check(!reftable_buf_addstr(&in[1].u.idx.last_key, "refs/heads/master"));
	check(!reftable_buf_addstr(&in[2].u.idx.last_key, "refs/heads/branch"));

	check(!reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));

	check(!reftable_record_equal(&in[1], &in[2], GIT_SHA1_RAWSZ));
	check_int(reftable_record_cmp(&in[1], &in[2]), >, 0);

	in[1].u.idx.offset = in[0].u.idx.offset;
	check(reftable_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	check(!reftable_record_cmp(&in[0], &in[1]));

	for (size_t i = 0; i < ARRAY_SIZE(in); i++)
		reftable_record_release(&in[i]);
}

static void t_reftable_index_record_roundtrip(void)
{
	struct reftable_record in = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = {
			.offset = 42,
			.last_key = REFTABLE_BUF_INIT,
		},
	};
	uint8_t buffer[1024] = { 0 };
	struct string_view dest = {
		.buf = buffer,
		.len = sizeof(buffer),
	};
	struct reftable_buf scratch = REFTABLE_BUF_INIT;
	struct reftable_buf key = REFTABLE_BUF_INIT;
	struct reftable_record out = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = REFTABLE_BUF_INIT },
	};
	int n, m;
	uint8_t extra;

	check(!reftable_buf_addstr(&in.u.idx.last_key, "refs/heads/master"));
	reftable_record_key(&in, &key);
	t_copy(&in);

	check(!reftable_record_is_deletion(&in));
	check(!reftable_buf_cmp(&key, &in.u.idx.last_key));
	n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
	check_int(n, >, 0);

	extra = reftable_record_val_type(&in);
	m = reftable_record_decode(&out, key, extra, dest, GIT_SHA1_RAWSZ,
				   &scratch);
	check_int(m, ==, n);

	check(reftable_record_equal(&in, &out, GIT_SHA1_RAWSZ));

	reftable_record_release(&out);
	reftable_buf_release(&key);
	reftable_buf_release(&scratch);
	reftable_buf_release(&in.u.idx.last_key);
}

int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	TEST(t_reftable_ref_record_comparison(), "comparison operations work on ref record");
	TEST(t_reftable_log_record_comparison(), "comparison operations work on log record");
	TEST(t_reftable_index_record_comparison(), "comparison operations work on index record");
	TEST(t_reftable_obj_record_comparison(), "comparison operations work on obj record");
	TEST(t_reftable_ref_record_compare_name(), "reftable_ref_record_compare_name works");
	TEST(t_reftable_log_record_compare_key(), "reftable_log_record_compare_key works");
	TEST(t_reftable_log_record_roundtrip(), "record operations work on log record");
	TEST(t_reftable_ref_record_roundtrip(), "record operations work on ref record");
	TEST(t_varint_roundtrip(), "put_var_int and get_var_int work");
	TEST(t_key_roundtrip(), "reftable_encode_key and reftable_decode_key work");
	TEST(t_reftable_obj_record_roundtrip(), "record operations work on obj record");
	TEST(t_reftable_index_record_roundtrip(), "record operations work on index record");

	return test_done();
}
