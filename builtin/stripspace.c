#include "builtin.h"
#include "cache.h"
#include "strbuf.h"

static void comment_lines(struct strbuf *buf)
{
	char *msg;
	size_t len;

	msg = strbuf_detach(buf, &len);
	strbuf_add_commented_lines(buf, msg, len);
	free(msg);
}

static const char *usage_msg = "\n"
"  git stripspace [-s | --strip-comments] < input\n"
"  git stripspace [-c | --comment-lines] < input";

int cmd_stripspace(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	int strip_comments = 0;
	enum { INVAL = 0, STRIP_SPACE = 1, COMMENT_LINES = 2 } mode = STRIP_SPACE;

	if (argc == 2) {
		if (!strcmp(argv[1], "-s") ||
		    !strcmp(argv[1], "--strip-comments")) {
			strip_comments = 1;
		} else if (!strcmp(argv[1], "-c") ||
			   !strcmp(argv[1], "--comment-lines")) {
			mode = COMMENT_LINES;
		} else {
			mode = INVAL;
		}
	} else if (argc > 1) {
		mode = INVAL;
	}

	if (mode == INVAL)
		usage(usage_msg);

	if (strip_comments || mode == COMMENT_LINES)
		git_config(git_default_config, NULL);

	if (strbuf_read(&buf, 0, 1024) < 0)
		die_errno("could not read the input");

	if (mode == STRIP_SPACE)
		strbuf_stripspace(&buf, strip_comments);
	else
		comment_lines(&buf);

	write_or_die(1, buf.buf, buf.len);
	strbuf_release(&buf);
	return 0;
}
