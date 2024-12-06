#include "test-lib.h"
#include "lib-reftable.h"
#include "reftable/blocksource.h"
#include "reftable/reader.h"

static int t_reader_seek_once(void)
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
	int ret;

	t_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records), NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	ret = reftable_reader_new(&reader, &source, "name");
	check(!ret);

	reftable_reader_init_ref_iterator(reader, &it);
	ret = reftable_iterator_seek_ref(&it, "");
	check(!ret);
	ret = reftable_iterator_next_ref(&it, &ref);
	check(!ret);

	ret = reftable_ref_record_equal(&ref, &records[0], REFTABLE_HASH_SIZE_SHA1);
	check_int(ret, ==, 1);

	ret = reftable_iterator_next_ref(&it, &ref);
	check_int(ret, ==, 1);

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	reftable_buf_release(&buf);
	return 0;
}

static int t_reader_reseek(void)
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
	int ret;

	t_reftable_write_to_buf(&buf, records, ARRAY_SIZE(records), NULL, 0, NULL);
	block_source_from_buf(&source, &buf);

	ret = reftable_reader_new(&reader, &source, "name");
	check(!ret);

	reftable_reader_init_ref_iterator(reader, &it);

	for (size_t i = 0; i < 5; i++) {
		ret = reftable_iterator_seek_ref(&it, "");
		check(!ret);
		ret = reftable_iterator_next_ref(&it, &ref);
		check(!ret);

		ret = reftable_ref_record_equal(&ref, &records[0], REFTABLE_HASH_SIZE_SHA1);
		check_int(ret, ==, 1);

		ret = reftable_iterator_next_ref(&it, &ref);
		check_int(ret, ==, 1);
	}

	reftable_ref_record_release(&ref);
	reftable_iterator_destroy(&it);
	reftable_reader_decref(reader);
	reftable_buf_release(&buf);
	return 0;
}

int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	TEST(t_reader_seek_once(), "reader can seek once");
	TEST(t_reader_reseek(), "reader can reseek multiple times");
	return test_done();
}
