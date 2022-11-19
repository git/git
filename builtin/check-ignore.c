#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "dir.h"
#include "quote.h"
#include "pathspec.h"
#include "parse-options.h"
#include "submodule.h"

static int quiet, verbose, stdin_paths, show_non_matching, no_index;
static const char * const check_ignore_usage[] = {
"git check-ignore [<options>] <pathname>...",
"git check-ignore [<options>] --stdin",
NULL
};

static int nul_term_line;

static const struct option check_ignore_options[] = {
	OPT__QUIET(&quiet, N_("suppress progress reporting")),
	OPT__VERBOSE(&verbose, N_("be verbose")),
	OPT_GROUP(""),
	OPT_BOOL(0, "stdin", &stdin_paths,
		 N_("read file names from stdin")),
	OPT_BOOL('z', NULL, &nul_term_line,
		 N_("terminate input and output records by a NUL character")),
	OPT_BOOL('n', "non-matching", &show_non_matching,
		 N_("show non-matching input paths")),
	OPT_BOOL(0, "no-index", &no_index,
		 N_("ignore index when checking")),
	OPT_END()
};

static void output_pattern(const char *path, struct path_pattern *pattern)
{
	char *bang  = (pattern && pattern->flags & PATTERN_FLAG_NEGATIVE)  ? "!" : "";
	char *slash = (pattern && pattern->flags & PATTERN_FLAG_MUSTBEDIR) ? "/" : "";
	if (!nul_term_line) {
		if (!verbose) {
			write_name_quoted(path, stdout, '\n');
		} else {
			if (pattern) {
				quote_c_style(pattern->pl->src, NULL, stdout, 0);
				printf(":%d:%s%s%s\t",
				       pattern->srcpos,
				       bang, pattern->pattern, slash);
			}
			else {
				printf("::\t");
			}
			quote_c_style(path, NULL, stdout, 0);
			fputc('\n', stdout);
		}
	} else {
		if (!verbose) {
			printf("%s%c", path, '\0');
		} else {
			if (pattern)
				printf("%s%c%d%c%s%s%s%c%s%c",
				       pattern->pl->src, '\0',
				       pattern->srcpos, '\0',
				       bang, pattern->pattern, slash, '\0',
				       path, '\0');
			else
				printf("%c%c%c%s%c", '\0', '\0', '\0', path, '\0');
		}
	}
}

static int check_ignore(struct dir_struct *dir,
			const char *prefix, int argc, const char **argv)
{
	const char *full_path;
	char *seen;
	int num_ignored = 0, i;
	struct path_pattern *pattern;
	struct pathspec pathspec;

	if (!argc) {
		if (!quiet)
			fprintf(stderr, "no pathspec given.\n");
		return 0;
	}

	/*
	 * check-ignore just needs paths. Magic beyond :/ is really
	 * irrelevant.
	 */
	parse_pathspec(&pathspec,
		       PATHSPEC_ALL_MAGIC & ~PATHSPEC_FROMTOP,
		       PATHSPEC_SYMLINK_LEADING_PATH |
		       PATHSPEC_KEEP_ORDER,
		       prefix, argv);

	die_path_inside_submodule(&the_index, &pathspec);

	/*
	 * look for pathspecs matching entries in the index, since these
	 * should not be ignored, in order to be consistent with
	 * 'git status', 'git add' etc.
	 */
	seen = find_pathspecs_matching_against_index(&pathspec, &the_index,
						     PS_HEED_SKIP_WORKTREE);
	for (i = 0; i < pathspec.nr; i++) {
		full_path = pathspec.items[i].match;
		pattern = NULL;
		if (!seen[i]) {
			int dtype = DT_UNKNOWN;
			pattern = last_matching_pattern(dir, &the_index,
							full_path, &dtype);
			if (!verbose && pattern &&
			    pattern->flags & PATTERN_FLAG_NEGATIVE)
				pattern = NULL;
		}
		if (!quiet && (pattern || show_non_matching))
			output_pattern(pathspec.items[i].original, pattern);
		if (pattern)
			num_ignored++;
	}
	free(seen);
	clear_pathspec(&pathspec);

	return num_ignored;
}

static int check_ignore_stdin_paths(struct dir_struct *dir, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf unquoted = STRBUF_INIT;
	char *pathspec[2] = { NULL, NULL };
	strbuf_getline_fn getline_fn;
	int num_ignored = 0;

	getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;
	while (getline_fn(&buf, stdin) != EOF) {
		if (!nul_term_line && buf.buf[0] == '"') {
			strbuf_reset(&unquoted);
			if (unquote_c_style(&unquoted, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &unquoted);
		}
		pathspec[0] = buf.buf;
		num_ignored += check_ignore(dir, prefix,
					    1, (const char **)pathspec);
		maybe_flush_or_die(stdout, "check-ignore to stdout");
	}
	strbuf_release(&buf);
	strbuf_release(&unquoted);
	return num_ignored;
}

int cmd_check_ignore(int argc, const char **argv, const char *prefix)
{
	int num_ignored;
	struct dir_struct dir = DIR_INIT;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, check_ignore_options,
			     check_ignore_usage, 0);

	if (stdin_paths) {
		if (argc > 0)
			die(_("cannot specify pathnames with --stdin"));
	} else {
		if (nul_term_line)
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
	if (show_non_matching && !verbose)
		die(_("--non-matching is only valid with --verbose"));

	/* read_cache() is only necessary so we can watch out for submodules. */
	if (!no_index && repo_read_index(the_repository) < 0)
		die(_("index file corrupt"));

	setup_standard_excludes(&dir);

	if (stdin_paths) {
		num_ignored = check_ignore_stdin_paths(&dir, prefix);
	} else {
		num_ignored = check_ignore(&dir, prefix, argc, argv);
		maybe_flush_or_die(stdout, "ignore to stdout");
	}

	dir_clear(&dir);

	return !num_ignored;
}
