/*
 * This program can either change modification time of the given
 * file(s) or just print it. The program does not change atime or
 * ctime (their values are explicitly preserved).
 *
 * The mtime can be changed to an absolute value:
 *
 *	test-chmtime =<seconds> file...
 *
 * Relative to the current time as returned by time(3):
 *
 *	test-chmtime =+<seconds> (or =-<seconds>) file...
 *
 * Or relative to the current mtime of the file:
 *
 *	test-chmtime <seconds> file...
 *	test-chmtime +<seconds> (or -<seconds>) file...
 *
 * Examples:
 *
 * To just print the mtime use --verbose and set the file mtime offset to 0:
 *
 *	test-chmtime -v +0 file
 *
 * To set the mtime to current time:
 *
 *	test-chmtime =+0 file
 *
 */
#include "git-compat-util.h"
#include <utime.h>

static const char usage_str[] = "-v|--verbose (+|=|=+|=-|-)<seconds> <file>...";

static int timespec_arg(const char *arg, long int *set_time, int *set_eq)
{
	char *test;
	const char *timespec = arg;
	*set_eq = (*timespec == '=') ? 1 : 0;
	if (*set_eq) {
		timespec++;
		if (*timespec == '+') {
			*set_eq = 2; /* relative "in the future" */
			timespec++;
		}
	}
	*set_time = strtol(timespec, &test, 10);
	if (*test) {
		fprintf(stderr, "Not a base-10 integer: %s\n", arg + 1);
		return 0;
	}
	if ((*set_eq && *set_time < 0) || *set_eq == 2) {
		time_t now = time(NULL);
		*set_time += now;
	}
	return 1;
}

int cmd_main(int argc, const char **argv)
{
	static int verbose;

	int i = 1;
	/* no mtime change by default */
	int set_eq = 0;
	long int set_time = 0;

	if (argc < 3)
		goto usage;

	if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
		verbose = 1;
		++i;
	}
	if (timespec_arg(argv[i], &set_time, &set_eq))
		++i;
	else
		goto usage;

	for (; i < argc; i++) {
		struct stat sb;
		struct utimbuf utb;

		if (stat(argv[i], &sb) < 0) {
			fprintf(stderr, "Failed to stat %s: %s\n",
			        argv[i], strerror(errno));
			return 1;
		}

#ifdef GIT_WINDOWS_NATIVE
		if (!(sb.st_mode & S_IWUSR) &&
				chmod(argv[i], sb.st_mode | S_IWUSR)) {
			fprintf(stderr, "Could not make user-writable %s: %s",
				argv[i], strerror(errno));
			return 1;
		}
#endif

		utb.actime = sb.st_atime;
		utb.modtime = set_eq ? set_time : sb.st_mtime + set_time;

		if (verbose) {
			uintmax_t mtime = utb.modtime < 0 ? 0: utb.modtime;
			printf("%"PRIuMAX"\t%s\n", mtime, argv[i]);
		}

		if (utb.modtime != sb.st_mtime && utime(argv[i], &utb) < 0) {
			fprintf(stderr, "Failed to modify time on %s: %s\n",
			        argv[i], strerror(errno));
			return 1;
		}
	}

	return 0;

usage:
	fprintf(stderr, "usage: %s %s\n", argv[0], usage_str);
	return 1;
}
