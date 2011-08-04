#include "builtin.h"
#include "cache.h"
#include "attr.h"
#include "quote.h"
#include "parse-options.h"

static int all_attrs;
static int stdin_paths;
static const char * const check_attr_usage[] = {
"git check-attr [-a | --all | attr...] [--] pathname...",
"git check-attr --stdin [-a | --all | attr...] < <list-of-paths>",
NULL
};

static int null_term_line;

static const struct option check_attr_options[] = {
	OPT_BOOLEAN('a', "all", &all_attrs, "report all attributes set on file"),
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
	if (check != NULL) {
		if (git_check_attr(file, cnt, check))
			die("git_check_attr died");
		output_attr(cnt, check, file);
	} else {
		if (git_all_attrs(file, &cnt, &check))
			die("git_all_attrs died");
		output_attr(cnt, check, file);
		free(check);
	}
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

static NORETURN void error_with_usage(const char *msg)
{
	error("%s", msg);
	usage_with_options(check_attr_usage, check_attr_options);
}

int cmd_check_attr(int argc, const char **argv, const char *prefix)
{
	struct git_attr_check *check;
	int cnt, i, doubledash, filei;

	argc = parse_options(argc, argv, prefix, check_attr_options,
			     check_attr_usage, PARSE_OPT_KEEP_DASHDASH);

	if (read_cache() < 0) {
		die("invalid cache");
	}

	doubledash = -1;
	for (i = 0; doubledash < 0 && i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			doubledash = i;
	}

	/* Process --all and/or attribute arguments: */
	if (all_attrs) {
		if (doubledash >= 1)
			error_with_usage("Attributes and --all both specified");

		cnt = 0;
		filei = doubledash + 1;
	} else if (doubledash == 0) {
		error_with_usage("No attribute specified");
	} else if (doubledash < 0) {
		if (!argc)
			error_with_usage("No attribute specified");

		if (stdin_paths) {
			/* Treat all arguments as attribute names. */
			cnt = argc;
			filei = argc;
		} else {
			/* Treat exactly one argument as an attribute name. */
			cnt = 1;
			filei = 1;
		}
	} else {
		cnt = doubledash;
		filei = doubledash + 1;
	}

	/* Check file argument(s): */
	if (stdin_paths) {
		if (filei < argc)
			error_with_usage("Can't specify files with --stdin");
	} else {
		if (filei >= argc)
			error_with_usage("No file specified");
	}

	if (all_attrs) {
		check = NULL;
	} else {
		check = xcalloc(cnt, sizeof(*check));
		for (i = 0; i < cnt; i++) {
			const char *name;
			struct git_attr *a;
			name = argv[i];
			a = git_attr(name);
			if (!a)
				return error("%s: not a valid attribute name",
					name);
			check[i].attr = a;
		}
	}

	if (stdin_paths)
		check_attr_stdin_paths(cnt, check);
	else {
		for (i = filei; i < argc; i++)
			check_attr(cnt, check, argv[i]);
		maybe_flush_or_die(stdout, "attribute to stdout");
	}
	return 0;
}
