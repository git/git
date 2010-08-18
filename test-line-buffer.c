/*
 * test-line-buffer.c: code to exercise the svn importer's input helper
 *
 * Input format:
 *	number NL
 *	(number bytes) NL
 *	number NL
 *	...
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

int main(int argc, char *argv[])
{
	char *s;

	if (argc != 1)
		usage("test-line-buffer < input.txt");
	if (buffer_init(NULL))
		die_errno("open error");
	while ((s = buffer_read_line())) {
		s = buffer_read_string(strtouint32(s));
		fputs(s, stdout);
		fputc('\n', stdout);
		buffer_skip_bytes(1);
		if (!(s = buffer_read_line()))
			break;
		buffer_copy_bytes(strtouint32(s) + 1);
	}
	if (buffer_deinit())
		die("input error");
	if (ferror(stdout))
		die("output error");
	buffer_reset();
	return 0;
}
