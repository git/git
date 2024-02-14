/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/

#include "record.h"

#include "system.h"
#include "basics.h"
#include "constants.h"
#include "test_framework.h"
#include "reftable-tests.h"

static void test_copy(struct reftable_record *rec)
{
	struct reftable_record copy;
	uint8_t typ;

	typ = reftable_record_type(rec);
	reftable_record_init(&copy, typ);
	reftable_record_copy_from(&copy, rec, GIT_SHA1_RAWSZ);
	/* do it twice to catch memory leaks */
	reftable_record_copy_from(&copy, rec, GIT_SHA1_RAWSZ);
	EXPECT(reftable_record_equal(rec, &copy, GIT_SHA1_RAWSZ));

	puts("testing print coverage:\n");
	reftable_record_print(&copy, GIT_SHA1_RAWSZ);

	reftable_record_release(&copy);
}

static void test_varint_roundtrip(void)
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
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		uint8_t dest[10];

		struct string_view out = {
			.buf = dest,
			.len = sizeof(dest),
		};
		uint64_t in = inputs[i];
		int n = put_var_int(&out, in);
		uint64_t got = 0;

		EXPECT(n > 0);
		out.len = n;
		n = get_var_int(&got, &out);
		EXPECT(n > 0);

		EXPECT(got == in);
	}
}

static void test_common_prefix(void)
{
	struct {
		const char *a, *b;
		int want;
	} cases[] = {
		{ "abc", "ab", 2 },
		{ "", "abc", 0 },
		{ "abc", "abd", 2 },
		{ "abc", "pqr", 0 },
	};

	int i = 0;
	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct strbuf a = STRBUF_INIT;
		struct strbuf b = STRBUF_INIT;
		strbuf_addstr(&a, cases[i].a);
		strbuf_addstr(&b, cases[i].b);
		EXPECT(common_prefix_size(&a, &b) == cases[i].want);

		strbuf_release(&a);
		strbuf_release(&b);
	}
}

static void set_hash(uint8_t *h, int j)
{
	int i = 0;
	for (i = 0; i < hash_size(GIT_SHA1_FORMAT_ID); i++) {
		h[i] = (j >> i) & 0xff;
	}
}

static void test_reftable_ref_record_roundtrip(void)
{
	int i = 0;

	for (i = REFTABLE_REF_DELETION; i < REFTABLE_NR_REF_VALUETYPES; i++) {
		struct reftable_record in = {
			.type = BLOCK_TYPE_REF,
		};
		struct reftable_record out = { .type = BLOCK_TYPE_REF };
		struct strbuf key = STRBUF_INIT;
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

		test_copy(&in);

		EXPECT(reftable_record_val_type(&in) == i);

		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
		EXPECT(n > 0);

		/* decode into a non-zero reftable_record to test for leaks. */
		m = reftable_record_decode(&out, key, i, dest, GIT_SHA1_RAWSZ);
		EXPECT(n == m);

		EXPECT(reftable_ref_record_equal(&in.u.ref, &out.u.ref,
						 GIT_SHA1_RAWSZ));
		reftable_record_release(&in);

		strbuf_release(&key);
		reftable_record_release(&out);
	}
}

static void test_reftable_log_record_equal(void)
{
	struct reftable_log_record in[2] = {
		{
			.refname = xstrdup("refs/heads/master"),
			.update_index = 42,
		},
		{
			.refname = xstrdup("refs/heads/master"),
			.update_index = 22,
		}
	};

	EXPECT(!reftable_log_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	in[1].update_index = in[0].update_index;
	EXPECT(reftable_log_record_equal(&in[0], &in[1], GIT_SHA1_RAWSZ));
	reftable_log_record_release(&in[0]);
	reftable_log_record_release(&in[1]);
}

static void test_reftable_log_record_roundtrip(void)
{
	int i;

	struct reftable_log_record in[] = {
		{
			.refname = xstrdup("refs/heads/master"),
			.update_index = 42,
			.value_type = REFTABLE_LOG_UPDATE,
			.value = {
				.update = {
					.old_hash = reftable_malloc(GIT_SHA1_RAWSZ),
					.new_hash = reftable_malloc(GIT_SHA1_RAWSZ),
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
			.value = {
				.update = {
					.old_hash = reftable_malloc(GIT_SHA1_RAWSZ),
					.new_hash = reftable_malloc(GIT_SHA1_RAWSZ),
					/* rest of fields left empty. */
				},
			},
		}
	};
	set_test_hash(in[0].value.update.new_hash, 1);
	set_test_hash(in[0].value.update.old_hash, 2);
	set_test_hash(in[2].value.update.new_hash, 3);
	set_test_hash(in[2].value.update.old_hash, 4);
	for (i = 0; i < ARRAY_SIZE(in); i++) {
		struct reftable_record rec = { .type = BLOCK_TYPE_LOG };
		struct strbuf key = STRBUF_INIT;
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
						.new_hash = reftable_calloc(GIT_SHA1_RAWSZ, 1),
						.old_hash = reftable_calloc(GIT_SHA1_RAWSZ, 1),
						.name = xstrdup("old name"),
						.email = xstrdup("old@email"),
						.message = xstrdup("old message"),
					},
				},
			},
		};
		int n, m, valtype;

		rec.u.log = in[i];

		test_copy(&rec);

		reftable_record_key(&rec, &key);

		n = reftable_record_encode(&rec, dest, GIT_SHA1_RAWSZ);
		EXPECT(n >= 0);
		valtype = reftable_record_val_type(&rec);
		m = reftable_record_decode(&out, key, valtype, dest,
					   GIT_SHA1_RAWSZ);
		EXPECT(n == m);

		EXPECT(reftable_log_record_equal(&in[i], &out.u.log,
						 GIT_SHA1_RAWSZ));
		reftable_log_record_release(&in[i]);
		strbuf_release(&key);
		reftable_record_release(&out);
	}
}

static void test_u24_roundtrip(void)
{
	uint32_t in = 0x112233;
	uint8_t dest[3];
	uint32_t out;
	put_be24(dest, in);
	out = get_be24(dest);
	EXPECT(in == out);
}

static void test_key_roundtrip(void)
{
	uint8_t buffer[1024] = { 0 };
	struct string_view dest = {
		.buf = buffer,
		.len = sizeof(buffer),
	};
	struct strbuf last_key = STRBUF_INIT;
	struct strbuf key = STRBUF_INIT;
	struct strbuf roundtrip = STRBUF_INIT;
	int restart;
	uint8_t extra;
	int n, m;
	uint8_t rt_extra;

	strbuf_addstr(&last_key, "refs/heads/master");
	strbuf_addstr(&key, "refs/tags/bla");
	extra = 6;
	n = reftable_encode_key(&restart, dest, last_key, key, extra);
	EXPECT(!restart);
	EXPECT(n > 0);

	m = reftable_decode_key(&roundtrip, &rt_extra, last_key, dest);
	EXPECT(n == m);
	EXPECT(0 == strbuf_cmp(&key, &roundtrip));
	EXPECT(rt_extra == extra);

	strbuf_release(&last_key);
	strbuf_release(&key);
	strbuf_release(&roundtrip);
}

static void test_reftable_obj_record_roundtrip(void)
{
	uint8_t testHash1[GIT_SHA1_RAWSZ] = { 1, 2, 3, 4, 0 };
	uint64_t till9[] = { 1, 2, 3, 4, 500, 600, 700, 800, 9000 };
	struct reftable_obj_record recs[3] = { {
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
					       } };
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(recs); i++) {
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
		struct strbuf key = STRBUF_INIT;
		struct reftable_record out = { .type = BLOCK_TYPE_OBJ };
		int n, m;
		uint8_t extra;

		test_copy(&in);
		reftable_record_key(&in, &key);
		n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
		EXPECT(n > 0);
		extra = reftable_record_val_type(&in);
		m = reftable_record_decode(&out, key, extra, dest,
					   GIT_SHA1_RAWSZ);
		EXPECT(n == m);

		EXPECT(reftable_record_equal(&in, &out, GIT_SHA1_RAWSZ));
		strbuf_release(&key);
		reftable_record_release(&out);
	}
}

static void test_reftable_index_record_roundtrip(void)
{
	struct reftable_record in = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = {
			.offset = 42,
			.last_key = STRBUF_INIT,
		},
	};
	uint8_t buffer[1024] = { 0 };
	struct string_view dest = {
		.buf = buffer,
		.len = sizeof(buffer),
	};
	struct strbuf key = STRBUF_INIT;
	struct reftable_record out = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = STRBUF_INIT },
	};
	int n, m;
	uint8_t extra;

	strbuf_addstr(&in.u.idx.last_key, "refs/heads/master");
	reftable_record_key(&in, &key);
	test_copy(&in);

	EXPECT(0 == strbuf_cmp(&key, &in.u.idx.last_key));
	n = reftable_record_encode(&in, dest, GIT_SHA1_RAWSZ);
	EXPECT(n > 0);

	extra = reftable_record_val_type(&in);
	m = reftable_record_decode(&out, key, extra, dest, GIT_SHA1_RAWSZ);
	EXPECT(m == n);

	EXPECT(reftable_record_equal(&in, &out, GIT_SHA1_RAWSZ));

	reftable_record_release(&out);
	strbuf_release(&key);
	strbuf_release(&in.u.idx.last_key);
}

int record_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_reftable_log_record_equal);
	RUN_TEST(test_reftable_log_record_roundtrip);
	RUN_TEST(test_reftable_ref_record_roundtrip);
	RUN_TEST(test_varint_roundtrip);
	RUN_TEST(test_key_roundtrip);
	RUN_TEST(test_common_prefix);
	RUN_TEST(test_reftable_obj_record_roundtrip);
	RUN_TEST(test_reftable_index_record_roundtrip);
	RUN_TEST(test_u24_roundtrip);
	return 0;
}
