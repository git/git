/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/basics.h"
#include "reftable/constants.h"
#include "reftable/record.h"

static void t_copy(struct reftable_record *rec)
{
	struct reftable_record copy;
	uint8_t typ;

	typ = reftable_record_type(rec);
	cl_assert_equal_i(reftable_record_init(&copy, typ), 0);
	reftable_record_copy_from(&copy, rec, REFTABLE_HASH_SIZE_SHA1);
	/* do it twice to catch memory leaks */
	reftable_record_copy_from(&copy, rec, REFTABLE_HASH_SIZE_SHA1);
	cl_assert(reftable_record_equal(rec, &copy,
					REFTABLE_HASH_SIZE_SHA1) != 0);

	reftable_record_release(&copy);
}

void test_reftable_record__varint_roundtrip(void)
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

		cl_assert(n > 0);
		out.len = n;
		n = get_var_int(&got, &out);
		cl_assert(n > 0);

		cl_assert_equal_i(got, in);
	}
}

void test_reftable_record__varint_overflow(void)
{
	unsigned char buf[] = {
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0x00,
	};
	struct string_view view = {
		.buf = buf,
		.len = sizeof(buf),
	};
	uint64_t value;
	cl_assert_equal_i(get_var_int(&value, &view), -1);
}

static void set_hash(uint8_t *h, int j)
{
	for (size_t i = 0; i < hash_size(REFTABLE_HASH_SHA1); i++)
		h[i] = (j >> i) & 0xff;
}

void test_reftable_record__ref_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = REFTABLE_BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "refs/heads/master",
			.u.ref.value_type = REFTABLE_REF_VAL1,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "refs/heads/master",
			.u.ref.value_type = REFTABLE_REF_DELETION,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_REF,
			.u.ref.refname = (char *) "HEAD",
			.u.ref.value_type = REFTABLE_REF_SYMREF,
			.u.ref.value.symref = (char *) "refs/heads/master",
		},
	};
	int cmp;

	cl_assert(reftable_record_equal(&in[0], &in[1], REFTABLE_HASH_SIZE_SHA1) == 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);

	cl_assert(reftable_record_equal(&in[1], &in[2],
					REFTABLE_HASH_SIZE_SHA1) == 0);
	cl_assert_equal_i(reftable_record_cmp(&in[1], &in[2], &cmp), 0);
	cl_assert(cmp > 0);

	in[1].u.ref.value_type = in[0].u.ref.value_type;
	cl_assert(reftable_record_equal(&in[0], &in[1],
					REFTABLE_HASH_SIZE_SHA1) != 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);
}

void test_reftable_record__ref_record_compare_name(void)
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

	cl_assert(reftable_ref_record_compare_name(&recs[0],
						   &recs[1]) < 0);
	cl_assert(reftable_ref_record_compare_name(&recs[1],
						   &recs[0]) > 0);
	cl_assert_equal_i(reftable_ref_record_compare_name(&recs[0],
							   &recs[2]), 0);
}

void test_reftable_record__ref_record_roundtrip(void)
{
	struct reftable_buf scratch = REFTABLE_BUF_INIT;

	for (int i = REFTABLE_REF_DELETION; i < REFTABLE_NR_REF_VALUETYPES; i++) {
		struct reftable_record in = {
			.type = REFTABLE_BLOCK_TYPE_REF,
			.u.ref.value_type = i,
		};
		struct reftable_record out = { .type = REFTABLE_BLOCK_TYPE_REF };
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

		cl_assert_equal_i(reftable_record_val_type(&in), i);
		cl_assert_equal_i(reftable_record_is_deletion(&in),
				  i == REFTABLE_REF_DELETION);

		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, REFTABLE_HASH_SIZE_SHA1);
		cl_assert(n > 0);

		/* decode into a non-zero reftable_record to test for leaks. */
		m = reftable_record_decode(&out, key, i, dest, REFTABLE_HASH_SIZE_SHA1, &scratch);
		cl_assert_equal_i(n, m);

		cl_assert(reftable_ref_record_equal(&in.u.ref,
						    &out.u.ref,
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_record_release(&in);

		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

void test_reftable_record__log_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = REFTABLE_BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/master",
			.u.log.update_index = 42,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/master",
			.u.log.update_index = 22,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_LOG,
			.u.log.refname = (char *) "refs/heads/main",
			.u.log.update_index = 22,
		},
	};
	int cmp;

	cl_assert_equal_i(reftable_record_equal(&in[0], &in[1],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_equal(&in[1], &in[2],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_cmp(&in[1], &in[2], &cmp), 0);
	cl_assert(cmp > 0);
	/* comparison should be reversed for equal keys, because
	 * comparison is now performed on the basis of update indices */
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(cmp < 0);

	in[1].u.log.update_index = in[0].u.log.update_index;
	cl_assert(reftable_record_equal(&in[0], &in[1],
					REFTABLE_HASH_SIZE_SHA1) != 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
}

void test_reftable_record__log_record_compare_key(void)
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

	cl_assert(reftable_log_record_compare_key(&logs[0],
						  &logs[1]) < 0);
	cl_assert(reftable_log_record_compare_key(&logs[1],
						  &logs[0]) > 0);

	logs[1].update_index = logs[0].update_index;
	cl_assert(reftable_log_record_compare_key(&logs[0],
						  &logs[1]) < 0);

	cl_assert(reftable_log_record_compare_key(&logs[0],
						  &logs[2]) > 0);
	cl_assert(reftable_log_record_compare_key(&logs[2],
						  &logs[0]) < 0);
	logs[2].update_index = logs[0].update_index;
	cl_assert_equal_i(reftable_log_record_compare_key(&logs[0], &logs[2]), 0);
}

void test_reftable_record__log_record_roundtrip(void)
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

	cl_assert_equal_i(reftable_log_record_is_deletion(&in[0]), 0);
	cl_assert(reftable_log_record_is_deletion(&in[1]) != 0);
	cl_assert_equal_i(reftable_log_record_is_deletion(&in[2]), 0);

	for (size_t i = 0; i < ARRAY_SIZE(in); i++) {
		struct reftable_record rec = { .type = REFTABLE_BLOCK_TYPE_LOG };
		struct reftable_buf key = REFTABLE_BUF_INIT;
		uint8_t buffer[1024] = { 0 };
		struct string_view dest = {
			.buf = buffer,
			.len = sizeof(buffer),
		};
		/* populate out, to check for leaks. */
		struct reftable_record out = {
			.type = REFTABLE_BLOCK_TYPE_LOG,
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

		n = reftable_record_encode(&rec, dest, REFTABLE_HASH_SIZE_SHA1);
		cl_assert(n >= 0);
		valtype = reftable_record_val_type(&rec);
		m = reftable_record_decode(&out, key, valtype, dest,
					   REFTABLE_HASH_SIZE_SHA1, &scratch);
		cl_assert_equal_i(n, m);

		cl_assert(reftable_log_record_equal(&in[i], &out.u.log,
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_log_record_release(&in[i]);
		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

void test_reftable_record__key_roundtrip(void)
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

	cl_assert_equal_i(reftable_buf_addstr(&last_key,
					      "refs/heads/master"), 0);
	cl_assert_equal_i(reftable_buf_addstr(&key,
					      "refs/tags/bla"), 0);
	extra = 6;
	n = reftable_encode_key(&restart, dest, last_key, key, extra);
	cl_assert(!restart);
	cl_assert(n > 0);

	cl_assert_equal_i(reftable_buf_addstr(&roundtrip,
					      "refs/heads/master"), 0);
	m = reftable_decode_key(&roundtrip, &rt_extra, dest);
	cl_assert_equal_i(n, m);
	cl_assert_equal_i(reftable_buf_cmp(&key, &roundtrip), 0);
	cl_assert_equal_i(rt_extra, extra);

	reftable_buf_release(&last_key);
	reftable_buf_release(&key);
	reftable_buf_release(&roundtrip);
}

void test_reftable_record__obj_record_comparison(void)
{

	uint8_t id_bytes[] = { 0, 1, 2, 3, 4, 5, 6 };
	uint64_t offsets[] = { 0, 16, 32, 48, 64, 80, 96, 112};
	struct reftable_record in[3] = {
		{
			.type = REFTABLE_BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 7,
			.u.obj.offsets = offsets,
			.u.obj.offset_len = 8,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 7,
			.u.obj.offsets = offsets,
			.u.obj.offset_len = 5,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_OBJ,
			.u.obj.hash_prefix = id_bytes,
			.u.obj.hash_prefix_len = 5,
		},
	};
	int cmp;

	cl_assert_equal_i(reftable_record_equal(&in[0], &in[1],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);

	cl_assert_equal_i(reftable_record_equal(&in[1], &in[2],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_cmp(&in[1], &in[2], &cmp), 0);
	cl_assert(cmp > 0);

	in[1].u.obj.offset_len = in[0].u.obj.offset_len;
	cl_assert(reftable_record_equal(&in[0], &in[1], REFTABLE_HASH_SIZE_SHA1) != 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);
}

void test_reftable_record__obj_record_roundtrip(void)
{
	uint8_t testHash1[REFTABLE_HASH_SIZE_SHA1] = { 1, 2, 3, 4, 0 };
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
			.type = REFTABLE_BLOCK_TYPE_OBJ,
			.u = {
				.obj = recs[i],
			},
		};
		struct reftable_buf key = REFTABLE_BUF_INIT;
		struct reftable_record out = { .type = REFTABLE_BLOCK_TYPE_OBJ };
		int n, m;
		uint8_t extra;

		cl_assert_equal_i(reftable_record_is_deletion(&in), 0);
		t_copy(&in);
		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, REFTABLE_HASH_SIZE_SHA1);
		cl_assert(n > 0);
		extra = reftable_record_val_type(&in);
		m = reftable_record_decode(&out, key, extra, dest,
					   REFTABLE_HASH_SIZE_SHA1, &scratch);
		cl_assert_equal_i(n, m);

		cl_assert(reftable_record_equal(&in, &out,
						REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_buf_release(&key);
		reftable_record_release(&out);
	}

	reftable_buf_release(&scratch);
}

void test_reftable_record__index_record_comparison(void)
{
	struct reftable_record in[3] = {
		{
			.type = REFTABLE_BLOCK_TYPE_INDEX,
			.u.idx.offset = 22,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_INDEX,
			.u.idx.offset = 32,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
		{
			.type = REFTABLE_BLOCK_TYPE_INDEX,
			.u.idx.offset = 32,
			.u.idx.last_key = REFTABLE_BUF_INIT,
		},
	};
	int cmp;

	cl_assert_equal_i(reftable_buf_addstr(&in[0].u.idx.last_key,
					      "refs/heads/master"), 0);
	cl_assert_equal_i(reftable_buf_addstr(&in[1].u.idx.last_key, "refs/heads/master"), 0);
	cl_assert(reftable_buf_addstr(&in[2].u.idx.last_key,
				      "refs/heads/branch") == 0);

	cl_assert_equal_i(reftable_record_equal(&in[0], &in[1],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);

	cl_assert_equal_i(reftable_record_equal(&in[1], &in[2],
						REFTABLE_HASH_SIZE_SHA1), 0);
	cl_assert_equal_i(reftable_record_cmp(&in[1], &in[2], &cmp), 0);
	cl_assert(cmp > 0);

	in[1].u.idx.offset = in[0].u.idx.offset;
	cl_assert(reftable_record_equal(&in[0], &in[1],
					REFTABLE_HASH_SIZE_SHA1) != 0);
	cl_assert_equal_i(reftable_record_cmp(&in[0], &in[1], &cmp), 0);
	cl_assert(!cmp);

	for (size_t i = 0; i < ARRAY_SIZE(in); i++)
		reftable_record_release(&in[i]);
}

void test_reftable_record__index_record_roundtrip(void)
{
	struct reftable_record in = {
		.type = REFTABLE_BLOCK_TYPE_INDEX,
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
		.type = REFTABLE_BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = REFTABLE_BUF_INIT },
	};
	int n, m;
	uint8_t extra;

	cl_assert_equal_i(reftable_buf_addstr(&in.u.idx.last_key,
					      "refs/heads/master"), 0);
	reftable_record_key(&in, &key);
	t_copy(&in);

	cl_assert_equal_i(reftable_record_is_deletion(&in), 0);
	cl_assert_equal_i(reftable_buf_cmp(&key, &in.u.idx.last_key), 0);
	n = reftable_record_encode(&in, dest, REFTABLE_HASH_SIZE_SHA1);
	cl_assert(n > 0);

	extra = reftable_record_val_type(&in);
	m = reftable_record_decode(&out, key, extra, dest,
				   REFTABLE_HASH_SIZE_SHA1, &scratch);
	cl_assert_equal_i(m, n);

	cl_assert(reftable_record_equal(&in, &out,
					REFTABLE_HASH_SIZE_SHA1) != 0);

	reftable_record_release(&out);
	reftable_buf_release(&key);
	reftable_buf_release(&scratch);
	reftable_buf_release(&in.u.idx.last_key);
}
