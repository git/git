/*
 * A test helper to exercise the progress display.
 *
 * Reads instructions from standard input, one instruction per line:
 *
 *   "start[ <total>[ <title>]]" - Call start_progress(title, total),
 *                                 when "start" use a title of
 *                                 "Working hard" with a total of 0.
 *   "progress <items>" - Call display_progress() with the given item count
 *                        as parameter.
 *   "throughput <bytes> <millis> - Call display_throughput() with the given
 *                                  byte count as parameter.  The 'millis'
 *                                  specify the time elapsed since the
 *                                  start_progress() call.
 *   "update" - Set the 'progress_update' flag.
 *   "stop" - Call stop_progress().
 *
 * See 't0500-progress-display.sh' for examples.
 */
#define GIT_TEST_PROGRESS_ONLY
#include "test-tool.h"
#include "gettext.h"
#include "parse-options.h"
#include "progress.h"
#include "strbuf.h"

int cmd__progress(int argc, const char **argv)
{
	const char *default_title = "Working hard";
	char *detached_title = NULL;
	struct strbuf line = STRBUF_INIT;
	struct progress *progress = NULL;

	const char *usage[] = {
		"test-tool progress <stdin",
		NULL
	};
	struct option options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc)
		usage_with_options(usage, options);

	progress_testing = 1;
	while (strbuf_getline(&line, stdin) != EOF) {
		char *end;

		if (!strcmp(line.buf, "start")) {
			progress = start_progress(default_title, 0);
		} else if (skip_prefix(line.buf, "start ", (const char **) &end)) {
			uint64_t total = strtoull(end, &end, 10);
			if (*end == '\0') {
				progress = start_progress(default_title, total);
			} else if (*end == ' ') {
				free(detached_title);
				detached_title = strbuf_detach(&line, NULL);
				progress = start_progress(end + 1, total);
			} else {
				die("invalid input: '%s'\n", line.buf);
			}
		} else if (skip_prefix(line.buf, "progress ", (const char **) &end)) {
			uint64_t item_count = strtoull(end, &end, 10);
			if (*end != '\0')
				die("invalid input: '%s'\n", line.buf);
			display_progress(progress, item_count);
		} else if (skip_prefix(line.buf, "throughput ",
				       (const char **) &end)) {
			uint64_t byte_count, test_ms;

			byte_count = strtoull(end, &end, 10);
			if (*end != ' ')
				die("invalid input: '%s'\n", line.buf);
			test_ms = strtoull(end + 1, &end, 10);
			if (*end != '\0')
				die("invalid input: '%s'\n", line.buf);
			progress_test_ns = test_ms * 1000 * 1000;
			display_throughput(progress, byte_count);
		} else if (!strcmp(line.buf, "update")) {
			progress_test_force_update();
		} else if (!strcmp(line.buf, "stop")) {
			stop_progress(&progress);
		} else {
			die("invalid input: '%s'\n", line.buf);
		}
	}
	free(detached_title);

	return 0;
}
