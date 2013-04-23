#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "quote.h"
#include "pathspec.h"
#include "parse-options.h"

static int quiet, verbose, stdin_paths;
static const char * const check_ignore_usage[] = {
"git check-ignore [options] pathname...",
"git check-ignore [options] --stdin < <list-of-paths>",
NULL
};

static int null_term_line;

static const struct option check_ignore_options[] = {
	OPT__QUIET(&quiet, N_("suppress progress reporting")),
	OPT__VERBOSE(&verbose, N_("be verbose")),
	OPT_GROUP(""),
	OPT_BOOLEAN(0, "stdin", &stdin_paths,
		    N_("read file names from stdin")),
	OPT_BOOLEAN('z', NULL, &null_term_line,
		    N_("input paths are terminated by a null character")),
	OPT_END()
};

static void output_exclude(const char *path, struct exclude *exclude)
{
	char *bang  = exclude->flags & EXC_FLAG_NEGATIVE  ? "!" : "";
	char *slash = exclude->flags & EXC_FLAG_MUSTBEDIR ? "/" : "";
	if (!null_term_line) {
		if (!verbose) {
			write_name_quoted(path, stdout, '\n');
		} else {
			quote_c_style(exclude->el->src, NULL, stdout, 0);
			printf(":%d:%s%s%s\t",
			       exclude->srcpos,
			       bang, exclude->pattern, slash);
			quote_c_style(path, NULL, stdout, 0);
			fputc('\n', stdout);
		}
	} else {
		if (!verbose) {
			printf("%s%c", path, '\0');
		} else {
			printf("%s%c%d%c%s%s%s%c%s%c",
			       exclude->el->src, '\0',
			       exclude->srcpos, '\0',
			       bang, exclude->pattern, slash, '\0',
			       path, '\0');
		}
	}
}

static int check_ignore(const char *prefix, const char **pathspec)
{
	struct dir_struct dir;
	const char *path, *full_path;
	char *seen;
	int num_ignored = 0, dtype = DT_UNKNOWN, i;
	struct exclude *exclude;

	/* read_cache() is only necessary so we can watch out for submodules. */
	if (read_cache() < 0)
		die(_("index file corrupt"));

	memset(&dir, 0, sizeof(dir));
	setup_standard_excludes(&dir);

	if (!pathspec || !*pathspec) {
		if (!quiet)
			fprintf(stderr, "no pathspec given.\n");
		return 0;
	}

	/*
	 * look for pathspecs matching entries in the index, since these
	 * should not be ignored, in order to be consistent with
	 * 'git status', 'git add' etc.
	 */
	seen = find_pathspecs_matching_against_index(pathspec);
	for (i = 0; pathspec[i]; i++) {
		path = pathspec[i];
		full_path = prefix_path(prefix, prefix
					? strlen(prefix) : 0, path);
		full_path = check_path_for_gitlink(full_path);
		die_if_path_beyond_symlink(full_path, prefix);
		if (!seen[i]) {
			exclude = last_exclude_matching(&dir, full_path, &dtype);
			if (exclude) {
				if (!quiet)
					output_exclude(path, exclude);
				num_ignored++;
			}
		}
	}
	free(seen);
	clear_directory(&dir);

	return num_ignored;
}

static int check_ignore_stdin_paths(const char *prefix)
{
	struct strbuf buf, nbuf;
	char **pathspec = NULL;
	size_t nr = 0, alloc = 0;
	int line_termination = null_term_line ? 0 : '\n';
	int num_ignored;

	strbuf_init(&buf, 0);
	strbuf_init(&nbuf, 0);
	while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
		if (line_termination && buf.buf[0] == '"') {
			strbuf_reset(&nbuf);
			if (unquote_c_style(&nbuf, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &nbuf);
		}
		ALLOC_GROW(pathspec, nr + 1, alloc);
		pathspec[nr] = xcalloc(strlen(buf.buf) + 1, sizeof(*buf.buf));
		strcpy(pathspec[nr++], buf.buf);
	}
	ALLOC_GROW(pathspec, nr + 1, alloc);
	pathspec[nr] = NULL;
	num_ignored = check_ignore(prefix, (const char **)pathspec);
	maybe_flush_or_die(stdout, "attribute to stdout");
	strbuf_release(&buf);
	strbuf_release(&nbuf);
	free(pathspec);
	return num_ignored;
}

int cmd_check_ignore(int argc, const char **argv, const char *prefix)
{
	int num_ignored;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, check_ignore_options,
			     check_ignore_usage, 0);

	if (stdin_paths) {
		if (argc > 0)
			die(_("cannot specify pathnames with --stdin"));
	} else {
		if (null_term_line)
			die(_("-z only makes sense with --stdin"));
		if (argc == 0)
			die(_("no path specified"));
	}
	if (quiet) {
		if (argc > 1)
			die(_("--quiet is only valid with a single pathname"));
		if (verbose)
			die(_("cannot have both --quiet and --verbose"));
	}

	if (stdin_paths) {
		num_ignored = check_ignore_stdin_paths(prefix);
	} else {
		num_ignored = check_ignore(prefix, argv);
		maybe_flush_or_die(stdout, "ignore to stdout");
	}

	return !num_ignored;
}
