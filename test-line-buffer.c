/*
 * test-line-buffer.c: code to exercise the svn importer's input helper
 */

#include "git-compat-util.h"
#include "vcs-svn/line_buffer.h"

static uint32_t strtouint32(const char *s)
{
	char *end;
	uintmax_t n = strtoumax(s, &end, 10);
	if (*s == '\0' || *end != '\0')
		die("invalid count: %s", s);
	return (uint32_t) n;
}

static void handle_command(const char *command, const char *arg, struct line_buffer *buf)
{
	switch (*command) {
	case 'c':
		if (!prefixcmp(command, "copy ")) {
			buffer_copy_bytes(buf, strtouint32(arg) + 1);
			return;
		}
	case 'r':
		if (!prefixcmp(command, "read ")) {
			const char *s = buffer_read_string(buf, strtouint32(arg));
			printf("%s\n", s);
			buffer_skip_bytes(buf, 1);	/* consume newline */
			return;
		}
	default:
		die("unrecognized command: %s", command);
	}
}

static void handle_line(const char *line, struct line_buffer *stdin_buf)
{
	const char *arg = strchr(line, ' ');
	if (!arg)
		die("no argument in line: %s", line);
	handle_command(line, arg + 1, stdin_buf);
}

int main(int argc, char *argv[])
{
	struct line_buffer stdin_buf = LINE_BUFFER_INIT;
	char *s;

	if (argc != 1)
		usage("test-line-buffer < script");

	if (buffer_init(&stdin_buf, NULL))
		die_errno("open error");
	while ((s = buffer_read_line(&stdin_buf)))
		handle_line(s, &stdin_buf);
	if (buffer_deinit(&stdin_buf))
		die("input error");
	if (ferror(stdout))
		die("output error");
	buffer_reset(&stdin_buf);
	return 0;
}
