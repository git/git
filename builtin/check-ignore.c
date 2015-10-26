#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "quote.h"
#include "pathspec.h"
#include "parse-options.h"

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

static void output_exclude(const char *path, struct exclude *exclude)
{
	char *bang  = (exclude && exclude->flags & EXC_FLAG_NEGATIVE)  ? "!" : "";
	char *slash = (exclude && exclude->flags & EXC_FLAG_MUSTBEDIR) ? "/" : "";
	if (!nul_term_line) {
		if (!verbose) {
			write_name_quoted(path, stdout, '\n');
		} else {
			if (exclude) {
				quote_c_style(exclude->el->src, NULL, stdout, 0);
				printf(":%d:%s%s%s\t",
				       exclude->srcpos,
				       bang, exclude->pattern, slash);
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
			if (exclude)
				printf("%s%c%d%c%s%s%s%c%s%c",
				       exclude->el->src, '\0',
				       exclude->srcpos, '\0',
				       bang, exclude->pattern, slash, '\0',
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
	int num_ignored = 0, dtype = DT_UNKNOWN, i;
	struct exclude *exclude;
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
		       PATHSPEC_STRIP_SUBMODULE_SLASH_EXPENSIVE |
		       PATHSPEC_KEEP_ORDER,
		       prefix, argv);

	/*
	 * look for pathspecs matching entries in the index, since these
	 * should not be ignored, in order to be consistent with
	 * 'git status', 'git add' etc.
	 */
	seen = find_pathspecs_matching_against_index(&pathspec);
	for (i = 0; i < pathspec.nr; i++) {
		full_path = pathspec.items[i].match;
		exclude = NULL;
		if (!seen[i]) {
			exclude = last_exclude_matching(dir, full_path, &dtype);
		}
		if (!quiet && (exclude || show_non_matching))
			output_exclude(pathspec.items[i].original, exclude);
		if (exclude)
			num_ignored++;
	}
	free(seen);

	return num_ignored;
}

static int check_ignore_stdin_paths(struct dir_struct *dir, const char *prefix)
{
	struct strbuf buf, nbuf;
	char *pathspec[2] = { NULL, NULL };
	int line_termination = nul_term_line ? 0 : '\n';
	int num_ignored = 0;

	strbuf_init(&buf, 0);
	strbuf_init(&nbuf, 0);
	while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
		if (line_termination && buf.buf[0] == '"') {
			strbuf_reset(&nbuf);
			if (unquote_c_style(&nbuf, buf.buf, NULL))
				die("line is badly quoted");
			strbuf_swap(&buf, &nbuf);
		}
		pathspec[0] = buf.buf;
		num_ignored += check_ignore(dir, prefix,
					    1, (const char **)pathspec);
		maybe_flush_or_die(stdout, "check-ignore to stdout");
	}
	strbuf_release(&buf);
	strbuf_release(&nbuf);
	return num_ignored;
}

int cmd_check_ignore(int argc, const char **argv, const char *prefix)
{
	int num_ignored;
	struct dir_struct dir;

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
	if (!no_index && read_cache() < 0)
		die(_("index file corrupt"));

	memset(&dir, 0, sizeof(dir));
	setup_standard_excludes(&dir);

	if (stdin_paths) {
		num_ignored = check_ignore_stdin_paths(&dir, prefix);
	} else {
		num_ignored = check_ignore(&dir, prefix, argc, argv);
		maybe_flush_or_die(stdout, "ignore to stdout");
	}

	clear_directory(&dir);

	return !num_ignored;
}
