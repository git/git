/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/block.h"
#include "reftable/blocksource.h"
#include "reftable/constants.h"
#include "reftable/reftable-error.h"
#include "strbuf.h"

static int cl_reftable_write_block(struct reftable_buf *buf,
				   uint8_t block_type,
				   struct reftable_record *recs,
				   size_t nrecs)
{
	struct block_writer writer = {
		.last_key = REFTABLE_BUF_INIT,
	};
	uint8_t block[1024];
	int block_end;

	cl_must_pass(block_writer_init(&writer, block_type, block, 1024,
				       0, hash_size(REFTABLE_HASH_SHA1)));
	for (size_t i = 0; i < nrecs; i++)
		cl_must_pass(block_writer_add(&writer, &recs[i]));

	block_end = block_writer_finish(&writer);
	cl_assert(block_end > 0);

	cl_must_pass(reftable_buf_add(buf, block, block_end));

	block_writer_release(&writer);
	return block_end;
}

void test_reftable_block__read_write(void)
{
	const int header_off = 21; /* random */
	struct reftable_record recs[30];
	const size_t N = ARRAY_SIZE(recs);
	const size_t block_size = 1024;
	struct reftable_block_source source = { 0 };
	struct block_writer bw = {
		.last_key = REFTABLE_BUF_INIT,
	};
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_REF,
	};
	size_t i = 0;
	int ret;
	struct reftable_block block = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	struct reftable_buf want = REFTABLE_BUF_INIT;
	struct reftable_buf block_data = REFTABLE_BUF_INIT;

	REFTABLE_CALLOC_ARRAY(block_data.buf, block_size);
	cl_assert(block_data.buf != NULL);
	block_data.len = block_size;

	ret = block_writer_init(&bw, REFTABLE_BLOCK_TYPE_REF,
				(uint8_t *) block_data.buf, block_size,
				header_off, hash_size(REFTABLE_HASH_SHA1));
	cl_assert(!ret);

	rec.u.ref.refname = (char *) "";
	rec.u.ref.value_type = REFTABLE_REF_DELETION;
	ret = block_writer_add(&bw, &rec);
	cl_assert_equal_i(ret, REFTABLE_API_ERROR);

	for (i = 0; i < N; i++) {
		rec.u.ref.refname = xstrfmt("branch%02"PRIuMAX, (uintmax_t)i);
		rec.u.ref.value_type = REFTABLE_REF_VAL1;
		memset(rec.u.ref.value.val1, i, REFTABLE_HASH_SIZE_SHA1);

		recs[i] = rec;
		ret = block_writer_add(&bw, &rec);
		rec.u.ref.refname = NULL;
		rec.u.ref.value_type = REFTABLE_REF_DELETION;
		cl_assert_equal_i(ret, 0);
	}

	ret = block_writer_finish(&bw);
	cl_assert(ret > 0);

	block_writer_release(&bw);

	block_source_from_buf(&source ,&block_data);
	reftable_block_init(&block, &source, 0, header_off, block_size,
			    REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_REF);

	block_iter_init(&it, &block);

	for (i = 0; ; i++) {
		ret = block_iter_next(&it, &rec);
		cl_assert(ret >= 0);
		if (ret > 0) {
			cl_assert_equal_i(i, N);
			break;
		}
		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	for (i = 0; i < N; i++) {
		reftable_record_key(&recs[i], &want);

		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);

		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);

		want.len--;
		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);
		cl_assert_equal_i(reftable_record_equal(&recs[10 * (i / 10)], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	reftable_block_release(&block);
	block_iter_close(&it);
	reftable_record_release(&rec);
	reftable_buf_release(&want);
	reftable_buf_release(&block_data);
	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
}

void test_reftable_block__log_read_write(void)
{
	const int header_off = 21;
	struct reftable_record recs[30];
	const size_t N = ARRAY_SIZE(recs);
	const size_t block_size = 2048;
	struct reftable_block_source source = { 0 };
	struct block_writer bw = {
		.last_key = REFTABLE_BUF_INIT,
	};
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_LOG,
	};
	size_t i = 0;
	int ret;
	struct reftable_block block = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	struct reftable_buf want = REFTABLE_BUF_INIT;
	struct reftable_buf block_data = REFTABLE_BUF_INIT;

	REFTABLE_CALLOC_ARRAY(block_data.buf, block_size);
	cl_assert(block_data.buf != NULL);
	block_data.len = block_size;

	ret = block_writer_init(&bw, REFTABLE_BLOCK_TYPE_LOG, (uint8_t *) block_data.buf, block_size,
				header_off, hash_size(REFTABLE_HASH_SHA1));
	cl_assert(!ret);

	for (i = 0; i < N; i++) {
		rec.u.log.refname = xstrfmt("branch%02"PRIuMAX , (uintmax_t)i);
		rec.u.log.update_index = i;
		rec.u.log.value_type = REFTABLE_LOG_UPDATE;

		recs[i] = rec;
		ret = block_writer_add(&bw, &rec);
		rec.u.log.refname = NULL;
		rec.u.log.value_type = REFTABLE_LOG_DELETION;
		cl_assert_equal_i(ret, 0);
	}

	ret = block_writer_finish(&bw);
	cl_assert(ret > 0);

	block_writer_release(&bw);

	block_source_from_buf(&source, &block_data);
	reftable_block_init(&block, &source, 0, header_off, block_size,
			    REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_LOG);

	block_iter_init(&it, &block);

	for (i = 0; ; i++) {
		ret = block_iter_next(&it, &rec);
		cl_assert(ret >= 0);
		if (ret > 0) {
			cl_assert_equal_i(i, N);
			break;
		}
		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	for (i = 0; i < N; i++) {
		reftable_buf_reset(&want);
		cl_assert(reftable_buf_addstr(&want, recs[i].u.log.refname) == 0);

		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);

		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);

		want.len--;
		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);
		cl_assert_equal_i(reftable_record_equal(&recs[10 * (i / 10)], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	reftable_block_release(&block);
	block_iter_close(&it);
	reftable_record_release(&rec);
	reftable_buf_release(&want);
	reftable_buf_release(&block_data);
	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
}

void test_reftable_block__obj_read_write(void)
{
	const int header_off = 21;
	struct reftable_record recs[30];
	const size_t N = ARRAY_SIZE(recs);
	const size_t block_size = 1024;
	struct reftable_block_source source = { 0 };
	struct block_writer bw = {
		.last_key = REFTABLE_BUF_INIT,
	};
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_OBJ,
	};
	size_t i = 0;
	int ret;
	struct reftable_block block = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	struct reftable_buf want = REFTABLE_BUF_INIT;
	struct reftable_buf block_data = REFTABLE_BUF_INIT;

	REFTABLE_CALLOC_ARRAY(block_data.buf, block_size);
	cl_assert(block_data.buf != NULL);
	block_data.len = block_size;

	ret = block_writer_init(&bw, REFTABLE_BLOCK_TYPE_OBJ, (uint8_t *) block_data.buf, block_size,
				header_off, hash_size(REFTABLE_HASH_SHA1));
	cl_assert(!ret);

	for (i = 0; i < N; i++) {
		uint8_t bytes[] = { i, i + 1, i + 2, i + 3, i + 5 }, *allocated;
		DUP_ARRAY(allocated, bytes, ARRAY_SIZE(bytes));

		rec.u.obj.hash_prefix = allocated;
		rec.u.obj.hash_prefix_len = 5;

		recs[i] = rec;
		ret = block_writer_add(&bw, &rec);
		rec.u.obj.hash_prefix = NULL;
		rec.u.obj.hash_prefix_len = 0;
		cl_assert_equal_i(ret, 0);
	}

	ret = block_writer_finish(&bw);
	cl_assert(ret > 0);

	block_writer_release(&bw);

	block_source_from_buf(&source, &block_data);
	reftable_block_init(&block, &source, 0, header_off, block_size,
			    REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_OBJ);

	block_iter_init(&it, &block);

	for (i = 0; ; i++) {
		ret = block_iter_next(&it, &rec);
		cl_assert(ret >= 0);
		if (ret > 0) {
			cl_assert_equal_i(i, N);
			break;
		}
		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	for (i = 0; i < N; i++) {
		reftable_record_key(&recs[i], &want);

		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);

		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	reftable_block_release(&block);
	block_iter_close(&it);
	reftable_record_release(&rec);
	reftable_buf_release(&want);
	reftable_buf_release(&block_data);
	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
}

void test_reftable_block__ref_read_write(void)
{
	const int header_off = 21;
	struct reftable_record recs[30];
	const size_t N = ARRAY_SIZE(recs);
	const size_t block_size = 1024;
	struct reftable_block_source source = { 0 };
	struct block_writer bw = {
		.last_key = REFTABLE_BUF_INIT,
	};
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_INDEX,
		.u.idx.last_key = REFTABLE_BUF_INIT,
	};
	size_t i = 0;
	int ret;
	struct reftable_block block = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	struct reftable_buf want = REFTABLE_BUF_INIT;
	struct reftable_buf block_data = REFTABLE_BUF_INIT;

	REFTABLE_CALLOC_ARRAY(block_data.buf, block_size);
	cl_assert(block_data.buf != NULL);
	block_data.len = block_size;

	ret = block_writer_init(&bw, REFTABLE_BLOCK_TYPE_INDEX, (uint8_t *) block_data.buf, block_size,
				header_off, hash_size(REFTABLE_HASH_SHA1));
	cl_assert(!ret);

	for (i = 0; i < N; i++) {
		char buf[128];

		snprintf(buf, sizeof(buf), "branch%02"PRIuMAX, (uintmax_t)i);

		reftable_buf_init(&recs[i].u.idx.last_key);
		recs[i].type = REFTABLE_BLOCK_TYPE_INDEX;
		cl_assert(!reftable_buf_addstr(&recs[i].u.idx.last_key, buf));
		recs[i].u.idx.offset = i;

		ret = block_writer_add(&bw, &recs[i]);
		cl_assert_equal_i(ret, 0);
	}

	ret = block_writer_finish(&bw);
	cl_assert(ret > 0);

	block_writer_release(&bw);

	block_source_from_buf(&source, &block_data);
	reftable_block_init(&block, &source, 0, header_off, block_size,
			    REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_INDEX);

	block_iter_init(&it, &block);

	for (i = 0; ; i++) {
		ret = block_iter_next(&it, &rec);
		cl_assert(ret >= 0);
		if (ret > 0) {
			cl_assert_equal_i(i, N);
			break;
		}
		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	for (i = 0; i < N; i++) {
		reftable_record_key(&recs[i], &want);

		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);

		cl_assert_equal_i(reftable_record_equal(&recs[i], &rec, REFTABLE_HASH_SIZE_SHA1), 1);

		want.len--;
		ret = block_iter_seek_key(&it, &want);
		cl_assert_equal_i(ret, 0);

		ret = block_iter_next(&it, &rec);
		cl_assert_equal_i(ret, 0);
		cl_assert_equal_i(reftable_record_equal(&recs[10 * (i / 10)], &rec, REFTABLE_HASH_SIZE_SHA1), 1);
	}

	reftable_block_release(&block);
	block_iter_close(&it);
	reftable_record_release(&rec);
	reftable_buf_release(&want);
	reftable_buf_release(&block_data);
	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
}

void test_reftable_block__iterator(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_record expected_refs[20];
	struct reftable_ref_record ref = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_block block = { 0 };
	struct reftable_buf data = REFTABLE_BUF_INIT;
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(expected_refs); i++) {
		expected_refs[i] = (struct reftable_record) {
			.type = REFTABLE_BLOCK_TYPE_REF,
			.u.ref = {
				.value_type = REFTABLE_REF_VAL1,
				.refname = xstrfmt("refs/heads/branch-%02"PRIuMAX, (uintmax_t)i),
			},
		};
		memset(expected_refs[i].u.ref.value.val1, i, REFTABLE_HASH_SIZE_SHA1);
	}

	cl_reftable_write_block(&data, REFTABLE_BLOCK_TYPE_REF,
				expected_refs, ARRAY_SIZE(expected_refs));

	block_source_from_buf(&source, &data);
	reftable_block_init(&block, &source, 0, 0, data.len,
			    REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_REF);

	err = reftable_block_init_iterator(&block, &it);
	cl_assert_equal_i(err, 0);

	for (size_t i = 0; ; i++) {
		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0) {
			cl_assert_equal_i(i, ARRAY_SIZE(expected_refs));
			break;
		}
		cl_assert_equal_i(err, 0);

		cl_assert(reftable_ref_record_equal(&ref,
						    &expected_refs[i].u.ref, REFTABLE_HASH_SIZE_SHA1));
	}

	err = reftable_iterator_seek_ref(&it, "refs/heads/does-not-exist");
	cl_assert_equal_i(err, 0);
	err = reftable_iterator_next_ref(&it, &ref);
	cl_assert_equal_i(err, 1);

	err = reftable_iterator_seek_ref(&it, "refs/heads/branch-13");
	cl_assert_equal_i(err, 0);
	err = reftable_iterator_next_ref(&it, &ref);
	cl_assert_equal_i(err, 0);
	cl_assert(reftable_ref_record_equal(&ref,
					    &expected_refs[13].u.ref,REFTABLE_HASH_SIZE_SHA1));

	for (size_t i = 0; i < ARRAY_SIZE(expected_refs); i++)
		reftable_free(expected_refs[i].u.ref.refname);
	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_block_release(&block);
	reftable_buf_release(&data);
}

void test_reftable_block__corrupt_log_block_size(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_LOG,
		.u.log = {
			.refname = (char *) "refs/heads/main",
			.update_index = 1,
			.value_type = REFTABLE_LOG_UPDATE,
		},
	};
	struct reftable_block block = { 0 };
	struct reftable_buf data = REFTABLE_BUF_INIT;

	cl_reftable_write_block(&data, REFTABLE_BLOCK_TYPE_LOG, &rec, 1);

	/*
	 * Log blocks store their inflated size as a big-endian 24-bit integer
	 * right after the one-byte block type. Rewrite it to claim a size that
	 * is smaller than the block header.
	 */
	reftable_put_be24((uint8_t *) data.buf + 1, 1);

	block_source_from_buf(&source, &data);
	cl_assert_equal_i(reftable_block_init(&block, &source, 0, 0, data.len,
					      REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_LOG),
			  REFTABLE_FORMAT_ERROR);

	reftable_block_release(&block);
	reftable_buf_release(&data);
}

void test_reftable_block__corrupt_block_size(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_REF,
		.u.ref = {
			.value_type = REFTABLE_REF_VAL1,
			.refname = (char *) "refs/heads/main",
		},
	};
	struct reftable_block block = { 0 };
	struct reftable_buf data = REFTABLE_BUF_INIT;
	uint32_t block_size;
	unsigned char *p;

	cl_reftable_write_block(&data, REFTABLE_BLOCK_TYPE_REF, &rec, 1);

	/*
	 * The block size is stored as a big-endian 24-bit integer right after
	 * the one-byte block type at the start of the block. Corrupt it to
	 * claim a size that is larger than the data we actually have. Reading
	 * the restart count and restart table relative to such a bogus block
	 * size must not access out-of-bounds memory.
	 */
	p = (unsigned char *) data.buf + 1;
	block_size = reftable_get_be24(p);
	cl_assert_equal_i(block_size, 47);
	reftable_put_be24(p, block_size + 1);

	block_source_from_buf(&source, &data);
	cl_assert_equal_i(reftable_block_init(&block, &source, 0, 0, data.len,
					      REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_REF),
			  REFTABLE_FORMAT_ERROR);

	reftable_block_release(&block);
	reftable_buf_release(&data);
}

void test_reftable_block__corrupt_restart_count(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_REF,
		.u.ref = {
			.value_type = REFTABLE_REF_VAL1,
			.refname = (char *) "refs/heads/main",
		},
	};
	struct reftable_block block = { 0 };
	struct reftable_buf data = REFTABLE_BUF_INIT;
	int block_size;

	block_size = cl_reftable_write_block(&data, REFTABLE_BLOCK_TYPE_REF, &rec, 1);

	/*
	 * Corrupt the restart count to claim a bogus number of restart points.
	 * Note that this would only cause us to perform an out-of-bounds
	 * access when seeking into the block, but we want to refuse such a
	 * block outright.
	 */
	reftable_put_be16((uint8_t *) data.buf + block_size - 2, 0xffff);

	block_source_from_buf(&source, &data);
	cl_assert_equal_i(reftable_block_init(&block, &source, 0, 0, data.len,
					      REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_REF),
			  REFTABLE_FORMAT_ERROR);

	reftable_block_release(&block);
	reftable_buf_release(&data);
}

void test_reftable_block__corrupt_restart_offset(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_record rec = {
		.type = REFTABLE_BLOCK_TYPE_REF,
		.u.ref = {
			.value_type = REFTABLE_REF_VAL1,
			.refname = (char *) "refs/heads/main",
		},
	};
	struct reftable_block block = { 0 };
	struct block_iter it = BLOCK_ITER_INIT;
	struct reftable_buf want = REFTABLE_BUF_INIT;
	struct reftable_buf data = REFTABLE_BUF_INIT;

	cl_reftable_write_block(&data, REFTABLE_BLOCK_TYPE_REF, &rec, 1);

	block_source_from_buf(&source, &data);
	cl_must_pass(reftable_block_init(&block, &source, 0, 0, data.len,
					 REFTABLE_HASH_SIZE_SHA1, REFTABLE_BLOCK_TYPE_REF));

	/*
	 * Corrupt the first restart offset, stored as a big-endian 24-bit
	 * integer at the start of the restart table, to point past the end of
	 * the records section. Seeking such a block must fail gracefully.
	 */
	reftable_put_be24((uint8_t *) block.block_data.data + block.restart_off,
			  0xffffff);

	block_iter_init(&it, &block);
	cl_must_pass(reftable_buf_addstr(&want, "refs/heads/main"));
	cl_assert_equal_i(block_iter_seek_key(&it, &want), REFTABLE_FORMAT_ERROR);

	reftable_buf_release(&want);
	block_iter_close(&it);
	reftable_block_release(&block);
	reftable_buf_release(&data);
}
