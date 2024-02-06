/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "block.h"

#include "system.h"
#include "blocksource.h"
#include "basics.h"
#include "constants.h"
#include "record.h"
#include "test_framework.h"
#include "reftable-tests.h"

static void test_block_read_write(void)
{
	const int header_off = 21; /* random */
	char *names[30];
	const int N = ARRAY_SIZE(names);
	const int block_size = 1024;
	struct reftable_block block = { NULL };
	struct block_writer bw = {
		.last_key = STRBUF_INIT,
	};
	struct reftable_record rec = {
		.type = BLOCK_TYPE_REF,
	};
	int i = 0;
	int n;
	struct block_reader br = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	int j = 0;
	struct strbuf want = STRBUF_INIT;

	REFTABLE_CALLOC_ARRAY(block.data, block_size);
	block.len = block_size;
	block.source = malloc_block_source();
	block_writer_init(&bw, BLOCK_TYPE_REF, block.data, block_size,
			  header_off, hash_size(GIT_SHA1_FORMAT_ID));

	rec.u.ref.refname = "";
	rec.u.ref.value_type = REFTABLE_REF_DELETION;
	n = block_writer_add(&bw, &rec);
	EXPECT(n == REFTABLE_API_ERROR);

	for (i = 0; i < N; i++) {
		char name[100];
		snprintf(name, sizeof(name), "branch%02d", i);

		rec.u.ref.refname = name;
		rec.u.ref.value_type = REFTABLE_REF_VAL1;
		memset(rec.u.ref.value.val1, i, GIT_SHA1_RAWSZ);

		names[i] = xstrdup(name);
		n = block_writer_add(&bw, &rec);
		rec.u.ref.refname = NULL;
		rec.u.ref.value_type = REFTABLE_REF_DELETION;
		EXPECT(n == 0);
	}

	n = block_writer_finish(&bw);
	EXPECT(n > 0);

	block_writer_release(&bw);

	block_reader_init(&br, &block, header_off, block_size, GIT_SHA1_RAWSZ);

	block_reader_start(&br, &it);

	while (1) {
		int r = block_iter_next(&it, &rec);
		EXPECT(r >= 0);
		if (r > 0) {
			break;
		}
		EXPECT_STREQ(names[j], rec.u.ref.refname);
		j++;
	}

	reftable_record_release(&rec);
	block_iter_close(&it);

	for (i = 0; i < N; i++) {
		struct block_iter it = BLOCK_ITER_INIT;
		strbuf_reset(&want);
		strbuf_addstr(&want, names[i]);

		n = block_reader_seek(&br, &it, &want);
		EXPECT(n == 0);

		n = block_iter_next(&it, &rec);
		EXPECT(n == 0);

		EXPECT_STREQ(names[i], rec.u.ref.refname);

		want.len--;
		n = block_reader_seek(&br, &it, &want);
		EXPECT(n == 0);

		n = block_iter_next(&it, &rec);
		EXPECT(n == 0);
		EXPECT_STREQ(names[10 * (i / 10)], rec.u.ref.refname);

		block_iter_close(&it);
	}

	reftable_record_release(&rec);
	reftable_block_done(&br.block);
	strbuf_release(&want);
	for (i = 0; i < N; i++) {
		reftable_free(names[i]);
	}
}

int block_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_block_read_write);
	return 0;
}
