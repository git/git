/*
 * A test helper to exercise the progress display.
 *
 * Reads instructions from standard input, one instruction per line:
 *
 *   "progress <items>" - Call display_progress() with the given item count
 *                        as parameter.
 *   "throughput <bytes> <millis> - Call display_throughput() with the given
 *                                  byte count as parameter.  The 'millis'
 *                                  specify the time elapsed since the
 *                                  start_progress() call.
 *   "update" - Set the 'progress_update' flag.
 *
 * See 't0500-progress-display.sh' for examples.
 */
#include "test-tool.h"
#include "gettext.h"
#include "parse-options.h"
#include "progress.h"
#include "strbuf.h"

/*
 * These are defined in 'progress.c', but are not exposed in 'progress.h',
 * because they are exclusively for testing.
 */
extern int progress_testing;
extern uint64_t progress_test_ns;
void progress_test_force_update(void);

int cmd__progress(int argc, const char **argv)
{
	uint64_t total = 0;
	const char *title;
	struct strbuf line = STRBUF_INIT;
	struct progress *progress;

	const char *usage[] = {
		"test-tool progress [--total=<n>] <progress-title>",
		NULL
	};
	struct option options[] = {
		OPT_INTEGER(0, "total", &total, "total number of items"),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc != 1)
		die("need a title for the progress output");
	title = argv[0];

	progress_testing = 1;
	progress = start_progress(title, total);
	while (strbuf_getline(&line, stdin) != EOF) {
		char *end;

		if (skip_prefix(line.buf, "progress ", (const char **) &end)) {
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
		} else if (!strcmp(line.buf, "update"))
			progress_test_force_update();
		else
			die("invalid input: '%s'\n", line.buf);
	}
	stop_progress(&progress);

	return 0;
}
