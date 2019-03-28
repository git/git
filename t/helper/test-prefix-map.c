#include "test-tool.h"
#include "cache.h"
#include "prefix-map.h"

static size_t test_count, failed_count;

static void check(int succeeded, const char *file, size_t line_no,
		  const char *fmt, ...)
{
	va_list ap;

	test_count++;
	if (succeeded)
		return;

	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: ", file, (int)line_no);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);

	failed_count++;
}

#define EXPECT_SIZE_T_EQUALS(expect, actual, hint) \
	check(expect == actual, __FILE__, __LINE__, \
	      "size_t's do not match: %" \
	      PRIdMAX " != %" PRIdMAX " (%s) (%s)", \
	      (intmax_t)expect, (intmax_t)actual, #actual, hint)

int cmd__prefix_map(int argc, const char **argv)
{
#define NR 5
	struct prefix_item items[NR] = {
		{ "unique" },
		{ "hell" },
		{ "hello" },
		{ "wok" },
		{ "world" },
	};
	struct prefix_item *list[NR] = {
		items, items + 1, items + 2, items + 3, items + 4
	};

	find_unique_prefixes(list, NR, 1, 3);

#define EXPECT_PREFIX_LENGTH_EQUALS(expect, index) \
	EXPECT_SIZE_T_EQUALS(expect, list[index]->prefix_length, \
			     list[index]->name)

	EXPECT_PREFIX_LENGTH_EQUALS(1, 0);
	EXPECT_PREFIX_LENGTH_EQUALS(0, 1);
	EXPECT_PREFIX_LENGTH_EQUALS(0, 2);
	EXPECT_PREFIX_LENGTH_EQUALS(3, 3);
	EXPECT_PREFIX_LENGTH_EQUALS(3, 4);

	return !!failed_count;
}
