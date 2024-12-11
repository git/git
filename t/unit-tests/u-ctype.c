#include "unit-test.h"

#define TEST_CHAR_CLASS(class, string) do { \
	size_t len = ARRAY_SIZE(string) - 1 + \
		BUILD_ASSERT_OR_ZERO(ARRAY_SIZE(string) > 0) + \
		BUILD_ASSERT_OR_ZERO(sizeof(string[0]) == sizeof(char)); \
	for (int i = 0; i < 256; i++) { \
		int actual = class(i), expect = !!memchr(string, i, len); \
		if (actual != expect) \
			cl_failf("0x%02x is classified incorrectly: expected %d, got %d", \
				 i, expect, actual); \
	} \
	cl_assert(!class(EOF)); \
} while (0)

#define DIGIT "0123456789"
#define LOWER "abcdefghijklmnopqrstuvwxyz"
#define UPPER "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define PUNCT "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
#define ASCII \
	"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f" \
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" \
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f" \
	"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f" \
	"\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f" \
	"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f" \
	"\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f" \
	"\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f"
#define CNTRL \
	"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f" \
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" \
	"\x7f"

void test_ctype__isspace(void)
{
	TEST_CHAR_CLASS(isspace, " \n\r\t");
}

void test_ctype__isdigit(void)
{
	TEST_CHAR_CLASS(isdigit, DIGIT);
}

void test_ctype__isalpha(void)
{
	TEST_CHAR_CLASS(isalpha, LOWER UPPER);
}

void test_ctype__isalnum(void)
{
	TEST_CHAR_CLASS(isalnum, LOWER UPPER DIGIT);
}

void test_ctype__is_glob_special(void)
{
	TEST_CHAR_CLASS(is_glob_special, "*?[\\");
}

void test_ctype__is_regex_special(void)
{
	TEST_CHAR_CLASS(is_regex_special, "$()*+.?[\\^{|");
}

void test_ctype__is_pathspec_magic(void)
{
	TEST_CHAR_CLASS(is_pathspec_magic, "!\"#%&',-/:;<=>@_`~");
}

void test_ctype__isascii(void)
{
	TEST_CHAR_CLASS(isascii, ASCII);
}

void test_ctype__islower(void)
{
	TEST_CHAR_CLASS(islower, LOWER);
}

void test_ctype__isupper(void)
{
	TEST_CHAR_CLASS(isupper, UPPER);
}

void test_ctype__iscntrl(void)
{
	TEST_CHAR_CLASS(iscntrl, CNTRL);
}

void test_ctype__ispunct(void)
{
	TEST_CHAR_CLASS(ispunct, PUNCT);
}

void test_ctype__isxdigit(void)
{
	TEST_CHAR_CLASS(isxdigit, DIGIT "abcdefABCDEF");
}

void test_ctype__isprint(void)
{
	TEST_CHAR_CLASS(isprint, LOWER UPPER DIGIT PUNCT " ");
}
