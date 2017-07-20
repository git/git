#include "test-tool.h"
#include "git-compat-util.h"
#include "strbuf.h"
#include "gettext.h"
#include "parse-options.h"
#include "utf8.h"

int cmd__iconv(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	char *from = NULL, *to = NULL, *p;
	size_t len;
	int ret = 0;
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
	if (write(1, p, len) < 0)
		ret = !!error_errno("Could not write %"PRIuMAX" bytes",
				    (uintmax_t)len);

	strbuf_release(&buf);
	free(p);

	return ret;
}
