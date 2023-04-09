#include "test-tool.h"

static int rc;

static void report_error(const char *class, int ch)
{
	printf("%s classifies char %d (0x%02x) wrongly\n", class, ch, ch);
	rc = 1;
}

static int is_in(const char *s, int ch)
{
	/*
	 * We can't find NUL using strchr. Accept it as the first
	 * character in the spec -- there are no empty classes.
	 */
	if (ch == '\0')
		return ch == *s;
	if (*s == '\0')
		s++;
	return !!strchr(s, ch);
}

#define TEST_CLASS(t,s) {			\
	int i;					\
	for (i = 0; i < 256; i++) {		\
		if (is_in(s, i) != t(i))	\
			report_error(#t, i);	\
	}					\
}

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

int cmd__ctype(int argc UNUSED, const char **argv UNUSED)
{
	TEST_CLASS(isdigit, DIGIT);
	TEST_CLASS(isspace, " \n\r\t");
	TEST_CLASS(isalpha, LOWER UPPER);
	TEST_CLASS(isalnum, LOWER UPPER DIGIT);
	TEST_CLASS(is_glob_special, "*?[\\");
	TEST_CLASS(is_regex_special, "$()*+.?[\\^{|");
	TEST_CLASS(is_pathspec_magic, "!\"#%&',-/:;<=>@_`~");
	TEST_CLASS(isascii, ASCII);
	TEST_CLASS(islower, LOWER);
	TEST_CLASS(isupper, UPPER);
	TEST_CLASS(iscntrl, CNTRL);
	TEST_CLASS(ispunct, PUNCT);
	TEST_CLASS(isxdigit, DIGIT "abcdefABCDEF");
	TEST_CLASS(isprint, LOWER UPPER DIGIT PUNCT " ");

	return rc;
}
