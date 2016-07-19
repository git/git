#include "cache.h"

static const char *usage_msg = "\n"
"  test-date relative [time_t]...\n"
"  test-date show:<format> [time_t]...\n"
"  test-date parse [date]...\n"
"  test-date approxidate [date]...\n";

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
		time_t t;
		int tz;

		/*
		 * Do not use our normal timestamp parsing here, as the point
		 * is to test the formatting code in isolation.
		 */
		t = strtol(*argv, &arg, 10);
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
		unsigned long t;
		int tz;

		strbuf_reset(&result);
		parse_date(*argv, &result);
		if (sscanf(result.buf, "%lu %d", &t, &tz) == 2)
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
		time_t t;
		t = approxidate_relative(*argv, now);
		printf("%s -> %s\n", *argv, show_date(t, 0, DATE_MODE(ISO8601)));
	}
}

int cmd_main(int argc, const char **argv)
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
	else
		usage(usage_msg);
	return 0;
}
