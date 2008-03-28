#include "git-compat-util.h"
#include <utime.h>

static const char usage_str[] = "(+|=|=+|=-|-)<seconds> <file>...";

int main(int argc, const char *argv[])
{
	int i;
	int set_eq;
	long int set_time;
	char *test;
	const char *timespec;

	if (argc < 3)
		goto usage;

	timespec = argv[1];
	set_eq = (*timespec == '=') ? 1 : 0;
	if (set_eq) {
		timespec++;
		if (*timespec == '+') {
			set_eq = 2; /* relative "in the future" */
			timespec++;
		}
	}
	set_time = strtol(timespec, &test, 10);
	if (*test) {
		fprintf(stderr, "Not a base-10 integer: %s\n", argv[1] + 1);
		goto usage;
	}
	if ((set_eq && set_time < 0) || set_eq == 2) {
		time_t now = time(NULL);
		set_time += now;
	}

	for (i = 2; i < argc; i++) {
		struct stat sb;
		struct utimbuf utb;

		if (stat(argv[i], &sb) < 0) {
			fprintf(stderr, "Failed to stat %s: %s\n",
			        argv[i], strerror(errno));
			return -1;
		}

		utb.actime = sb.st_atime;
		utb.modtime = set_eq ? set_time : sb.st_mtime + set_time;

		if (utime(argv[i], &utb) < 0) {
			fprintf(stderr, "Failed to modify time on %s: %s\n",
			        argv[i], strerror(errno));
			return -1;
		}
	}

	return 0;

usage:
	fprintf(stderr, "Usage: %s %s\n", argv[0], usage_str);
	return -1;
}
