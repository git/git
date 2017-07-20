#include "git-compat-util.h"
#include "strbuf.h"
#include "gettext.h"
#include "parse-options.h"
#include "utf8.h"

static const char * const test_helper_usage[] = {
	N_("test-helper [<options>]"),
	NULL
};

static int cmp(int argc, const char **argv)
{
	FILE *f0, *f1;
	struct strbuf b0 = STRBUF_INIT, b1 = STRBUF_INIT;

	if (argc != 3)
		die("Require exactly 2 arguments, got %d", argc);

	if (!(f0 = !strcmp(argv[1], "-") ? stdin : fopen(argv[1], "r")))
		return error_errno("could not open '%s'", argv[1]);
	if (!(f1 = !strcmp(argv[2], "-") ? stdin : fopen(argv[2], "r"))) {
		fclose(f0);
		return error_errno("could not open '%s'", argv[2]);
	}

	for (;;) {
		int r0 = strbuf_getline(&b0, f0);
		int r1 = strbuf_getline(&b1, f1);

		if (r0 == EOF) {
			fclose(f0);
			fclose(f1);
			strbuf_release(&b0);
			strbuf_release(&b1);
			if (r1 == EOF)
				return 0;
			return 1;
		}
		if (r1 == EOF || strbuf_cmp(&b0, &b1)) {
			fclose(f0);
			fclose(f1);
			strbuf_release(&b0);
			strbuf_release(&b1);
			return 1;
		}
	}
}

static int iconv_(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	char *from = NULL, *to = NULL, *p;
	int len, ret;
	const char * const iconv_usage[] = {
		N_("test-helper --iconv [<options>]"),
		NULL
	};
	struct option options[] = {
		OPT_STRING('f', "from-code", &from, "encoding", "from"),
		OPT_STRING('t', "to-code", &to, "encoding", "to"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options,
			iconv_usage, 0);

	if (argc > 1 || !from || !to)
		usage_with_options(iconv_usage, options);

	if (!argc) {
		if (strbuf_read(&buf, 0, 2048) < 0)
			die_errno("Could not read from stdin");
	} else if (strbuf_read_file(&buf, argv[0], 2048) < 0)
		die_errno("Could not read from '%s'", argv[0]);

	p = reencode_string_len(buf.buf, buf.len, to, from, &len);
	if (!p)
		die_errno("Could not reencode");
	ret = write(1, p, len);

	strbuf_release(&buf);
	free(p);

	return ret;
}

int cmd_main(int argc, const char **argv)
{
	enum mode {
		CMP = 1, ICONV
	} command = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "cmp", &command,
			N_("compare files (ignoring LF vs CR/LF)"), CMP),
		OPT_CMDMODE(0, "iconv", &command,
			N_("act as drop-in replacement for `iconv`"), ICONV),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options,
			test_helper_usage,
			PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN);

	if (command == CMP)
		return !!cmp(argc, argv);
	if (command == ICONV)
		return !!iconv_(argc, argv);

	die("unhandled mode");
}

