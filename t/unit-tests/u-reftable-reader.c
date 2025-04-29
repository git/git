#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/blocksource.h"
#include "reftable/reader.h"

void test_reftable_reader__seek_once(void)
{
	struct reftable_ref_record records[] = {
		{
			.refname = (char *) "refs/heads/main",
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 42 },
		},
	};
	struct reftable_block_source source = { 0 };
	struct reftable_ref_record ref = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_reader *reader;
	struct reftable_buf buf = REFTABLE_BUF_INIT;

	cl_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records), NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	cl_assert(reftable_reader_new(&reader, &source, "name") == 0);


	reftable_reader_init_ref_iterator(reader, &it);
	cl_assert(reftable_iterator_seek_ref(&it, "") == 0);
	cl_assert(reftable_iterator_next_ref(&it, &ref) == 0);

	cl_assert_equal_i(reftable_ref_record_equal(&ref, &records[0],
												REFTABLE_HASH_SIZE_SHA1), 1);

	cl_assert_equal_i(reftable_iterator_next_ref(&it, &ref), 1);

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	reftable_buf_release(&buf);
}

void test_reftable_reader__reseek(void)
{
	struct reftable_ref_record records[] = {
		{
			.refname = (char *) "refs/heads/main",
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { 42 },
		},
	};
	struct reftable_block_source source = { 0 };
	struct reftable_ref_record ref = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_reader *reader;
	struct reftable_buf buf = REFTABLE_BUF_INIT;

	cl_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records), NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	cl_assert(reftable_reader_new(&reader, &source, "name") == 0);

	reftable_reader_init_ref_iterator(reader, &it);

	for (size_t i = 0; i < 5; i++) {
		cl_assert(reftable_iterator_seek_ref(&it, "") == 0);
		cl_assert(reftable_iterator_next_ref(&it, &ref) == 0);

		cl_assert_equal_i(reftable_ref_record_equal(&ref, &records[0],
													REFTABLE_HASH_SIZE_SHA1), 1);

		cl_assert_equal_i(reftable_iterator_next_ref(&it, &ref), 1);
	}

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	reftable_buf_release(&buf);
}
