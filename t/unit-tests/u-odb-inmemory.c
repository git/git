#include "unit-test.h"
#include "hex.h"
#include "odb/source-inmemory.h"
#include "odb/streaming.h"
#include "oidset.h"
#include "repository.h"
#include "strbuf.h"

#define RANDOM_OID "da39a3ee5e6b4b0d3255bfef95601890afd80709"
#define FOOBAR_OID "f6ea0495187600e7b2288c8ac19c5886383a4632"

static struct repository repo = {
	.hash_algo = &hash_algos[GIT_HASH_SHA1],
};
static struct object_database *odb;

static void cl_assert_object_info(struct odb_source_inmemory *source,
				  const struct object_id *oid,
				  enum object_type expected_type,
				  const char *expected_content)
{
	enum object_type actual_type;
	unsigned long actual_size;
	void *actual_content;
	struct object_info oi = {
		.typep = &actual_type,
		.sizep = &actual_size,
		.contentp = &actual_content,
	};

	cl_must_pass(odb_source_read_object_info(&source->base, oid, &oi, 0));
	cl_assert_equal_u(actual_size, strlen(expected_content));
	cl_assert_equal_u(actual_type, expected_type);
	cl_assert_equal_s((char *) actual_content, expected_content);

	free(actual_content);
}

void test_odb_inmemory__initialize(void)
{
	odb = odb_new(&repo, "", "");
}

void test_odb_inmemory__cleanup(void)
{
	odb_free(odb);
}

void test_odb_inmemory__new(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	cl_assert_equal_i(source->base.type, ODB_SOURCE_INMEMORY);
	odb_source_free(&source->base);
}

void test_odb_inmemory__read_missing_object(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct object_id oid;
	const char *end;

	cl_must_pass(parse_oid_hex_algop(RANDOM_OID, &oid, &end, repo.hash_algo));
	cl_must_fail(odb_source_read_object_info(&source->base, &oid, NULL, 0));

	odb_source_free(&source->base);
}

void test_odb_inmemory__read_empty_tree(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	cl_assert_object_info(source, repo.hash_algo->empty_tree, OBJ_TREE, "");
	odb_source_free(&source->base);
}

void test_odb_inmemory__read_written_object(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	const char data[] = "foobar";
	struct object_id written_oid;

	cl_must_pass(odb_source_write_object(&source->base, data, strlen(data),
					     OBJ_BLOB, &written_oid, NULL, 0));
	cl_assert_equal_s(oid_to_hex(&written_oid), FOOBAR_OID);
	cl_assert_object_info(source, &written_oid, OBJ_BLOB, "foobar");

	odb_source_free(&source->base);
}

void test_odb_inmemory__read_stream_object(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct odb_read_stream *stream;
	struct object_id written_oid;
	const char data[] = "foobar";
	char buf[3] = { 0 };

	cl_must_pass(odb_source_write_object(&source->base, data, strlen(data),
					     OBJ_BLOB, &written_oid, NULL, 0));

	cl_must_pass(odb_source_read_object_stream(&stream, &source->base,
						   &written_oid));
	cl_assert_equal_i(stream->type, OBJ_BLOB);
	cl_assert_equal_u(stream->size, 6);

	cl_assert_equal_i(odb_read_stream_read(stream, buf, 2), 2);
	cl_assert_equal_s(buf, "fo");
	cl_assert_equal_i(odb_read_stream_read(stream, buf, 2), 2);
	cl_assert_equal_s(buf, "ob");
	cl_assert_equal_i(odb_read_stream_read(stream, buf, 2), 2);
	cl_assert_equal_s(buf, "ar");
	cl_assert_equal_i(odb_read_stream_read(stream, buf, 2), 0);

	odb_read_stream_close(stream);
	odb_source_free(&source->base);
}

static int add_one_object(const struct object_id *oid,
			  struct object_info *oi UNUSED,
			  void *payload)
{
	struct oidset *actual_oids = payload;
	cl_must_pass(oidset_insert(actual_oids, oid));
	return 0;
}

void test_odb_inmemory__for_each_object(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct odb_for_each_object_options opts = { 0 };
	struct oidset expected_oids = OIDSET_INIT;
	struct oidset actual_oids = OIDSET_INIT;
	struct strbuf buf = STRBUF_INIT;

	cl_must_pass(odb_source_for_each_object(&source->base, NULL,
						add_one_object, &actual_oids, &opts));
	cl_assert_equal_u(oidset_size(&actual_oids), 0);

	for (int i = 0; i < 10; i++) {
		struct object_id written_oid;

		strbuf_reset(&buf);
		strbuf_addf(&buf, "%d", i);

		cl_must_pass(odb_source_write_object(&source->base, buf.buf, buf.len,
						     OBJ_BLOB, &written_oid, NULL, 0));
		cl_must_pass(oidset_insert(&expected_oids, &written_oid));
	}

	cl_must_pass(odb_source_for_each_object(&source->base, NULL,
						add_one_object, &actual_oids, &opts));
	cl_assert_equal_b(oidset_equal(&expected_oids, &actual_oids), true);

	odb_source_free(&source->base);
	oidset_clear(&expected_oids);
	oidset_clear(&actual_oids);
	strbuf_release(&buf);
}

static int abort_after_two_objects(const struct object_id *oid UNUSED,
				   struct object_info *oi UNUSED,
				   void *payload)
{
	unsigned *counter = payload;
	(*counter)++;
	if (*counter == 2)
		return 123;
	return 0;
}

void test_odb_inmemory__for_each_object_can_abort_iteration(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct odb_for_each_object_options opts = { 0 };
	struct object_id written_oid;
	unsigned counter = 0;

	cl_must_pass(odb_source_write_object(&source->base, "1", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));
	cl_must_pass(odb_source_write_object(&source->base, "2", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));
	cl_must_pass(odb_source_write_object(&source->base, "3", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));

	cl_assert_equal_i(odb_source_for_each_object(&source->base, NULL,
						     abort_after_two_objects,
						     &counter, &opts),
			  123);
	cl_assert_equal_u(counter, 2);

	odb_source_free(&source->base);
}

void test_odb_inmemory__count_objects(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct object_id written_oid;
	unsigned long count;

	cl_must_pass(odb_source_count_objects(&source->base, 0, &count));
	cl_assert_equal_u(count, 0);

	cl_must_pass(odb_source_write_object(&source->base, "1", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));
	cl_must_pass(odb_source_write_object(&source->base, "2", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));
	cl_must_pass(odb_source_write_object(&source->base, "3", 1,
					     OBJ_BLOB, &written_oid, NULL, 0));

	cl_must_pass(odb_source_count_objects(&source->base, 0, &count));
	cl_assert_equal_u(count, 3);

	odb_source_free(&source->base);
}

void test_odb_inmemory__find_abbrev_len(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct object_id oid1, oid2;
	unsigned abbrev_len;

	/*
	 * The two blobs we're about to write share the first 10 hex characters
	 * of their object IDs ("a09f43dc45"), so at least 11 characters are
	 * needed to tell them apart:
	 *
	 *   "368317" -> a09f43dc4562d45115583f5094640ae237df55f7
	 *   "514796" -> a09f43dc45fef837235eb7e6b1a6ca5e169a3981
	 *
	 * With only one blob written we expect a length of 4.
	 */
	cl_must_pass(odb_source_write_object(&source->base, "368317", strlen("368317"),
					     OBJ_BLOB, &oid1, NULL, 0));
	cl_must_pass(odb_source_find_abbrev_len(&source->base, &oid1, 4,
						&abbrev_len));
	cl_assert_equal_u(abbrev_len, 4);

	/*
	 * With both objects present, the shared 10-character prefix means we
	 * need at least 11 characters to uniquely identify either object.
	 */
	cl_must_pass(odb_source_write_object(&source->base, "514796", strlen("514796"),
					     OBJ_BLOB, &oid2, NULL, 0));
	cl_must_pass(odb_source_find_abbrev_len(&source->base, &oid1, 4,
						&abbrev_len));
	cl_assert_equal_u(abbrev_len, 11);

	odb_source_free(&source->base);
}

void test_odb_inmemory__freshen_object(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	struct object_id written_oid;
	struct object_id oid;
	const char *end;

	cl_must_pass(parse_oid_hex_algop(RANDOM_OID, &oid, &end, repo.hash_algo));
	cl_assert_equal_i(odb_source_freshen_object(&source->base, &oid), 0);

	cl_must_pass(odb_source_write_object(&source->base, "foobar",
					     strlen("foobar"), OBJ_BLOB,
					     &written_oid, NULL, 0));
	cl_assert_equal_i(odb_source_freshen_object(&source->base,
						    &written_oid), 1);

	odb_source_free(&source->base);
}

struct membuf_write_stream {
	struct odb_write_stream base;
	const char *buf;
	size_t offset;
	size_t size;
};

static ssize_t membuf_write_stream_read(struct odb_write_stream *stream,
					unsigned char *buf, size_t len)
{
	struct membuf_write_stream *s = container_of(stream, struct membuf_write_stream, base);
	size_t chunk_size = 2;

	if (chunk_size > len)
		chunk_size = len;
	if (chunk_size > s->size - s->offset)
		chunk_size = s->size - s->offset;

	memcpy(buf, s->buf + s->offset, chunk_size);

	s->offset += chunk_size;
	if (s->offset == s->size)
		s->base.is_finished = 1;

	return chunk_size;
}

void test_odb_inmemory__write_object_stream(void)
{
	struct odb_source_inmemory *source = odb_source_inmemory_new(odb);
	const char data[] = "foobar";
	struct membuf_write_stream stream = {
		.base.read = membuf_write_stream_read,
		.buf = data,
		.size = strlen(data),
	};
	struct object_id written_oid;

	cl_must_pass(odb_source_write_object_stream(&source->base, &stream.base,
						    strlen(data), &written_oid));
	cl_assert_equal_s(oid_to_hex(&written_oid), FOOBAR_OID);
	cl_assert_object_info(source, &written_oid, OBJ_BLOB, "foobar");

	odb_source_free(&source->base);
}
