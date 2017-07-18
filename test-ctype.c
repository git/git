#include "cache.h"

static int rc;

static void report_error(const char *class, int ch)
{
	printf("%s classifies char %d (0x%02x) wrongly\n", class, ch, ch);
	rc = 1;
}

static int is_in(const char *s, int ch)
{
	/* We can't find NUL using strchr.  It's classless anyway. */
	if (ch == '\0')
		return 0;
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

int main(int argc, char **argv)
{
	TEST_CLASS(isdigit, DIGIT);
	TEST_CLASS(isspace, " \n\r\t");
	TEST_CLASS(isalpha, LOWER UPPER);
	TEST_CLASS(isalnum, LOWER UPPER DIGIT);
	TEST_CLASS(is_glob_special, "*?[\\");
	TEST_CLASS(is_regex_special, "$()*+.?[\\^{|");
	TEST_CLASS(is_pathspec_magic, "!\"#%&',-/:;<=>@_`~");

	return rc;
}
