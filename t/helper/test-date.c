#include "test-tool.h"
#include "cache.h"

static const char *usage_msg = "\n"
"  test-tool date relative [time_t]...\n"
"  test-tool date show:<format> [time_t]...\n"
"  test-tool date parse [date]...\n"
"  test-tool date approxidate [date]...\n"
"  test-tool date timestamp [date]...\n"
"  test-tool date is64bit\n"
"  test-tool date time_t-is64bit\n";

static void show_relative_dates(const char **argv, struct timeval *now)
{
	struct strbuf buf = STRBUF_INIT;

	for (; *argv; argv++) {
		time_t t = atoi(*argv);
		show_date_relative(t, 0, now, &buf);
		printf("%s -> %s\n", *argv, buf.buf);
	}
	strbuf_release(&buf);
}

static void show_dates(const char **argv, const char *format)
{
	struct date_mode mode;

	parse_date_format(format, &mode);
	for (; *argv; argv++) {
		char *arg;
		timestamp_t t;
		int tz;

		/*
		 * Do not use our normal timestamp parsing here, as the point
		 * is to test the formatting code in isolation.
		 */
		t = parse_timestamp(*argv, &arg, 10);
		while (*arg == ' ')
			arg++;
		tz = atoi(arg);

		printf("%s -> %s\n", *argv, show_date(t, tz, &mode));
	}
}

static void parse_dates(const char **argv, struct timeval *now)
{
	struct strbuf result = STRBUF_INIT;

	for (; *argv; argv++) {
		timestamp_t t;
		int tz;

		strbuf_reset(&result);
		parse_date(*argv, &result);
		if (sscanf(result.buf, "%"PRItime" %d", &t, &tz) == 2)
			printf("%s -> %s\n",
			       *argv, show_date(t, tz, DATE_MODE(ISO8601)));
		else
			printf("%s -> bad\n", *argv);
	}
	strbuf_release(&result);
}

static void parse_approxidate(const char **argv, struct timeval *now)
{
	for (; *argv; argv++) {
		timestamp_t t;
		t = approxidate_relative(*argv, now);
		printf("%s -> %s\n", *argv, show_date(t, 0, DATE_MODE(ISO8601)));
	}
}

static void parse_approx_timestamp(const char **argv, struct timeval *now)
{
	for (; *argv; argv++) {
		timestamp_t t;
		t = approxidate_relative(*argv, now);
		printf("%s -> %"PRItime"\n", *argv, t);
	}
}

int cmd__date(int argc, const char **argv)
{
	struct timeval now;
	const char *x;

	x = getenv("TEST_DATE_NOW");
	if (x) {
		now.tv_sec = atoi(x);
		now.tv_usec = 0;
	}
	else
		gettimeofday(&now, NULL);

	argv++;
	if (!*argv)
		usage(usage_msg);
	if (!strcmp(*argv, "relative"))
		show_relative_dates(argv+1, &now);
	else if (skip_prefix(*argv, "show:", &x))
		show_dates(argv+1, x);
	else if (!strcmp(*argv, "parse"))
		parse_dates(argv+1, &now);
	else if (!strcmp(*argv, "approxidate"))
		parse_approxidate(argv+1, &now);
	else if (!strcmp(*argv, "timestamp"))
		parse_approx_timestamp(argv+1, &now);
	else if (!strcmp(*argv, "is64bit"))
		return sizeof(timestamp_t) == 8 ? 0 : 1;
	else if (!strcmp(*argv, "time_t-is64bit"))
		return sizeof(time_t) == 8 ? 0 : 1;
	else
		usage(usage_msg);
	return 0;
}
