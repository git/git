#include "unit-test.h"
#include "hex.h"
#include "strbuf.h"

static void check_hash_data(const void *data, size_t data_length,
			    const char *expected_hashes[])
{
	cl_assert(data != NULL);

	for (size_t i = 1; i < ARRAY_SIZE(hash_algos); i++) {
		struct git_hash_ctx ctx;
		unsigned char hash[GIT_MAX_HEXSZ];
		const struct git_hash_algo *algop = &hash_algos[i];

		algop->init_fn(&ctx);
		git_hash_update(&ctx, data, data_length);
		git_hash_final(hash, &ctx);

		cl_assert_equal_s(hash_to_hex_algop(hash,algop), expected_hashes[i - 1]);
	}
}

/* Works with a NUL terminated string. Doesn't work if it should contain a NUL character. */
#define TEST_HASH_STR(data, expected_sha1, expected_sha256) do { \
		const char *expected_hashes[] = { expected_sha1, expected_sha256 }; \
		check_hash_data(data, strlen(data), expected_hashes); \
	} while (0)

/* Only works with a literal string, useful when it contains a NUL character. */
#define TEST_HASH_LITERAL(literal, expected_sha1, expected_sha256) do { \
		const char *expected_hashes[] = { expected_sha1, expected_sha256 }; \
		check_hash_data(literal, (sizeof(literal) - 1), expected_hashes); \
	} while (0)

void test_hash__empty_string(void)
{
	TEST_HASH_STR("",
		"da39a3ee5e6b4b0d3255bfef95601890afd80709",
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void test_hash__single_character(void)
{
	TEST_HASH_STR("a",
		"86f7e437faa5a7fce15d1ddcb9eaeaea377667b8",
		"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
}

void test_hash__multi_character(void)
{
	TEST_HASH_STR("abc",
		"a9993e364706816aba3e25717850c26c9cd0d89d",
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_hash__message_digest(void)
{
	TEST_HASH_STR("message digest",
		"c12252ceda8be8994d5fa0290a47231c1d16aae3",
		"f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650");
}

void test_hash__alphabet(void)
{
	TEST_HASH_STR("abcdefghijklmnopqrstuvwxyz",
		"32d10c7b8cf96570ca04ce37f2a19d84240d3a89",
		"71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73");
}

void test_hash__aaaaaaaaaa_100000(void)
{
	struct strbuf aaaaaaaaaa_100000 = STRBUF_INIT;
	strbuf_addstrings(&aaaaaaaaaa_100000, "aaaaaaaaaa", 100000);
	TEST_HASH_STR(aaaaaaaaaa_100000.buf,
		"34aa973cd4c4daa4f61eeb2bdbad27316534016f",
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	strbuf_release(&aaaaaaaaaa_100000);
}

void test_hash__alphabet_100000(void)
{
	struct strbuf alphabet_100000 = STRBUF_INIT;
	strbuf_addstrings(&alphabet_100000, "abcdefghijklmnopqrstuvwxyz", 100000);
	TEST_HASH_STR(alphabet_100000.buf,
		"e7da7c55b3484fdf52aebec9cbe7b85a98f02fd4",
		"e406ba321ca712ad35a698bf0af8d61fc4dc40eca6bdcea4697962724ccbde35");
	strbuf_release(&alphabet_100000);
}

void test_hash__zero_blob_literal(void)
{
	TEST_HASH_LITERAL("blob 0\0",
		"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391",
		"473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813");
}

void test_hash__three_blob_literal(void)
{
	TEST_HASH_LITERAL("blob 3\0abc",
		"f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f",
		"c1cf6e465077930e88dc5136641d402f72a229ddd996f627d60e9639eaba35a6");
}

void test_hash__zero_tree_literal(void)
{
	TEST_HASH_LITERAL("tree 0\0",
		"4b825dc642cb6eb9a060e54bf8d69288fbee4904",
		"6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321");
}
