/*
 * Builtin "git grep"
 *
 * Copyright (c) 2006 Junio C Hamano
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"
#include "builtin.h"
#include "parse-options.h"
#include "userdiff.h"
#include "grep.h"
#include "quote.h"
#include "dir.h"

static char const * const grep_usage[] = {
	"git grep [options] [-e] <pattern> [<rev>...] [[--] path...]",
	NULL
};

static int grep_config(const char *var, const char *value, void *cb)
{
	struct grep_opt *opt = cb;

	switch (userdiff_config(var, value)) {
	case 0: break;
	case -1: return -1;
	default: return 0;
	}

	if (!strcmp(var, "color.grep")) {
		opt->color = git_config_colorbool(var, value, -1);
		return 0;
	}
	if (!strcmp(var, "color.grep.match")) {
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, opt->color_match);
		return 0;
	}
	return git_color_default_config(var, value, cb);
}

/*
 * Return non-zero if max_depth is negative or path has no more then max_depth
 * slashes.
 */
static int accept_subdir(const char *path, int max_depth)
{
	if (max_depth < 0)
		return 1;

	while ((path = strchr(path, '/')) != NULL) {
		max_depth--;
		if (max_depth < 0)
			return 0;
		path++;
	}
	return 1;
}

/*
 * Return non-zero if name is a subdirectory of match and is not too deep.
 */
static int is_subdir(const char *name, int namelen,
		const char *match, int matchlen, int max_depth)
{
	if (matchlen > namelen || strncmp(name, match, matchlen))
		return 0;

	if (name[matchlen] == '\0') /* exact match */
		return 1;

	if (!matchlen || match[matchlen-1] == '/' || name[matchlen] == '/')
		return accept_subdir(name + matchlen + 1, max_depth);

	return 0;
}

/*
 * git grep pathspecs are somewhat different from diff-tree pathspecs;
 * pathname wildcards are allowed.
 */
static int pathspec_matches(const char **paths, const char *name, int max_depth)
{
	int namelen, i;
	if (!paths || !*paths)
		return accept_subdir(name, max_depth);
	namelen = strlen(name);
	for (i = 0; paths[i]; i++) {
		const char *match = paths[i];
		int matchlen = strlen(match);
		const char *cp, *meta;

		if (is_subdir(name, namelen, match, matchlen, max_depth))
			return 1;
		if (!fnmatch(match, name, 0))
			return 1;
		if (name[namelen-1] != '/')
			continue;

		/* We are being asked if the directory ("name") is worth
		 * descending into.
		 *
		 * Find the longest leading directory name that does
		 * not have metacharacter in the pathspec; the name
		 * we are looking at must overlap with that directory.
		 */
		for (cp = match, meta = NULL; cp - match < matchlen; cp++) {
			char ch = *cp;
			if (ch == '*' || ch == '[' || ch == '?') {
				meta = cp;
				break;
			}
		}
		if (!meta)
			meta = cp; /* fully literal */

		if (namelen <= meta - match) {
			/* Looking at "Documentation/" and
			 * the pattern says "Documentation/howto/", or
			 * "Documentation/diff*.txt".  The name we
			 * have should match prefix.
			 */
			if (!memcmp(match, name, namelen))
				return 1;
			continue;
		}

		if (meta - match < namelen) {
			/* Looking at "Documentation/howto/" and
			 * the pattern says "Documentation/h*";
			 * match up to "Do.../h"; this avoids descending
			 * into "Documentation/technical/".
			 */
			if (!memcmp(match, name, meta - match))
				return 1;
			continue;
		}
	}
	return 0;
}

static int grep_sha1(struct grep_opt *opt, const unsigned char *sha1, const char *name, int tree_name_len)
{
	unsigned long size;
	char *data;
	enum object_type type;
	int hit;
	struct strbuf pathbuf = STRBUF_INIT;

	data = read_sha1_file(sha1, &type, &size);
	if (!data) {
		error("'%s': unable to read %s", name, sha1_to_hex(sha1));
		return 0;
	}
	if (opt->relative && opt->prefix_length) {
		quote_path_relative(name + tree_name_len, -1, &pathbuf, opt->prefix);
		strbuf_insert(&pathbuf, 0, name, tree_name_len);
		name = pathbuf.buf;
	}
	hit = grep_buffer(opt, name, data, size);
	strbuf_release(&pathbuf);
	free(data);
	return hit;
}

static int grep_file(struct grep_opt *opt, const char *filename)
{
	struct stat st;
	int i;
	char *data;
	size_t sz;
	struct strbuf buf = STRBUF_INIT;

	if (lstat(filename, &st) < 0) {
	err_ret:
		if (errno != ENOENT)
			error("'%s': %s", filename, strerror(errno));
		return 0;
	}
	if (!S_ISREG(st.st_mode))
		return 0;
	sz = xsize_t(st.st_size);
	i = open(filename, O_RDONLY);
	if (i < 0)
		goto err_ret;
	data = xmalloc(sz + 1);
	if (st.st_size != read_in_full(i, data, sz)) {
		error("'%s': short read %s", filename, strerror(errno));
		close(i);
		free(data);
		return 0;
	}
	close(i);
	data[sz] = 0;
	if (opt->relative && opt->prefix_length)
		filename = quote_path_relative(filename, -1, &buf, opt->prefix);
	i = grep_buffer(opt, filename, data, sz);
	strbuf_release(&buf);
	free(data);
	return i;
}

static int grep_cache(struct grep_opt *opt, const char **paths, int cached)
{
	int hit = 0;
	int nr;
	read_cache();

	for (nr = 0; nr < active_nr; nr++) {
		struct cache_entry *ce = active_cache[nr];
		if (!S_ISREG(ce->ce_mode))
			continue;
		if (!pathspec_matches(paths, ce->name, opt->max_depth))
			continue;
		/*
		 * If CE_VALID is on, we assume worktree file and its cache entry
		 * are identical, even if worktree file has been modified, so use
		 * cache version instead
		 */
		if (cached || (ce->ce_flags & CE_VALID) || ce_skip_worktree(ce)) {
			if (ce_stage(ce))
				continue;
			hit |= grep_sha1(opt, ce->sha1, ce->name, 0);
		}
		else
			hit |= grep_file(opt, ce->name);
		if (ce_stage(ce)) {
			do {
				nr++;
			} while (nr < active_nr &&
				 !strcmp(ce->name, active_cache[nr]->name));
			nr--; /* compensate for loop control */
		}
	}
	free_grep_patterns(opt);
	return hit;
}

static int grep_tree(struct grep_opt *opt, const char **paths,
		     struct tree_desc *tree,
		     const char *tree_name, const char *base)
{
	int len;
	int hit = 0;
	struct name_entry entry;
	char *down;
	int tn_len = strlen(tree_name);
	struct strbuf pathbuf;

	strbuf_init(&pathbuf, PATH_MAX + tn_len);

	if (tn_len) {
		strbuf_add(&pathbuf, tree_name, tn_len);
		strbuf_addch(&pathbuf, ':');
		tn_len = pathbuf.len;
	}
	strbuf_addstr(&pathbuf, base);
	len = pathbuf.len;

	while (tree_entry(tree, &entry)) {
		int te_len = tree_entry_len(entry.path, entry.sha1);
		pathbuf.len = len;
		strbuf_add(&pathbuf, entry.path, te_len);

		if (S_ISDIR(entry.mode))
			/* Match "abc/" against pathspec to
			 * decide if we want to descend into "abc"
			 * directory.
			 */
			strbuf_addch(&pathbuf, '/');

		down = pathbuf.buf + tn_len;
		if (!pathspec_matches(paths, down, opt->max_depth))
			;
		else if (S_ISREG(entry.mode))
			hit |= grep_sha1(opt, entry.sha1, pathbuf.buf, tn_len);
		else if (S_ISDIR(entry.mode)) {
			enum object_type type;
			struct tree_desc sub;
			void *data;
			unsigned long size;

			data = read_sha1_file(entry.sha1, &type, &size);
			if (!data)
				die("unable to read tree (%s)",
				    sha1_to_hex(entry.sha1));
			init_tree_desc(&sub, data, size);
			hit |= grep_tree(opt, paths, &sub, tree_name, down);
			free(data);
		}
	}
	strbuf_release(&pathbuf);
	return hit;
}

static int grep_object(struct grep_opt *opt, const char **paths,
		       struct object *obj, const char *name)
{
	if (obj->type == OBJ_BLOB)
		return grep_sha1(opt, obj->sha1, name, 0);
	if (obj->type == OBJ_COMMIT || obj->type == OBJ_TREE) {
		struct tree_desc tree;
		void *data;
		unsigned long size;
		int hit;
		data = read_object_with_reference(obj->sha1, tree_type,
						  &size, NULL);
		if (!data)
			die("unable to read tree (%s)", sha1_to_hex(obj->sha1));
		init_tree_desc(&tree, data, size);
		hit = grep_tree(opt, paths, &tree, name, "");
		free(data);
		return hit;
	}
	die("unable to grep from object of type %s", typename(obj->type));
}

static int grep_directory(struct grep_opt *opt, const char **paths)
{
	struct dir_struct dir;
	int i, hit = 0;

	memset(&dir, 0, sizeof(dir));
	setup_standard_excludes(&dir);

	fill_directory(&dir, paths);
	for (i = 0; i < dir.nr; i++)
		hit |= grep_file(opt, dir.entries[i]->name);
	free_grep_patterns(opt);
	return hit;
}

static int context_callback(const struct option *opt, const char *arg,
			    int unset)
{
	struct grep_opt *grep_opt = opt->value;
	int value;
	const char *endp;

	if (unset) {
		grep_opt->pre_context = grep_opt->post_context = 0;
		return 0;
	}
	value = strtol(arg, (char **)&endp, 10);
	if (*endp) {
		return error("switch `%c' expects a numerical value",
			     opt->short_name);
	}
	grep_opt->pre_context = grep_opt->post_context = value;
	return 0;
}

static int file_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	FILE *patterns;
	int lno = 0;
	struct strbuf sb = STRBUF_INIT;

	patterns = fopen(arg, "r");
	if (!patterns)
		die_errno("cannot open '%s'", arg);
	while (strbuf_getline(&sb, patterns, '\n') == 0) {
		/* ignore empty line like grep does */
		if (sb.len == 0)
			continue;
		append_grep_pattern(grep_opt, strbuf_detach(&sb, NULL), arg,
				    ++lno, GREP_PATTERN);
	}
	fclose(patterns);
	strbuf_release(&sb);
	return 0;
}

static int not_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "--not", "command line", 0, GREP_NOT);
	return 0;
}

static int and_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "--and", "command line", 0, GREP_AND);
	return 0;
}

static int open_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "(", "command line", 0, GREP_OPEN_PAREN);
	return 0;
}

static int close_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, ")", "command line", 0, GREP_CLOSE_PAREN);
	return 0;
}

static int pattern_callback(const struct option *opt, const char *arg,
			    int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, arg, "-e option", 0, GREP_PATTERN);
	return 0;
}

static int help_callback(const struct option *opt, const char *arg, int unset)
{
	return -1;
}

int cmd_grep(int argc, const char **argv, const char *prefix)
{
	int hit = 0;
	int cached = 0;
	int seen_dashdash = 0;
	int external_grep_allowed__ignored;
	struct grep_opt opt;
	struct object_array list = { 0, 0, NULL };
	const char **paths = NULL;
	int i;
	int dummy;
	int nongit = 0, use_index = 1;
	struct option options[] = {
		OPT_BOOLEAN(0, "cached", &cached,
			"search in index instead of in the work tree"),
		OPT_BOOLEAN(0, "index", &use_index,
			"--no-index finds in contents not managed by git"),
		OPT_GROUP(""),
		OPT_BOOLEAN('v', "invert-match", &opt.invert,
			"show non-matching lines"),
		OPT_BOOLEAN('i', "ignore-case", &opt.ignore_case,
			"case insensitive matching"),
		OPT_BOOLEAN('w', "word-regexp", &opt.word_regexp,
			"match patterns only at word boundaries"),
		OPT_SET_INT('a', "text", &opt.binary,
			"process binary files as text", GREP_BINARY_TEXT),
		OPT_SET_INT('I', NULL, &opt.binary,
			"don't match patterns in binary files",
			GREP_BINARY_NOMATCH),
		{ OPTION_INTEGER, 0, "max-depth", &opt.max_depth, "depth",
			"descend at most <depth> levels", PARSE_OPT_NONEG,
			NULL, 1 },
		OPT_GROUP(""),
		OPT_BIT('E', "extended-regexp", &opt.regflags,
			"use extended POSIX regular expressions", REG_EXTENDED),
		OPT_NEGBIT('G', "basic-regexp", &opt.regflags,
			"use basic POSIX regular expressions (default)",
			REG_EXTENDED),
		OPT_BOOLEAN('F', "fixed-strings", &opt.fixed,
			"interpret patterns as fixed strings"),
		OPT_GROUP(""),
		OPT_BOOLEAN('n', NULL, &opt.linenum, "show line numbers"),
		OPT_NEGBIT('h', NULL, &opt.pathname, "don't show filenames", 1),
		OPT_BIT('H', NULL, &opt.pathname, "show filenames", 1),
		OPT_NEGBIT(0, "full-name", &opt.relative,
			"show filenames relative to top directory", 1),
		OPT_BOOLEAN('l', "files-with-matches", &opt.name_only,
			"show only filenames instead of matching lines"),
		OPT_BOOLEAN(0, "name-only", &opt.name_only,
			"synonym for --files-with-matches"),
		OPT_BOOLEAN('L', "files-without-match",
			&opt.unmatch_name_only,
			"show only the names of files without match"),
		OPT_BOOLEAN('z', "null", &opt.null_following_name,
			"print NUL after filenames"),
		OPT_BOOLEAN('c', "count", &opt.count,
			"show the number of matches instead of matching lines"),
		OPT_SET_INT(0, "color", &opt.color, "highlight matches", 1),
		OPT_GROUP(""),
		OPT_CALLBACK('C', NULL, &opt, "n",
			"show <n> context lines before and after matches",
			context_callback),
		OPT_INTEGER('B', NULL, &opt.pre_context,
			"show <n> context lines before matches"),
		OPT_INTEGER('A', NULL, &opt.post_context,
			"show <n> context lines after matches"),
		OPT_NUMBER_CALLBACK(&opt, "shortcut for -C NUM",
			context_callback),
		OPT_BOOLEAN('p', "show-function", &opt.funcname,
			"show a line with the function name before matches"),
		OPT_GROUP(""),
		OPT_CALLBACK('f', NULL, &opt, "file",
			"read patterns from file", file_callback),
		{ OPTION_CALLBACK, 'e', NULL, &opt, "pattern",
			"match <pattern>", PARSE_OPT_NONEG, pattern_callback },
		{ OPTION_CALLBACK, 0, "and", &opt, NULL,
		  "combine patterns specified with -e",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG, and_callback },
		OPT_BOOLEAN(0, "or", &dummy, ""),
		{ OPTION_CALLBACK, 0, "not", &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG, not_callback },
		{ OPTION_CALLBACK, '(', NULL, &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH,
		  open_callback },
		{ OPTION_CALLBACK, ')', NULL, &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH,
		  close_callback },
		OPT_BOOLEAN(0, "all-match", &opt.all_match,
			"show only matches from files that match all patterns"),
		OPT_GROUP(""),
		OPT_BOOLEAN(0, "ext-grep", &external_grep_allowed__ignored,
			    "allow calling of grep(1) (ignored by this build)"),
		{ OPTION_CALLBACK, 0, "help-all", &options, NULL, "show usage",
		  PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, help_callback },
		OPT_END()
	};

	prefix = setup_git_directory_gently(&nongit);

	/*
	 * 'git grep -h', unlike 'git grep -h <pattern>', is a request
	 * to show usage information and exit.
	 */
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(grep_usage, options);

	memset(&opt, 0, sizeof(opt));
	opt.prefix = prefix;
	opt.prefix_length = (prefix && *prefix) ? strlen(prefix) : 0;
	opt.relative = 1;
	opt.pathname = 1;
	opt.pattern_tail = &opt.pattern_list;
	opt.regflags = REG_NEWLINE;
	opt.max_depth = -1;

	strcpy(opt.color_match, GIT_COLOR_RED GIT_COLOR_BOLD);
	opt.color = -1;
	git_config(grep_config, &opt);
	if (opt.color == -1)
		opt.color = git_use_color_default;

	/*
	 * If there is no -- then the paths must exist in the working
	 * tree.  If there is no explicit pattern specified with -e or
	 * -f, we take the first unrecognized non option to be the
	 * pattern, but then what follows it must be zero or more
	 * valid refs up to the -- (if exists), and then existing
	 * paths.  If there is an explicit pattern, then the first
	 * unrecognized non option is the beginning of the refs list
	 * that continues up to the -- (if exists), and then paths.
	 */
	argc = parse_options(argc, argv, prefix, options, grep_usage,
			     PARSE_OPT_KEEP_DASHDASH |
			     PARSE_OPT_STOP_AT_NON_OPTION |
			     PARSE_OPT_NO_INTERNAL_HELP);

	if (use_index && nongit)
		/* die the same way as if we did it at the beginning */
		setup_git_directory();

	/* First unrecognized non-option token */
	if (argc > 0 && !opt.pattern_list) {
		append_grep_pattern(&opt, argv[0], "command line", 0,
				    GREP_PATTERN);
		argv++;
		argc--;
	}

	if (!opt.pattern_list)
		die("no pattern given.");
	if (!opt.fixed && opt.ignore_case)
		opt.regflags |= REG_ICASE;
	if ((opt.regflags != REG_NEWLINE) && opt.fixed)
		die("cannot mix --fixed-strings and regexp");
	compile_grep_patterns(&opt);

	/* Check revs and then paths */
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		unsigned char sha1[20];
		/* Is it a rev? */
		if (!get_sha1(arg, sha1)) {
			struct object *object = parse_object(sha1);
			if (!object)
				die("bad object %s", arg);
			add_object_array(object, arg, &list);
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			seen_dashdash = 1;
		}
		break;
	}

	/* The rest are paths */
	if (!seen_dashdash) {
		int j;
		for (j = i; j < argc; j++)
			verify_filename(prefix, argv[j]);
	}

	if (i < argc)
		paths = get_pathspec(prefix, argv + i);
	else if (prefix) {
		paths = xcalloc(2, sizeof(const char *));
		paths[0] = prefix;
		paths[1] = NULL;
	}

	if (!use_index) {
		if (cached)
			die("--cached cannot be used with --no-index.");
		if (list.nr)
			die("--no-index cannot be used with revs.");
		return !grep_directory(&opt, paths);
	}

	if (!list.nr) {
		if (!cached)
			setup_work_tree();
		return !grep_cache(&opt, paths, cached);
	}

	if (cached)
		die("both --cached and trees are given.");

	for (i = 0; i < list.nr; i++) {
		struct object *real_obj;
		real_obj = deref_tag(list.objects[i].item, NULL, 0);
		if (grep_object(&opt, paths, real_obj, list.objects[i].name))
			hit = 1;
	}
	free_grep_patterns(&opt);
	return !hit;
}
