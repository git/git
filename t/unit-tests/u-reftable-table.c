#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/blocksource.h"
#include "reftable/constants.h"
#include "reftable/iter.h"
#include "reftable/table.h"
#include "strbuf.h"

void test_reftable_table__seek_once(void)
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
	struct reftable_table *table;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int ret;

	cl_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records), NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	ret = reftable_table_new(&table, &source, "name");
	cl_assert(!ret);

	reftable_table_init_ref_iterator(table, &it);
	ret = reftable_iterator_seek_ref(&it, "");
	cl_assert(!ret);
	ret = reftable_iterator_next_ref(&it, &ref);
	cl_assert(!ret);

	ret = reftable_ref_record_equal(&ref, &records[0],
					REFTABLE_HASH_SIZE_SHA1);
	cl_assert_equal_i(ret, 1);

	ret = reftable_iterator_next_ref(&it, &ref);
	cl_assert_equal_i(ret, 1);

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
}

void test_reftable_table__reseek(void)
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
	struct reftable_table *table;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	int ret;

	cl_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records),
				 NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	ret = reftable_table_new(&table, &source, "name");
	cl_assert(!ret);

	reftable_table_init_ref_iterator(table, &it);

	for (size_t i = 0; i < 5; i++) {
		ret = reftable_iterator_seek_ref(&it, "");
		cl_assert(!ret);
		ret = reftable_iterator_next_ref(&it, &ref);
		cl_assert(!ret);

		ret = reftable_ref_record_equal(&ref, &records[0], REFTABLE_HASH_SIZE_SHA1);
		cl_assert_equal_i(ret, 1);

		ret = reftable_iterator_next_ref(&it, &ref);
		cl_assert_equal_i(ret, 1);
	}

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
}

void test_reftable_table__block_iterator(void)
{
	struct reftable_block_source source = { 0 };
	struct reftable_table_iterator it = { 0 };
	struct reftable_ref_record *records;
	const struct reftable_block *block;
	struct reftable_table *table;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct {
		uint8_t block_type;
		uint16_t header_off;
		uint16_t restart_count;
		uint16_t record_count;
	} expected_blocks[] = {
		{
			.block_type = REFTABLE_BLOCK_TYPE_REF,
			.header_off = 24,
			.restart_count = 10,
			.record_count = 158,
		},
		{
			.block_type = REFTABLE_BLOCK_TYPE_REF,
			.restart_count = 10,
			.record_count = 159,
		},
		{
			.block_type = REFTABLE_BLOCK_TYPE_REF,
			.restart_count = 10,
			.record_count = 159,
		},
		{
			.block_type = REFTABLE_BLOCK_TYPE_REF,
			.restart_count = 2,
			.record_count = 24,
		},
		{
			.block_type = REFTABLE_BLOCK_TYPE_INDEX,
			.restart_count = 1,
			.record_count = 4,
		},
		{
			.block_type = REFTABLE_BLOCK_TYPE_OBJ,
			.restart_count = 1,
			.record_count = 1,
		},
	};
	const size_t nrecords = 500;
	int ret;

	REFTABLE_CALLOC_ARRAY(records, nrecords);
	for (size_t i = 0; i < nrecords; i++) {
		records[i].value_type = REFTABLE_REF_VAL1;
		records[i].refname = xstrfmt("refs/heads/branch-%03"PRIuMAX,
					     (uintmax_t) i);
	}

	cl_reftable_write_to_buf(&buf, records, nrecords, NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	ret = reftable_table_new(&table, &source, "name");
	cl_assert(!ret);

	ret = reftable_table_iterator_init(&it, table);
	cl_assert(!ret);

	for (size_t i = 0; i < ARRAY_SIZE(expected_blocks); i++) {
		struct reftable_iterator record_it = { 0 };
		struct reftable_record record = {
			.type = expected_blocks[i].block_type,
		};

		ret = reftable_table_iterator_next(&it, &block);
		cl_assert(!ret);

		cl_assert_equal_i(block->block_type,
				  expected_blocks[i].block_type);
		cl_assert_equal_i(block->header_off,
				  expected_blocks[i].header_off);
		cl_assert_equal_i(block->restart_count,
				  expected_blocks[i].restart_count);

		ret = reftable_block_init_iterator(block, &record_it);
		cl_assert(!ret);

		for (size_t j = 0; ; j++) {
			ret = iterator_next(&record_it, &record);
			if (ret > 0) {
				cl_assert_equal_i(j,
						  expected_blocks[i].record_count);
				break;
			}
			cl_assert(!ret);
		}

		reftable_iterator_destroy(&record_it);
		reftable_record_release(&record);
	}

	ret = reftable_table_iterator_next(&it, &block);
	cl_assert_equal_i(ret, 1);

	for (size_t i = 0; i < nrecords; i++)
		reftable_free(records[i].refname);
	reftable_table_iterator_release(&it);
	reftable_table_decref(table);
	reftable_buf_release(&buf);
	reftable_free(records);
}
