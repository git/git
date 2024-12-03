/*
 * Example implementation for the Git filter protocol version 2
 * See Documentation/gitattributes.txt, section "Filter Protocol"
 *
 * Usage: test-tool rot13-filter [--always-delay] --log=<path> <capabilities>
 *
 * Log path defines a debug log file that the script writes to. The
 * subsequent arguments define a list of supported protocol capabilities
 * ("clean", "smudge", etc).
 *
 * When --always-delay is given all pathnames with the "can-delay" flag
 * that don't appear on the list below are delayed with a count of 1
 * (see more below).
 *
 * This implementation supports special test cases:
 * (1) If data with the pathname "clean-write-fail.r" is processed with
 *     a "clean" operation then the write operation will die.
 * (2) If data with the pathname "smudge-write-fail.r" is processed with
 *     a "smudge" operation then the write operation will die.
 * (3) If data with the pathname "error.r" is processed with any
 *     operation then the filter signals that it cannot or does not want
 *     to process the file.
 * (4) If data with the pathname "abort.r" is processed with any
 *     operation then the filter signals that it cannot or does not want
 *     to process the file and any file after that is processed with the
 *     same command.
 * (5) If data with a pathname that is a key in the delay hash is
 *     requested (e.g. "test-delay10.a") then the filter responds with
 *     a "delay" status and sets the "requested" field in the delay hash.
 *     The filter will signal the availability of this object after
 *     "count" (field in delay hash) "list_available_blobs" commands.
 * (6) If data with the pathname "missing-delay.a" is processed that the
 *     filter will drop the path from the "list_available_blobs" response.
 * (7) If data with the pathname "invalid-delay.a" is processed that the
 *     filter will add the path "unfiltered" which was not delayed before
 *     to the "list_available_blobs" response.
 */

#include "test-tool.h"
#include "pkt-line.h"
#include "string-list.h"
#include "strmap.h"
#include "parse-options.h"

static FILE *logfile;
static int always_delay, has_clean_cap, has_smudge_cap;
static struct strmap delay = STRMAP_INIT;

static inline const char *str_or_null(const char *str)
{
	return str ? str : "(null)";
}

static char *rot13(char *str)
{
	char *c;
	for (c = str; *c; c++)
		if (isalpha(*c))
			*c += tolower(*c) < 'n' ? 13 : -13;
	return str;
}

static char *get_value(char *buf, const char *key)
{
	const char *orig_buf = buf;
	if (!buf ||
	    !skip_prefix((const char *)buf, key, (const char **)&buf) ||
	    !skip_prefix((const char *)buf, "=", (const char **)&buf) ||
	    !*buf)
		die("expected key '%s', got '%s'", key, str_or_null(orig_buf));
	return buf;
}

/*
 * Read a text packet, expecting that it is in the form "key=value" for
 * the given key. An EOF does not trigger any error and is reported
 * back to the caller with NULL. Die if the "key" part of "key=value" does
 * not match the given key, or the value part is empty.
 */
static char *packet_key_val_read(const char *key)
{
	char *buf;
	if (packet_read_line_gently(0, NULL, &buf) < 0)
		return NULL;
	return xstrdup(get_value(buf, key));
}

static inline void assert_remote_capability(struct strset *caps, const char *cap)
{
	if (!strset_contains(caps, cap))
		die("required '%s' capability not available from remote", cap);
}

static void read_capabilities(struct strset *remote_caps)
{
	for (;;) {
		char *buf = packet_read_line(0, NULL);
		if (!buf)
			break;
		strset_add(remote_caps, get_value(buf, "capability"));
	}

	assert_remote_capability(remote_caps, "clean");
	assert_remote_capability(remote_caps, "smudge");
	assert_remote_capability(remote_caps, "delay");
}

static void check_and_write_capabilities(struct strset *remote_caps,
					 const char **caps, int nr_caps)
{
	int i;
	for (i = 0; i < nr_caps; i++) {
		if (!strset_contains(remote_caps, caps[i]))
			die("our capability '%s' is not available from remote",
			    caps[i]);
		packet_write_fmt(1, "capability=%s\n", caps[i]);
	}
	packet_flush(1);
}

struct delay_entry {
	int requested, count;
	char *output;
};

static void free_delay_entries(void)
{
	struct hashmap_iter iter;
	struct strmap_entry *ent;

	strmap_for_each_entry(&delay, &iter, ent) {
		struct delay_entry *delay_entry = ent->value;
		free(delay_entry->output);
		free(delay_entry);
	}
	strmap_clear(&delay, 0);
}

static void add_delay_entry(const char *pathname, int count, int requested)
{
	struct delay_entry *entry = xcalloc(1, sizeof(*entry));
	entry->count = count;
	entry->requested = requested;
	if (strmap_put(&delay, pathname, entry))
		BUG("adding the same path twice to delay hash?");
}

static void reply_list_available_blobs_cmd(void)
{
	struct hashmap_iter iter;
	struct strmap_entry *ent;
	struct string_list_item *str_item;
	struct string_list paths = STRING_LIST_INIT_NODUP;

	/* flush */
	if (packet_read_line(0, NULL))
		die("bad list_available_blobs end");

	strmap_for_each_entry(&delay, &iter, ent) {
		struct delay_entry *delay_entry = ent->value;
		if (!delay_entry->requested)
			continue;
		delay_entry->count--;
		if (!strcmp(ent->key, "invalid-delay.a")) {
			/* Send Git a pathname that was not delayed earlier */
			packet_write_fmt(1, "pathname=unfiltered");
		}
		if (!strcmp(ent->key, "missing-delay.a")) {
			/* Do not signal Git that this file is available */
		} else if (!delay_entry->count) {
			string_list_append(&paths, ent->key);
			packet_write_fmt(1, "pathname=%s", ent->key);
		}
	}

	/* Print paths in sorted order. */
	string_list_sort(&paths);
	for_each_string_list_item(str_item, &paths)
		fprintf(logfile, " %s", str_item->string);
	string_list_clear(&paths, 0);

	packet_flush(1);

	fprintf(logfile, " [OK]\n");
	packet_write_fmt(1, "status=success");
	packet_flush(1);
}

static void command_loop(void)
{
	for (;;) {
		char *buf;
		const char *output;
		char *pathname;
		struct delay_entry *entry;
		struct strbuf input = STRBUF_INIT;
		char *command = packet_key_val_read("command");

		if (!command) {
			fprintf(logfile, "STOP\n");
			break;
		}
		fprintf(logfile, "IN: %s", command);

		if (!strcmp(command, "list_available_blobs")) {
			reply_list_available_blobs_cmd();
			free(command);
			continue;
		}

		pathname = packet_key_val_read("pathname");
		if (!pathname)
			die("unexpected EOF while expecting pathname");
		fprintf(logfile, " %s", pathname);

		/* Read until flush */
		while ((buf = packet_read_line(0, NULL))) {
			if (!strcmp(buf, "can-delay=1")) {
				entry = strmap_get(&delay, pathname);
				if (entry && !entry->requested)
					entry->requested = 1;
				else if (!entry && always_delay)
					add_delay_entry(pathname, 1, 1);
			} else if (starts_with(buf, "ref=") ||
				   starts_with(buf, "treeish=") ||
				   starts_with(buf, "blob=")) {
				fprintf(logfile, " %s", buf);
			} else {
				/*
				 * In general, filters need to be graceful about
				 * new metadata, since it's documented that we
				 * can pass any key-value pairs, but for tests,
				 * let's be a little stricter.
				 */
				die("Unknown message '%s'", buf);
			}
		}

		read_packetized_to_strbuf(0, &input, 0);
		fprintf(logfile, " %"PRIuMAX" [OK] -- ", (uintmax_t)input.len);

		entry = strmap_get(&delay, pathname);
		if (entry && entry->output) {
			output = entry->output;
		} else if (!strcmp(pathname, "error.r") || !strcmp(pathname, "abort.r")) {
			output = "";
		} else if (!strcmp(command, "clean") && has_clean_cap) {
			output = rot13(input.buf);
		} else if (!strcmp(command, "smudge") && has_smudge_cap) {
			output = rot13(input.buf);
		} else {
			die("bad command '%s'", command);
		}

		if (!strcmp(pathname, "error.r")) {
			fprintf(logfile, "[ERROR]\n");
			packet_write_fmt(1, "status=error");
			packet_flush(1);
		} else if (!strcmp(pathname, "abort.r")) {
			fprintf(logfile, "[ABORT]\n");
			packet_write_fmt(1, "status=abort");
			packet_flush(1);
		} else if (!strcmp(command, "smudge") &&
			   (entry = strmap_get(&delay, pathname)) &&
			   entry->requested == 1) {
			fprintf(logfile, "[DELAYED]\n");
			packet_write_fmt(1, "status=delayed");
			packet_flush(1);
			entry->requested = 2;
			if (entry->output != output) {
				free(entry->output);
				entry->output = xstrdup(output);
			}
		} else {
			int i, nr_packets = 0;
			size_t output_len;
			const char *p;
			packet_write_fmt(1, "status=success");
			packet_flush(1);

			if (skip_prefix(pathname, command, &p) &&
			    !strcmp(p, "-write-fail.r")) {
				fprintf(logfile, "[WRITE FAIL]\n");
				die("%s write error", command);
			}

			output_len = strlen(output);
			fprintf(logfile, "OUT: %"PRIuMAX" ", (uintmax_t)output_len);

			if (write_packetized_from_buf_no_flush_count(output,
				output_len, 1, &nr_packets))
				die("failed to write buffer to stdout");
			packet_flush(1);

			for (i = 0; i < nr_packets; i++)
				fprintf(logfile, ".");
			fprintf(logfile, " [OK]\n");

			packet_flush(1);
		}
		free(pathname);
		strbuf_release(&input);
		free(command);
	}
}

static void packet_initialize(void)
{
	char *pkt_buf = packet_read_line(0, NULL);

	if (!pkt_buf || strcmp(pkt_buf, "git-filter-client"))
		die("bad initialize: '%s'", str_or_null(pkt_buf));

	pkt_buf = packet_read_line(0, NULL);
	if (!pkt_buf || strcmp(pkt_buf, "version=2"))
		die("bad version: '%s'", str_or_null(pkt_buf));

	pkt_buf = packet_read_line(0, NULL);
	if (pkt_buf)
		die("bad version end: '%s'", pkt_buf);

	packet_write_fmt(1, "git-filter-server");
	packet_write_fmt(1, "version=2");
	packet_flush(1);
}

static const char *rot13_usage[] = {
	"test-tool rot13-filter [--always-delay] --log=<path> <capabilities>",
	NULL
};

int cmd__rot13_filter(int argc, const char **argv)
{
	int i, nr_caps;
	struct strset remote_caps = STRSET_INIT;
	const char *log_path = NULL;

	struct option options[] = {
		OPT_BOOL(0, "always-delay", &always_delay,
			 "delay all paths with the can-delay flag"),
		OPT_STRING(0, "log", &log_path, "path",
			   "path to the debug log file"),
		OPT_END()
	};
	nr_caps = parse_options(argc, argv, NULL, options, rot13_usage,
				PARSE_OPT_STOP_AT_NON_OPTION);

	if (!log_path || !nr_caps)
		usage_with_options(rot13_usage, options);

	logfile = fopen(log_path, "a");
	if (!logfile)
		die_errno("failed to open log file");

	for (i = 0; i < nr_caps; i++) {
		if (!strcmp(argv[i], "smudge"))
			has_smudge_cap = 1;
		if (!strcmp(argv[i], "clean"))
			has_clean_cap = 1;
	}

	add_delay_entry("test-delay10.a", 1, 0);
	add_delay_entry("test-delay11.a", 1, 0);
	add_delay_entry("test-delay20.a", 2, 0);
	add_delay_entry("test-delay10.b", 1, 0);
	add_delay_entry("missing-delay.a", 1, 0);
	add_delay_entry("invalid-delay.a", 1, 0);

	fprintf(logfile, "START\n");
	packet_initialize();

	read_capabilities(&remote_caps);
	check_and_write_capabilities(&remote_caps, argv, nr_caps);
	fprintf(logfile, "init handshake complete\n");
	strset_clear(&remote_caps);

	command_loop();

	if (fclose(logfile))
		die_errno("error closing logfile");
	free_delay_entries();
	return 0;
}
