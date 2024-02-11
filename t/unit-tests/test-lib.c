#include "test-lib.h"

enum result {
	RESULT_NONE,
	RESULT_FAILURE,
	RESULT_SKIP,
	RESULT_SUCCESS,
	RESULT_TODO
};

static struct {
	enum result result;
	int count;
	unsigned failed :1;
	unsigned lazy_plan :1;
	unsigned running :1;
	unsigned skip_all :1;
	unsigned todo :1;
} ctx = {
	.lazy_plan = 1,
	.result = RESULT_NONE,
};

/*
 * Visual C interpolates the absolute Windows path for `__FILE__`,
 * but we want to see relative paths, as verified by t0080.
 * There are other compilers that do the same, and are not for
 * Windows.
 */
#include "dir.h"

static const char *make_relative(const char *location)
{
	static char prefix[] = __FILE__, buf[PATH_MAX], *p;
	static size_t prefix_len;
	static int need_bs_to_fs = -1;

	/* one-time preparation */
	if (need_bs_to_fs < 0) {
		size_t len = strlen(prefix);
		char needle[] = "t\\unit-tests\\test-lib.c";
		size_t needle_len = strlen(needle);

		if (len < needle_len)
			die("unexpected prefix '%s'", prefix);

		/*
		 * The path could be relative (t/unit-tests/test-lib.c)
		 * or full (/home/user/git/t/unit-tests/test-lib.c).
		 * Check the slash between "t" and "unit-tests".
		 */
		prefix_len = len - needle_len;
		if (prefix[prefix_len + 1] == '/') {
			/* Oh, we're not Windows */
			for (size_t i = 0; i < needle_len; i++)
				if (needle[i] == '\\')
					needle[i] = '/';
			need_bs_to_fs = 0;
		} else {
			need_bs_to_fs = 1;
		}

		/*
		 * prefix_len == 0 if the compiler gives paths relative
		 * to the root of the working tree.  Otherwise, we want
		 * to see that we did find the needle[] at a directory
		 * boundary.  Again we rely on that needle[] begins with
		 * "t" followed by the directory separator.
		 */
		if (fspathcmp(needle, prefix + prefix_len) ||
		    (prefix_len && prefix[prefix_len - 1] != needle[1]))
			die("unexpected suffix of '%s'", prefix);
	}

	/*
	 * Does it not start with the expected prefix?
	 * Return it as-is without making it worse.
	 */
	if (prefix_len && fspathncmp(location, prefix, prefix_len))
		return location;

	/*
	 * If we do not need to munge directory separator, we can return
	 * the substring at the tail of the location.
	 */
	if (!need_bs_to_fs)
		return location + prefix_len;

	/* convert backslashes to forward slashes */
	strlcpy(buf, location + prefix_len, sizeof(buf));
	for (p = buf; *p; p++)
		if (*p == '\\')
			*p = '/';
	return buf;
}

static void msg_with_prefix(const char *prefix, const char *format, va_list ap)
{
	fflush(stderr);
	if (prefix)
		fprintf(stdout, "%s", prefix);
	vprintf(format, ap); /* TODO: handle newlines */
	putc('\n', stdout);
	fflush(stdout);
}

void test_msg(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	msg_with_prefix("# ", format, ap);
	va_end(ap);
}

void test_plan(int count)
{
	assert(!ctx.running);

	fflush(stderr);
	printf("1..%d\n", count);
	fflush(stdout);
	ctx.lazy_plan = 0;
}

int test_done(void)
{
	assert(!ctx.running);

	if (ctx.lazy_plan)
		test_plan(ctx.count);

	return ctx.failed;
}

void test_skip(const char *format, ...)
{
	va_list ap;

	assert(ctx.running);

	ctx.result = RESULT_SKIP;
	va_start(ap, format);
	if (format)
		msg_with_prefix("# skipping test - ", format, ap);
	va_end(ap);
}

void test_skip_all(const char *format, ...)
{
	va_list ap;
	const char *prefix;

	if (!ctx.count && ctx.lazy_plan) {
		/* We have not printed a test plan yet */
		prefix = "1..0 # SKIP ";
		ctx.lazy_plan = 0;
	} else {
		/* We have already printed a test plan */
		prefix = "Bail out! # ";
		ctx.failed = 1;
	}
	ctx.skip_all = 1;
	ctx.result = RESULT_SKIP;
	va_start(ap, format);
	msg_with_prefix(prefix, format, ap);
	va_end(ap);
}

int test__run_begin(void)
{
	assert(!ctx.running);

	ctx.count++;
	ctx.result = RESULT_NONE;
	ctx.running = 1;

	return ctx.skip_all;
}

static void print_description(const char *format, va_list ap)
{
	if (format) {
		fputs(" - ", stdout);
		vprintf(format, ap);
	}
}

int test__run_end(int was_run UNUSED, const char *location, const char *format, ...)
{
	va_list ap;

	assert(ctx.running);
	assert(!ctx.todo);

	fflush(stderr);
	va_start(ap, format);
	if (!ctx.skip_all) {
		switch (ctx.result) {
		case RESULT_SUCCESS:
			printf("ok %d", ctx.count);
			print_description(format, ap);
			break;

		case RESULT_FAILURE:
			printf("not ok %d", ctx.count);
			print_description(format, ap);
			break;

		case RESULT_TODO:
			printf("not ok %d", ctx.count);
			print_description(format, ap);
			printf(" # TODO");
			break;

		case RESULT_SKIP:
			printf("ok %d", ctx.count);
			print_description(format, ap);
			printf(" # SKIP");
			break;

		case RESULT_NONE:
			test_msg("BUG: test has no checks at %s",
				 make_relative(location));
			printf("not ok %d", ctx.count);
			print_description(format, ap);
			ctx.result = RESULT_FAILURE;
			break;
		}
	}
	va_end(ap);
	ctx.running = 0;
	if (ctx.skip_all)
		return 1;
	putc('\n', stdout);
	fflush(stdout);
	ctx.failed |= ctx.result == RESULT_FAILURE;

	return ctx.result != RESULT_FAILURE;
}

static void test_fail(void)
{
	assert(ctx.result != RESULT_SKIP);

	ctx.result = RESULT_FAILURE;
}

static void test_pass(void)
{
	assert(ctx.result != RESULT_SKIP);

	if (ctx.result == RESULT_NONE)
		ctx.result = RESULT_SUCCESS;
}

static void test_todo(void)
{
	assert(ctx.result != RESULT_SKIP);

	if (ctx.result != RESULT_FAILURE)
		ctx.result = RESULT_TODO;
}

int test_assert(const char *location, const char *check, int ok)
{
	assert(ctx.running);

	if (ctx.result == RESULT_SKIP) {
		test_msg("skipping check '%s' at %s", check,
			 make_relative(location));
		return 1;
	}
	if (!ctx.todo) {
		if (ok) {
			test_pass();
		} else {
			test_msg("check \"%s\" failed at %s", check,
				 make_relative(location));
			test_fail();
		}
	}

	return !!ok;
}

void test__todo_begin(void)
{
	assert(ctx.running);
	assert(!ctx.todo);

	ctx.todo = 1;
}

int test__todo_end(const char *location, const char *check, int res)
{
	assert(ctx.running);
	assert(ctx.todo);

	ctx.todo = 0;
	if (ctx.result == RESULT_SKIP)
		return 1;
	if (res) {
		test_msg("todo check '%s' succeeded at %s", check,
			 make_relative(location));
		test_fail();
	} else {
		test_todo();
	}

	return !res;
}

int check_bool_loc(const char *loc, const char *check, int ok)
{
	return test_assert(loc, check, ok);
}

union test__tmp test__tmp[2];

int check_int_loc(const char *loc, const char *check, int ok,
		  intmax_t a, intmax_t b)
{
	int ret = test_assert(loc, check, ok);

	if (!ret) {
		test_msg("   left: %"PRIdMAX, a);
		test_msg("  right: %"PRIdMAX, b);
	}

	return ret;
}

int check_uint_loc(const char *loc, const char *check, int ok,
		   uintmax_t a, uintmax_t b)
{
	int ret = test_assert(loc, check, ok);

	if (!ret) {
		test_msg("   left: %"PRIuMAX, a);
		test_msg("  right: %"PRIuMAX, b);
	}

	return ret;
}

static void print_one_char(char ch, char quote)
{
	if ((unsigned char)ch < 0x20u || ch == 0x7f) {
		/* TODO: improve handling of \a, \b, \f ... */
		printf("\\%03o", (unsigned char)ch);
	} else {
		if (ch == '\\' || ch == quote)
			putc('\\', stdout);
		putc(ch, stdout);
	}
}

static void print_char(const char *prefix, char ch)
{
	printf("# %s: '", prefix);
	print_one_char(ch, '\'');
	fputs("'\n", stdout);
}

int check_char_loc(const char *loc, const char *check, int ok, char a, char b)
{
	int ret = test_assert(loc, check, ok);

	if (!ret) {
		fflush(stderr);
		print_char("   left", a);
		print_char("  right", b);
		fflush(stdout);
	}

	return ret;
}

static void print_str(const char *prefix, const char *str)
{
	printf("# %s: ", prefix);
	if (!str) {
		fputs("NULL\n", stdout);
	} else {
		putc('"', stdout);
		while (*str)
			print_one_char(*str++, '"');
		fputs("\"\n", stdout);
	}
}

int check_str_loc(const char *loc, const char *check,
		  const char *a, const char *b)
{
	int ok = (!a && !b) || (a && b && !strcmp(a, b));
	int ret = test_assert(loc, check, ok);

	if (!ret) {
		fflush(stderr);
		print_str("   left", a);
		print_str("  right", b);
		fflush(stdout);
	}

	return ret;
}
