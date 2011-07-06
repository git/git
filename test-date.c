#include "cache.h"

static const char *usage_msg = "\n"
"  test-date show [time_t]...\n"
"  test-date parse [date]...\n"
"  test-date approxidate [date]...\n";

static void show_dates(char **argv, struct timeval *now)
{
	char buf[128];

	for (; *argv; argv++) {
		time_t t = atoi(*argv);
		show_date_relative(t, 0, now, buf, sizeof(buf));
		printf("%s -> %s\n", *argv, buf);
	}
}

static void parse_dates(char **argv, struct timeval *now)
{
	for (; *argv; argv++) {
		char result[100];
		unsigned long t;
		int tz;

		result[0] = 0;
		parse_date(*argv, result, sizeof(result));
		if (sscanf(result, "%lu %d", &t, &tz) == 2)
			printf("%s -> %s\n",
			       *argv, show_date(t, tz, DATE_ISO8601));
		else
			printf("%s -> bad\n", *argv);
	}
}

static void parse_approxidate(char **argv, struct timeval *now)
{
	for (; *argv; argv++) {
		time_t t;
		t = approxidate_relative(*argv, now);
		printf("%s -> %s\n", *argv, show_date(t, 0, DATE_ISO8601));
	}
}

int main(int argc, char **argv)
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
	if (!strcmp(*argv, "show"))
		show_dates(argv+1, &now);
	else if (!strcmp(*argv, "parse"))
		parse_dates(argv+1, &now);
	else if (!strcmp(*argv, "approxidate"))
		parse_approxidate(argv+1, &now);
	else
		usage(usage_msg);
	return 0;
}
