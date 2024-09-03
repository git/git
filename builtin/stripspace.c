#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "parse-options.h"
#include "setup.h"
#include "strbuf.h"
#include "write-or-die.h"

static void comment_lines(struct strbuf *buf)
{
	char *msg;
	size_t len;

	msg = strbuf_detach(buf, &len);
	strbuf_add_commented_lines(buf, msg, len, comment_line_str);
	free(msg);
}

static const char * const stripspace_usage[] = {
	"git stripspace [-s | --strip-comments]",
	"git stripspace [-c | --comment-lines]",
	NULL
};

enum stripspace_mode {
	STRIP_DEFAULT = 0,
	STRIP_COMMENTS,
	COMMENT_LINES
};

int cmd_stripspace(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo UNUSED)
{
	struct strbuf buf = STRBUF_INIT;
	enum stripspace_mode mode = STRIP_DEFAULT;
	int nongit;

	const struct option options[] = {
		OPT_CMDMODE('s', "strip-comments", &mode,
			    N_("skip and remove all lines starting with comment character"),
			    STRIP_COMMENTS),
		OPT_CMDMODE('c', "comment-lines", &mode,
			    N_("prepend comment character and space to each line"),
			    COMMENT_LINES),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, stripspace_usage, 0);
	if (argc)
		usage_with_options(stripspace_usage, options);

	if (mode == STRIP_COMMENTS || mode == COMMENT_LINES) {
		setup_git_directory_gently(&nongit);
		git_config(git_default_config, NULL);
	}

	if (strbuf_read(&buf, 0, 1024) < 0)
		die_errno("could not read the input");

	if (mode == STRIP_DEFAULT || mode == STRIP_COMMENTS)
		strbuf_stripspace(&buf,
			  mode == STRIP_COMMENTS ? comment_line_str : NULL);
	else
		comment_lines(&buf);

	write_or_die(1, buf.buf, buf.len);
	strbuf_release(&buf);
	return 0;
}
