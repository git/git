/*
 * test-touch.c: variation on /usr/bin/touch to speed up tests
 * with a large number of files (primarily on Windows where child
 * process are very, very expensive).
 */

#include "test-tool.h"
#include "cache.h"
#include "parse-options.h"

static char *seq_pattern;
static int seq_start = 1;
static int seq_count = 1;

static int do_touch_one(const char *path)
{
	int fd;

	if (!utime(path, NULL))
		return 0;

	if (errno != ENOENT) {
		warning_errno("could not touch '%s'", path);
		return 0;
	}

	fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		warning_errno("could not create '%s'", path);
		return 0;
	}
	close(fd);

	return 0;
}

/*
 * Touch a series of files.  We assume that any required subdirs
 * already exist.  This function allows us to replace the following
 * test script fragment:
 *
 *    for i in $(test_seq 1 10000); do touch 10000_files/$i; done &&
 *
 * with a single process:
 *
 *    test-tool touch sequence --pattern="10000_files/%d" --start=1 --count=10000
 *
 * which is much faster on Windows.
 */
static int do_sequence(void)
{
	struct strbuf buf = STRBUF_INIT;
	int k;

	for (k = seq_start; k < seq_start + seq_count; k++) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, seq_pattern, k);

		if (do_touch_one(buf.buf))
			return 1;
	}

	return 0;
}

/*
 * Read a list of pathnames from stdin and touch them.  We assume that
 * any required subdirs already exist.
 */
static int do_stdin(void)
{
	struct strbuf buf = STRBUF_INIT;

	while (strbuf_getline(&buf, stdin) != EOF && buf.len)
		if (do_touch_one(buf.buf))
			return 1;

	return 0;
}

int cmd__touch(int argc, const char **argv)
{
	const char *touch_usage[] = {
		N_("test-tool touch sequence <pattern> <start> <count>"),
		N_("test-tool touch stdin"),
		NULL,
	};

	struct option touch_options[] = {
		OPT_GROUP(N_("sequence")),
		OPT_STRING(0, "pattern", &seq_pattern, N_("format"),
			   N_("sequence pathname pattern")),
		OPT_INTEGER(0, "start", &seq_start,
			    N_("sequence starting value")),
		OPT_INTEGER(0, "count", &seq_count,
			    N_("sequence count")),
		OPT_END()
	};

	const char *subcmd;

	if (argc < 2)
		usage_with_options(touch_usage, touch_options);
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(touch_usage, touch_options);

	subcmd = argv[1];
	argv--;
	argc++;

	argc = parse_options(argc, argv, NULL, touch_options, touch_usage, 0);

	if (!strcmp(subcmd, "sequence")) {
		if (!seq_pattern || !strstr(seq_pattern, "%d"))
			die("invalid sequence pattern");
		if (seq_count < 1)
			die("invalid sequence count: %d", seq_count);
		return !!do_sequence();
	}

	if (!strcmp(subcmd, "stdin")) {
		return !!do_stdin();
	}

	die("Unhandled subcommand: '%s'", subcmd);
}
