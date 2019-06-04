/*
 * test-line-buffer.c: code to exercise the svn importer's input helper
 */

#include "git-compat-util.h"
#include "strbuf.h"
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
	if (starts_with(command, "binary ")) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addch(&sb, '>');
		buffer_read_binary(buf, &sb, strtouint32(arg));
		fwrite(sb.buf, 1, sb.len, stdout);
		strbuf_release(&sb);
	} else if (starts_with(command, "copy ")) {
		buffer_copy_bytes(buf, strtouint32(arg));
	} else if (starts_with(command, "skip ")) {
		buffer_skip_bytes(buf, strtouint32(arg));
	} else {
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

int cmd_main(int argc, const char **argv)
{
	struct line_buffer stdin_buf = LINE_BUFFER_INIT;
	struct line_buffer file_buf = LINE_BUFFER_INIT;
	struct line_buffer *input = &stdin_buf;
	const char *filename;
	char *s;

	if (argc == 1)
		filename = NULL;
	else if (argc == 2)
		filename = argv[1];
	else
		usage("test-line-buffer [file | &fd] < script");

	if (buffer_init(&stdin_buf, NULL))
		die_errno("open error");
	if (filename) {
		if (*filename == '&') {
			if (buffer_fdinit(&file_buf, strtouint32(filename + 1)))
				die_errno("error opening fd %s", filename + 1);
		} else {
			if (buffer_init(&file_buf, filename))
				die_errno("error opening %s", filename);
		}
		input = &file_buf;
	}

	while ((s = buffer_read_line(&stdin_buf)))
		handle_line(s, input);

	if (filename && buffer_deinit(&file_buf))
		die("error reading from %s", filename);
	if (buffer_deinit(&stdin_buf))
		die("input error");
	if (ferror(stdout))
		die("output error");
	return 0;
}
