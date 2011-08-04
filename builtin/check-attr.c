#include "builtin.h"
#include "cache.h"
#include "attr.h"
#include "quote.h"
#include "parse-options.h"

static int stdin_paths;
static const char * const check_attr_usage[] = {
"git check-attr attr... [--] pathname...",
"git check-attr --stdin attr... < <list-of-paths>",
NULL
};

static int null_term_line;

static const struct option check_attr_options[] = {
	OPT_BOOLEAN(0 , "stdin", &stdin_paths, "read file names from stdin"),
	OPT_BOOLEAN('z', NULL, &null_term_line,
		"input paths are terminated by a null character"),
	OPT_END()
};

static void output_attr(int cnt, struct git_attr_check *check,
	const char *file)
{
	int j;
	for (j = 0; j < cnt; j++) {
		const char *value = check[j].value;

		if (ATTR_TRUE(value))
			value = "set";
		else if (ATTR_FALSE(value))
			value = "unset";
		else if (ATTR_UNSET(value))
			value = "unspecified";

		quote_c_style(file, NULL, stdout, 0);
		printf(": %s: %s\n", git_attr_name(check[j].attr), value);
	}
}

static void check_attr(int cnt, struct git_attr_check *check,
	const char *file)
{
	if (git_checkattr(file, cnt, check))
		die("git_checkattr died");
	output_attr(cnt, check, file);
}

static void check_attr_stdin_paths(int cnt, struct git_attr_check *check)
{
	struct strbuf buf, nbuf;
	int line_termination = null_term_line ? 0 : '\n';

	strbuf_init(&buf, 0);
	strbuf_init(&nbuf, 0);
	while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
		if (line_termination && buf.buf[0] == '"') {
			strbuf_reset(&nbuf);
			if (unquote_c_style(&nbuf, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &nbuf);
		}
		check_attr(cnt, check, buf.buf);
		maybe_flush_or_die(stdout, "attribute to stdout");
	}
	strbuf_release(&buf);
	strbuf_release(&nbuf);
}

int cmd_check_attr(int argc, const char **argv, const char *prefix)
{
	struct git_attr_check *check;
	int cnt, i, doubledash;
	const char *errstr = NULL;

	argc = parse_options(argc, argv, prefix, check_attr_options,
			     check_attr_usage, PARSE_OPT_KEEP_DASHDASH);
	if (!argc)
		usage_with_options(check_attr_usage, check_attr_options);

	if (read_cache() < 0) {
		die("invalid cache");
	}

	doubledash = -1;
	for (i = 0; doubledash < 0 && i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			doubledash = i;
	}

	/* If there is no double dash, we handle only one attribute */
	if (doubledash < 0) {
		cnt = 1;
		doubledash = 0;
	} else
		cnt = doubledash;
	doubledash++;

	if (cnt <= 0)
		errstr = "No attribute specified";
	else if (stdin_paths && doubledash < argc)
		errstr = "Can't specify files with --stdin";
	if (errstr) {
		error("%s", errstr);
		usage_with_options(check_attr_usage, check_attr_options);
	}

	check = xcalloc(cnt, sizeof(*check));
	for (i = 0; i < cnt; i++) {
		const char *name;
		struct git_attr *a;
		name = argv[i];
		a = git_attr(name);
		if (!a)
			return error("%s: not a valid attribute name", name);
		check[i].attr = a;
	}

	if (stdin_paths)
		check_attr_stdin_paths(cnt, check);
	else {
		for (i = doubledash; i < argc; i++)
			check_attr(cnt, check, argv[i]);
		maybe_flush_or_die(stdout, "attribute to stdout");
	}
	return 0;
}
