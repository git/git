#include "cache.h"


static int test_isdigit(int c)
{
	return isdigit(c);
}

static int test_isspace(int c)
{
	return isspace(c);
}

static int test_isalpha(int c)
{
	return isalpha(c);
}

static int test_isalnum(int c)
{
	return isalnum(c);
}

static int test_is_glob_special(int c)
{
	return is_glob_special(c);
}

static int test_is_regex_special(int c)
{
	return is_regex_special(c);
}

#define DIGIT "0123456789"
#define LOWER "abcdefghijklmnopqrstuvwxyz"
#define UPPER "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

static const struct ctype_class {
	const char *name;
	int (*test_fn)(int);
	const char *members;
} classes[] = {
	{ "isdigit", test_isdigit, DIGIT },
	{ "isspace", test_isspace, " \n\r\t" },
	{ "isalpha", test_isalpha, LOWER UPPER },
	{ "isalnum", test_isalnum, LOWER UPPER DIGIT },
	{ "is_glob_special", test_is_glob_special, "*?[\\" },
	{ "is_regex_special", test_is_regex_special, "$()*+.?[\\^{|" },
	{ NULL }
};

static int test_class(const struct ctype_class *test)
{
	int i, rc = 0;

	for (i = 0; i < 256; i++) {
		int expected = i ? !!strchr(test->members, i) : 0;
		int actual = test->test_fn(i);

		if (actual != expected) {
			rc = 1;
			printf("%s classifies char %d (0x%02x) wrongly\n",
			       test->name, i, i);
		}
	}
	return rc;
}

int main(int argc, char **argv)
{
	const struct ctype_class *test;
	int rc = 0;

	for (test = classes; test->name; test++)
		rc |= test_class(test);

	return rc;
}
