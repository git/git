/*
 * This program can either change modification time of the given
 * file(s) or just print it. The program does not change atime or
 * ctime (their values are explicitly preserved).
 *
 * The mtime can be changed to an absolute value:
 *
 *	test-tool chmtime =<seconds> file...
 *
 * Relative to the current time as returned by time(3):
 *
 *	test-tool chmtime =+<seconds> (or =-<seconds>) file...
 *
 * Or relative to the current mtime of the file:
 *
 *	test-tool chmtime <seconds> file...
 *	test-tool chmtime +<seconds> (or -<seconds>) file...
 *
 * Examples:
 *
 * To print the mtime and the file name use --verbose and set
 * the file mtime offset to 0:
 *
 *	test-tool chmtime -v +0 file
 *
 * To print only the mtime use --get:
 *
 *	test-tool chmtime --get file
 *
 * To set the mtime to current time:
 *
 *	test-tool chmtime =+0 file
 *
 * To set the file mtime offset to +1 and print the new value:
 *
 *	test-tool chmtime --get +1 file
 *
 */
#include "test-tool.h"
#include "git-compat-util.h"
#include "parse-options.h"
#include <utime.h>

static const char * const chmtime_usage[] = {
	"test-tool chmtime (-v|--verbose|-g|--get) (+|=|=+|=-|-)<seconds> <file>...",
	NULL
};

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
		return 0;
	}
	if ((*set_eq && *set_time < 0) || *set_eq == 2) {
		time_t now = time(NULL);
		*set_time += now;
	}
	return 1;
}

int cmd__chmtime(int argc, const char **argv)
{
	int verbose = 0;
	int get = 0;

	int i = 0;
	/* no mtime change by default */
	int set_eq = 0;
	long int set_time = 0;

	struct option options[] = {
		OPT_BOOL('v', "verbose", &verbose, "print the mtime and the file name"),
		OPT_BOOL('g', "get", &get, "print only the mtime"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, chmtime_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (argc < 1 || (!get && !verbose && argc < 2))
		usage_with_options(chmtime_usage, options);

	if (timespec_arg(argv[i], &set_time, &set_eq)) {
		++i;
	} else {
		if (get == 0 && verbose == 0) {
			fprintf(stderr, "Not a base-10 integer: %s\n", argv[i] + 1);
			usage_with_options(chmtime_usage, options);
		}
	}

	if (i == argc)
		usage_with_options(chmtime_usage, options);

	for (; i < argc; i++) {
		struct stat sb;
		struct utimbuf utb;
		uintmax_t mtime;

		if (stat(argv[i], &sb) < 0) {
			fprintf(stderr, "Failed to stat %s: %s. Skipping\n",
				argv[i], strerror(errno));
			continue;
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

		mtime = utb.modtime < 0 ? 0: utb.modtime;
		if (get) {
			printf("%"PRIuMAX"\n", mtime);
		} else if (verbose) {
			printf("%"PRIuMAX"\t%s\n", mtime, argv[i]);
		}

		if (utb.modtime != sb.st_mtime && utime(argv[i], &utb) < 0) {
#ifdef GIT_WINDOWS_NATIVE
			if (S_ISDIR(sb.st_mode)) {
				/*
				 * NEEDSWORK: The Windows version of `utime()`
				 * (aka `mingw_utime()`) does not correctly
				 * handle directory arguments, since it uses
				 * `_wopen()`.  Ignore it for now since this
				 * is just a test.
				 */
				fprintf(stderr,
					("Failed to modify time on directory %s. "
					 "Skipping\n"), argv[i]);
				continue;
			}
#endif
			fprintf(stderr, "Failed to modify time on %s: %s\n",
				argv[i], strerror(errno));
			return 1;
		}
	}

	return 0;
}
