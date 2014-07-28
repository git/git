/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "quote.h"
#include "builtin.h"
#include "parse-options.h"
#include "diff.h"
#include "revision.h"
#include "split-index.h"

#define DO_REVS		1
#define DO_NOREV	2
#define DO_FLAGS	4
#define DO_NONFLAGS	8
static int filter = ~0;

static const char *def;

#define NORMAL 0
#define REVERSED 1
static int show_type = NORMAL;

#define SHOW_SYMBOLIC_ASIS 1
#define SHOW_SYMBOLIC_FULL 2
static int symbolic;
static int abbrev;
static int abbrev_ref;
static int abbrev_ref_strict;
static int output_sq;

static int stuck_long;
static struct string_list *ref_excludes;

/*
 * Some arguments are relevant "revision" arguments,
 * others are about output format or other details.
 * This sorts it all out.
 */
static int is_rev_argument(const char *arg)
{
	static const char *rev_args[] = {
		"--all",
		"--bisect",
		"--dense",
		"--branches=",
		"--branches",
		"--header",
		"--ignore-missing",
		"--max-age=",
		"--max-count=",
		"--min-age=",
		"--no-merges",
		"--min-parents=",
		"--no-min-parents",
		"--max-parents=",
		"--no-max-parents",
		"--objects",
		"--objects-edge",
		"--parents",
		"--pretty",
		"--remotes=",
		"--remotes",
		"--glob=",
		"--sparse",
		"--tags=",
		"--tags",
		"--topo-order",
		"--date-order",
		"--unpacked",
		NULL
	};
	const char **p = rev_args;

	/* accept -<digit>, like traditional "head" */
	if ((*arg == '-') && isdigit(arg[1]))
		return 1;

	for (;;) {
		const char *str = *p++;
		int len;
		if (!str)
			return 0;
		len = strlen(str);
		if (!strcmp(arg, str) ||
		    (str[len-1] == '=' && !strncmp(arg, str, len)))
			return 1;
	}
}

/* Output argument as a string, either SQ or normal */
static void show(const char *arg)
{
	if (output_sq) {
		int sq = '\'', ch;

		putchar(sq);
		while ((ch = *arg++)) {
			if (ch == sq)
				fputs("'\\'", stdout);
			putchar(ch);
		}
		putchar(sq);
		putchar(' ');
	}
	else
		puts(arg);
}

/* Like show(), but with a negation prefix according to type */
static void show_with_type(int type, const char *arg)
{
	if (type != show_type)
		putchar('^');
	show(arg);
}

/* Output a revision, only if filter allows it */
static void show_rev(int type, const unsigned char *sha1, const char *name)
{
	if (!(filter & DO_REVS))
		return;
	def = NULL;

	if ((symbolic || abbrev_ref) && name) {
		if (symbolic == SHOW_SYMBOLIC_FULL || abbrev_ref) {
			unsigned char discard[20];
			char *full;

			switch (dwim_ref(name, strlen(name), discard, &full)) {
			case 0:
				/*
				 * Not found -- not a ref.  We could
				 * emit "name" here, but symbolic-full
				 * users are interested in finding the
				 * refs spelled in full, and they would
				 * need to filter non-refs if we did so.
				 */
				break;
			case 1: /* happy */
				if (abbrev_ref)
					full = shorten_unambiguous_ref(full,
						abbrev_ref_strict);
				show_with_type(type, full);
				break;
			default: /* ambiguous */
				error("refname '%s' is ambiguous", name);
				break;
			}
			free(full);
		} else {
			show_with_type(type, name);
		}
	}
	else if (abbrev)
		show_with_type(type, find_unique_abbrev(sha1, abbrev));
	else
		show_with_type(type, sha1_to_hex(sha1));
}

/* Output a flag, only if filter allows it. */
static int show_flag(const char *arg)
{
	if (!(filter & DO_FLAGS))
		return 0;
	if (filter & (is_rev_argument(arg) ? DO_REVS : DO_NOREV)) {
		show(arg);
		return 1;
	}
	return 0;
}

static int show_default(void)
{
	const char *s = def;

	if (s) {
		unsigned char sha1[20];

		def = NULL;
		if (!get_sha1(s, sha1)) {
			show_rev(NORMAL, sha1, s);
			return 1;
		}
	}
	return 0;
}

static int show_reference(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	if (ref_excluded(ref_excludes, refname))
		return 0;
	show_rev(NORMAL, sha1, refname);
	return 0;
}

static int anti_reference(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	show_rev(REVERSED, sha1, refname);
	return 0;
}

static int show_abbrev(const unsigned char *sha1, void *cb_data)
{
	show_rev(NORMAL, sha1, NULL);
	return 0;
}

static void show_datestring(const char *flag, const char *datestr)
{
	static char buffer[100];

	/* date handling requires both flags and revs */
	if ((filter & (DO_FLAGS | DO_REVS)) != (DO_FLAGS | DO_REVS))
		return;
	snprintf(buffer, sizeof(buffer), "%s%lu", flag, approxidate(datestr));
	show(buffer);
}

static int show_file(const char *arg, int output_prefix)
{
	show_default();
	if ((filter & (DO_NONFLAGS|DO_NOREV)) == (DO_NONFLAGS|DO_NOREV)) {
		if (output_prefix) {
			const char *prefix = startup_info->prefix;
			show(prefix_filename(prefix,
					     prefix ? strlen(prefix) : 0,
					     arg));
		} else
			show(arg);
		return 1;
	}
	return 0;
}

static int try_difference(const char *arg)
{
	char *dotdot;
	unsigned char sha1[20];
	unsigned char end[20];
	const char *next;
	const char *this;
	int symmetric;
	static const char head_by_default[] = "HEAD";

	if (!(dotdot = strstr(arg, "..")))
		return 0;
	next = dotdot + 2;
	this = arg;
	symmetric = (*next == '.');

	*dotdot = 0;
	next += symmetric;

	if (!*next)
		next = head_by_default;
	if (dotdot == arg)
		this = head_by_default;

	if (this == head_by_default && next == head_by_default &&
	    !symmetric) {
		/*
		 * Just ".."?  That is not a range but the
		 * pathspec for the parent directory.
		 */
		*dotdot = '.';
		return 0;
	}

	if (!get_sha1_committish(this, sha1) && !get_sha1_committish(next, end)) {
		show_rev(NORMAL, end, next);
		show_rev(symmetric ? NORMAL : REVERSED, sha1, this);
		if (symmetric) {
			struct commit_list *exclude;
			struct commit *a, *b;
			a = lookup_commit_reference(sha1);
			b = lookup_commit_reference(end);
			exclude = get_merge_bases(a, b, 1);
			while (exclude) {
				struct commit_list *n = exclude->next;
				show_rev(REVERSED,
					 exclude->item->object.sha1,NULL);
				free(exclude);
				exclude = n;
			}
		}
		*dotdot = '.';
		return 1;
	}
	*dotdot = '.';
	return 0;
}

static int try_parent_shorthands(const char *arg)
{
	char *dotdot;
	unsigned char sha1[20];
	struct commit *commit;
	struct commit_list *parents;
	int parents_only;

	if ((dotdot = strstr(arg, "^!")))
		parents_only = 0;
	else if ((dotdot = strstr(arg, "^@")))
		parents_only = 1;

	if (!dotdot || dotdot[2])
		return 0;

	*dotdot = 0;
	if (get_sha1_committish(arg, sha1)) {
		*dotdot = '^';
		return 0;
	}

	if (!parents_only)
		show_rev(NORMAL, sha1, arg);
	commit = lookup_commit_reference(sha1);
	for (parents = commit->parents; parents; parents = parents->next)
		show_rev(parents_only ? NORMAL : REVERSED,
				parents->item->object.sha1, arg);

	*dotdot = '^';
	return 1;
}

static int parseopt_dump(const struct option *o, const char *arg, int unset)
{
	struct strbuf *parsed = o->value;
	if (unset)
		strbuf_addf(parsed, " --no-%s", o->long_name);
	else if (o->short_name && (o->long_name == NULL || !stuck_long))
		strbuf_addf(parsed, " -%c", o->short_name);
	else
		strbuf_addf(parsed, " --%s", o->long_name);
	if (arg) {
		if (!stuck_long)
			strbuf_addch(parsed, ' ');
		else if (o->long_name)
			strbuf_addch(parsed, '=');
		sq_quote_buf(parsed, arg);
	}
	return 0;
}

static const char *skipspaces(const char *s)
{
	while (isspace(*s))
		s++;
	return s;
}

static int cmd_parseopt(int argc, const char **argv, const char *prefix)
{
	static int keep_dashdash = 0, stop_at_non_option = 0;
	static char const * const parseopt_usage[] = {
		N_("git rev-parse --parseopt [options] -- [<args>...]"),
		NULL
	};
	static struct option parseopt_opts[] = {
		OPT_BOOL(0, "keep-dashdash", &keep_dashdash,
					N_("keep the `--` passed as an arg")),
		OPT_BOOL(0, "stop-at-non-option", &stop_at_non_option,
					N_("stop parsing after the "
					   "first non-option argument")),
		OPT_BOOL(0, "stuck-long", &stuck_long,
					N_("output in stuck long form")),
		OPT_END(),
	};

	struct strbuf sb = STRBUF_INIT, parsed = STRBUF_INIT;
	const char **usage = NULL;
	struct option *opts = NULL;
	int onb = 0, osz = 0, unb = 0, usz = 0;

	strbuf_addstr(&parsed, "set --");
	argc = parse_options(argc, argv, prefix, parseopt_opts, parseopt_usage,
	                     PARSE_OPT_KEEP_DASHDASH);
	if (argc < 1 || strcmp(argv[0], "--"))
		usage_with_options(parseopt_usage, parseopt_opts);

	/* get the usage up to the first line with a -- on it */
	for (;;) {
		if (strbuf_getline(&sb, stdin, '\n') == EOF)
			die("premature end of input");
		ALLOC_GROW(usage, unb + 1, usz);
		if (!strcmp("--", sb.buf)) {
			if (unb < 1)
				die("no usage string given before the `--' separator");
			usage[unb] = NULL;
			break;
		}
		usage[unb++] = strbuf_detach(&sb, NULL);
	}

	/* parse: (<short>|<short>,<long>|<long>)[*=?!]*<arghint>? SP+ <help> */
	while (strbuf_getline(&sb, stdin, '\n') != EOF) {
		const char *s;
		const char *end;
		struct option *o;

		if (!sb.len)
			continue;

		ALLOC_GROW(opts, onb + 1, osz);
		memset(opts + onb, 0, sizeof(opts[onb]));

		o = &opts[onb++];
		s = strchr(sb.buf, ' ');
		if (!s || *sb.buf == ' ') {
			o->type = OPTION_GROUP;
			o->help = xstrdup(skipspaces(sb.buf));
			continue;
		}

		o->type = OPTION_CALLBACK;
		o->help = xstrdup(skipspaces(s));
		o->value = &parsed;
		o->flags = PARSE_OPT_NOARG;
		o->callback = &parseopt_dump;

		/* Possible argument name hint */
		end = s;
		while (s > sb.buf && strchr("*=?!", s[-1]) == NULL)
			--s;
		if (s != sb.buf && s != end)
			o->argh = xmemdupz(s, end - s);
		if (s == sb.buf)
			s = end;

		while (s > sb.buf && strchr("*=?!", s[-1])) {
			switch (*--s) {
			case '=':
				o->flags &= ~PARSE_OPT_NOARG;
				break;
			case '?':
				o->flags &= ~PARSE_OPT_NOARG;
				o->flags |= PARSE_OPT_OPTARG;
				break;
			case '!':
				o->flags |= PARSE_OPT_NONEG;
				break;
			case '*':
				o->flags |= PARSE_OPT_HIDDEN;
				break;
			}
		}

		if (s - sb.buf == 1) /* short option only */
			o->short_name = *sb.buf;
		else if (sb.buf[1] != ',') /* long option only */
			o->long_name = xmemdupz(sb.buf, s - sb.buf);
		else {
			o->short_name = *sb.buf;
			o->long_name = xmemdupz(sb.buf + 2, s - sb.buf - 2);
		}
	}
	strbuf_release(&sb);

	/* put an OPT_END() */
	ALLOC_GROW(opts, onb + 1, osz);
	memset(opts + onb, 0, sizeof(opts[onb]));
	argc = parse_options(argc, argv, prefix, opts, usage,
			(keep_dashdash ? PARSE_OPT_KEEP_DASHDASH : 0) |
			(stop_at_non_option ? PARSE_OPT_STOP_AT_NON_OPTION : 0) |
			PARSE_OPT_SHELL_EVAL);

	strbuf_addf(&parsed, " --");
	sq_quote_argv(&parsed, argv, 0);
	puts(parsed.buf);
	return 0;
}

static int cmd_sq_quote(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;

	if (argc)
		sq_quote_argv(&buf, argv, 0);
	printf("%s\n", buf.buf);
	strbuf_release(&buf);

	return 0;
}

static void die_no_single_rev(int quiet)
{
	if (quiet)
		exit(1);
	else
		die("Needed a single revision");
}

static const char builtin_rev_parse_usage[] =
N_("git rev-parse --parseopt [options] -- [<args>...]\n"
   "   or: git rev-parse --sq-quote [<arg>...]\n"
   "   or: git rev-parse [options] [<arg>...]\n"
   "\n"
   "Run \"git rev-parse --parseopt -h\" for more information on the first usage.");

int cmd_rev_parse(int argc, const char **argv, const char *prefix)
{
	int i, as_is = 0, verify = 0, quiet = 0, revs_count = 0, type = 0;
	int has_dashdash = 0;
	int output_prefix = 0;
	unsigned char sha1[20];
	const char *name = NULL;

	if (argc > 1 && !strcmp("--parseopt", argv[1]))
		return cmd_parseopt(argc - 1, argv + 1, prefix);

	if (argc > 1 && !strcmp("--sq-quote", argv[1]))
		return cmd_sq_quote(argc - 2, argv + 2);

	if (argc > 1 && !strcmp("-h", argv[1]))
		usage(builtin_rev_parse_usage);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			has_dashdash = 1;
			break;
		}
	}

	prefix = setup_git_directory();
	git_config(git_default_config, NULL);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (as_is) {
			if (show_file(arg, output_prefix) && as_is < 2)
				verify_filename(prefix, arg, 0);
			continue;
		}
		if (!strcmp(arg,"-n")) {
			if (++i >= argc)
				die("-n requires an argument");
			if ((filter & DO_FLAGS) && (filter & DO_REVS)) {
				show(arg);
				show(argv[i]);
			}
			continue;
		}
		if (starts_with(arg, "-n")) {
			if ((filter & DO_FLAGS) && (filter & DO_REVS))
				show(arg);
			continue;
		}

		if (*arg == '-') {
			if (!strcmp(arg, "--")) {
				as_is = 2;
				/* Pass on the "--" if we show anything but files.. */
				if (filter & (DO_FLAGS | DO_REVS))
					show_file(arg, 0);
				continue;
			}
			if (!strcmp(arg, "--default")) {
				def = argv[++i];
				if (!def)
					die("--default requires an argument");
				continue;
			}
			if (!strcmp(arg, "--prefix")) {
				prefix = argv[++i];
				if (!prefix)
					die("--prefix requires an argument");
				startup_info->prefix = prefix;
				output_prefix = 1;
				continue;
			}
			if (!strcmp(arg, "--revs-only")) {
				filter &= ~DO_NOREV;
				continue;
			}
			if (!strcmp(arg, "--no-revs")) {
				filter &= ~DO_REVS;
				continue;
			}
			if (!strcmp(arg, "--flags")) {
				filter &= ~DO_NONFLAGS;
				continue;
			}
			if (!strcmp(arg, "--no-flags")) {
				filter &= ~DO_FLAGS;
				continue;
			}
			if (!strcmp(arg, "--verify")) {
				filter &= ~(DO_FLAGS|DO_NOREV);
				verify = 1;
				continue;
			}
			if (!strcmp(arg, "--quiet") || !strcmp(arg, "-q")) {
				quiet = 1;
				continue;
			}
			if (!strcmp(arg, "--short") ||
			    starts_with(arg, "--short=")) {
				filter &= ~(DO_FLAGS|DO_NOREV);
				verify = 1;
				abbrev = DEFAULT_ABBREV;
				if (arg[7] == '=')
					abbrev = strtoul(arg + 8, NULL, 10);
				if (abbrev < MINIMUM_ABBREV)
					abbrev = MINIMUM_ABBREV;
				else if (40 <= abbrev)
					abbrev = 40;
				continue;
			}
			if (!strcmp(arg, "--sq")) {
				output_sq = 1;
				continue;
			}
			if (!strcmp(arg, "--not")) {
				show_type ^= REVERSED;
				continue;
			}
			if (!strcmp(arg, "--symbolic")) {
				symbolic = SHOW_SYMBOLIC_ASIS;
				continue;
			}
			if (!strcmp(arg, "--symbolic-full-name")) {
				symbolic = SHOW_SYMBOLIC_FULL;
				continue;
			}
			if (starts_with(arg, "--abbrev-ref") &&
			    (!arg[12] || arg[12] == '=')) {
				abbrev_ref = 1;
				abbrev_ref_strict = warn_ambiguous_refs;
				if (arg[12] == '=') {
					if (!strcmp(arg + 13, "strict"))
						abbrev_ref_strict = 1;
					else if (!strcmp(arg + 13, "loose"))
						abbrev_ref_strict = 0;
					else
						die("unknown mode for %s", arg);
				}
				continue;
			}
			if (!strcmp(arg, "--all")) {
				for_each_ref(show_reference, NULL);
				continue;
			}
			if (starts_with(arg, "--disambiguate=")) {
				for_each_abbrev(arg + 15, show_abbrev, NULL);
				continue;
			}
			if (!strcmp(arg, "--bisect")) {
				for_each_ref_in("refs/bisect/bad", show_reference, NULL);
				for_each_ref_in("refs/bisect/good", anti_reference, NULL);
				continue;
			}
			if (starts_with(arg, "--branches=")) {
				for_each_glob_ref_in(show_reference, arg + 11,
					"refs/heads/", NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (!strcmp(arg, "--branches")) {
				for_each_branch_ref(show_reference, NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (starts_with(arg, "--tags=")) {
				for_each_glob_ref_in(show_reference, arg + 7,
					"refs/tags/", NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (!strcmp(arg, "--tags")) {
				for_each_tag_ref(show_reference, NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (starts_with(arg, "--glob=")) {
				for_each_glob_ref(show_reference, arg + 7, NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (starts_with(arg, "--remotes=")) {
				for_each_glob_ref_in(show_reference, arg + 10,
					"refs/remotes/", NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (!strcmp(arg, "--remotes")) {
				for_each_remote_ref(show_reference, NULL);
				clear_ref_exclusion(&ref_excludes);
				continue;
			}
			if (starts_with(arg, "--exclude=")) {
				add_ref_exclusion(&ref_excludes, arg + 10);
				continue;
			}
			if (!strcmp(arg, "--local-env-vars")) {
				int i;
				for (i = 0; local_repo_env[i]; i++)
					printf("%s\n", local_repo_env[i]);
				continue;
			}
			if (!strcmp(arg, "--show-toplevel")) {
				const char *work_tree = get_git_work_tree();
				if (work_tree)
					puts(work_tree);
				continue;
			}
			if (!strcmp(arg, "--show-prefix")) {
				if (prefix)
					puts(prefix);
				else
					putchar('\n');
				continue;
			}
			if (!strcmp(arg, "--show-cdup")) {
				const char *pfx = prefix;
				if (!is_inside_work_tree()) {
					const char *work_tree =
						get_git_work_tree();
					if (work_tree)
						printf("%s\n", work_tree);
					continue;
				}
				while (pfx) {
					pfx = strchr(pfx, '/');
					if (pfx) {
						pfx++;
						printf("../");
					}
				}
				putchar('\n');
				continue;
			}
			if (!strcmp(arg, "--git-dir")) {
				const char *gitdir = getenv(GIT_DIR_ENVIRONMENT);
				static char cwd[PATH_MAX];
				int len;
				if (gitdir) {
					puts(gitdir);
					continue;
				}
				if (!prefix) {
					puts(".git");
					continue;
				}
				if (!getcwd(cwd, PATH_MAX))
					die_errno("unable to get current working directory");
				len = strlen(cwd);
				printf("%s%s.git\n", cwd, len && cwd[len-1] != '/' ? "/" : "");
				continue;
			}
			if (!strcmp(arg, "--resolve-git-dir")) {
				const char *gitdir = argv[++i];
				if (!gitdir)
					die("--resolve-git-dir requires an argument");
				gitdir = resolve_gitdir(gitdir);
				if (!gitdir)
					die("not a gitdir '%s'", argv[i]);
				puts(gitdir);
				continue;
			}
			if (!strcmp(arg, "--is-inside-git-dir")) {
				printf("%s\n", is_inside_git_dir() ? "true"
						: "false");
				continue;
			}
			if (!strcmp(arg, "--is-inside-work-tree")) {
				printf("%s\n", is_inside_work_tree() ? "true"
						: "false");
				continue;
			}
			if (!strcmp(arg, "--is-bare-repository")) {
				printf("%s\n", is_bare_repository() ? "true"
						: "false");
				continue;
			}
			if (!strcmp(arg, "--shared-index-path")) {
				if (read_cache() < 0)
					die(_("Could not read the index"));
				if (the_index.split_index) {
					const unsigned char *sha1 = the_index.split_index->base_sha1;
					puts(git_path("sharedindex.%s", sha1_to_hex(sha1)));
				}
				continue;
			}
			if (starts_with(arg, "--since=")) {
				show_datestring("--max-age=", arg+8);
				continue;
			}
			if (starts_with(arg, "--after=")) {
				show_datestring("--max-age=", arg+8);
				continue;
			}
			if (starts_with(arg, "--before=")) {
				show_datestring("--min-age=", arg+9);
				continue;
			}
			if (starts_with(arg, "--until=")) {
				show_datestring("--min-age=", arg+8);
				continue;
			}
			if (show_flag(arg) && verify)
				die_no_single_rev(quiet);
			continue;
		}

		/* Not a flag argument */
		if (try_difference(arg))
			continue;
		if (try_parent_shorthands(arg))
			continue;
		name = arg;
		type = NORMAL;
		if (*arg == '^') {
			name++;
			type = REVERSED;
		}
		if (!get_sha1(name, sha1)) {
			if (verify)
				revs_count++;
			else
				show_rev(type, sha1, name);
			continue;
		}
		if (verify)
			die_no_single_rev(quiet);
		if (has_dashdash)
			die("bad revision '%s'", arg);
		as_is = 1;
		if (!show_file(arg, output_prefix))
			continue;
		verify_filename(prefix, arg, 1);
	}
	if (verify) {
		if (revs_count == 1) {
			show_rev(type, sha1, name);
			return 0;
		} else if (revs_count == 0 && show_default())
			return 0;
		die_no_single_rev(quiet);
	} else
		show_default();
	return 0;
}
