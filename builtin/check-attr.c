#include "builtin.h"
#include "cache.h"
#include "attr.h"
#include "quote.h"
#include "parse-options.h"

static int all_attrs;
static int cached_attrs;
static int stdin_paths;
static const char * const check_attr_usage[] = {
N_("git check-attr [-a | --all | <attr>...] [--] <pathname>..."),
N_("git check-attr --stdin [-z] [-a | --all | <attr>...]"),
NULL
};

static int nul_term_line;

static const struct option check_attr_options[] = {
	OPT_BOOL('a', "all", &all_attrs, N_("report all attributes set on file")),
	OPT_BOOL(0,  "cached", &cached_attrs, N_("use .gitattributes only from the index")),
	OPT_BOOL(0 , "stdin", &stdin_paths, N_("read file names from stdin")),
	OPT_BOOL('z', NULL, &nul_term_line,
		 N_("terminate input and output records by a NUL character")),
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

		if (nul_term_line) {
			printf("%s%c" /* path */
			       "%s%c" /* attrname */
			       "%s%c" /* attrvalue */,
			       file, 0, git_attr_name(check[j].attr), 0, value, 0);
		} else {
			quote_c_style(file, NULL, stdout, 0);
			printf(": %s: %s\n", git_attr_name(check[j].attr), value);
		}

	}
}

static void check_attr(const char *prefix, int cnt,
	struct git_attr_check *check, const char *file)
{
	char *full_path =
		prefix_path(prefix, prefix ? strlen(prefix) : 0, file);
	if (check != NULL) {
		if (git_check_attr(full_path, cnt, check))
			die("git_check_attr died");
		output_attr(cnt, check, file);
	} else {
		if (git_all_attrs(full_path, &cnt, &check))
			die("git_all_attrs died");
		output_attr(cnt, check, file);
		free(check);
	}
	free(full_path);
}

static void check_attr_stdin_paths(const char *prefix, int cnt,
	struct git_attr_check *check)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf unquoted = STRBUF_INIT;
	strbuf_getline_fn getline_fn;

	getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;
	while (getline_fn(&buf, stdin) != EOF) {
		if (!nul_term_line && buf.buf[0] == '"') {
			strbuf_reset(&unquoted);
			if (unquote_c_style(&unquoted, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &unquoted);
		}
		check_attr(prefix, cnt, check, buf.buf);
		maybe_flush_or_die(stdout, "attribute to stdout");
	}
	strbuf_release(&buf);
	strbuf_release(&unquoted);
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

	if (!is_bare_repository())
		setup_work_tree();

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, check_attr_options,
			     check_attr_usage, PARSE_OPT_KEEP_DASHDASH);

	if (read_cache() < 0) {
		die("invalid cache");
	}

	if (cached_attrs)
		git_attr_set_direction(GIT_ATTR_INDEX, NULL);

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
		check_attr_stdin_paths(prefix, cnt, check);
	else {
		for (i = filei; i < argc; i++)
			check_attr(prefix, cnt, check, argv[i]);
		maybe_flush_or_die(stdout, "attribute to stdout");
	}
	return 0;
}
