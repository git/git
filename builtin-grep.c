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
#include "grep.h"

#ifndef NO_EXTERNAL_GREP
#ifdef __unix__
#define NO_EXTERNAL_GREP 0
#else
#define NO_EXTERNAL_GREP 1
#endif
#endif

/*
 * git grep pathspecs are somewhat different from diff-tree pathspecs;
 * pathname wildcards are allowed.
 */
static int pathspec_matches(const char **paths, const char *name)
{
	int namelen, i;
	if (!paths || !*paths)
		return 1;
	namelen = strlen(name);
	for (i = 0; paths[i]; i++) {
		const char *match = paths[i];
		int matchlen = strlen(match);
		const char *cp, *meta;

		if (!matchlen ||
		    ((matchlen <= namelen) &&
		     !strncmp(name, match, matchlen) &&
		     (match[matchlen-1] == '/' ||
		      name[matchlen] == '\0' || name[matchlen] == '/')))
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
	char *to_free = NULL;
	int hit;

	data = read_sha1_file(sha1, &type, &size);
	if (!data) {
		error("'%s': unable to read %s", name, sha1_to_hex(sha1));
		return 0;
	}
	if (opt->relative && opt->prefix_length) {
		static char name_buf[PATH_MAX];
		char *cp;
		int name_len = strlen(name) - opt->prefix_length + 1;

		if (!tree_name_len)
			name += opt->prefix_length;
		else {
			if (ARRAY_SIZE(name_buf) <= name_len)
				cp = to_free = xmalloc(name_len);
			else
				cp = name_buf;
			memcpy(cp, name, tree_name_len);
			strcpy(cp + tree_name_len,
			       name + tree_name_len + opt->prefix_length);
			name = cp;
		}
	}
	hit = grep_buffer(opt, name, data, size);
	free(data);
	free(to_free);
	return hit;
}

static int grep_file(struct grep_opt *opt, const char *filename)
{
	struct stat st;
	int i;
	char *data;
	size_t sz;

	if (lstat(filename, &st) < 0) {
	err_ret:
		if (errno != ENOENT)
			error("'%s': %s", filename, strerror(errno));
		return 0;
	}
	if (!st.st_size)
		return 0; /* empty file -- no grep hit */
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
	if (opt->relative && opt->prefix_length)
		filename += opt->prefix_length;
	i = grep_buffer(opt, filename, data, sz);
	free(data);
	return i;
}

#if !NO_EXTERNAL_GREP
static int exec_grep(int argc, const char **argv)
{
	pid_t pid;
	int status;

	argv[argc] = NULL;
	pid = fork();
	if (pid < 0)
		return pid;
	if (!pid) {
		execvp("grep", (char **) argv);
		exit(255);
	}
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		return -1;
	}
	if (WIFEXITED(status)) {
		if (!WEXITSTATUS(status))
			return 1;
		return 0;
	}
	return -1;
}

#define MAXARGS 1000
#define ARGBUF 4096
#define push_arg(a) do { \
	if (nr < MAXARGS) argv[nr++] = (a); \
	else die("maximum number of args exceeded"); \
	} while (0)

/*
 * If you send a singleton filename to grep, it does not give
 * the name of the file.  GNU grep has "-H" but we would want
 * that behaviour in a portable way.
 *
 * So we keep two pathnames in argv buffer unsent to grep in
 * the main loop if we need to do more than one grep.
 */
static int flush_grep(struct grep_opt *opt,
		      int argc, int arg0, const char **argv, int *kept)
{
	int status;
	int count = argc - arg0;
	const char *kept_0 = NULL;

	if (count <= 2) {
		/*
		 * Because we keep at least 2 paths in the call from
		 * the main loop (i.e. kept != NULL), and MAXARGS is
		 * far greater than 2, this usually is a call to
		 * conclude the grep.  However, the user could attempt
		 * to overflow the argv buffer by giving too many
		 * options to leave very small number of real
		 * arguments even for the call in the main loop.
		 */
		if (kept)
			die("insanely many options to grep");

		/*
		 * If we have two or more paths, we do not have to do
		 * anything special, but we need to push /dev/null to
		 * get "-H" behaviour of GNU grep portably but when we
		 * are not doing "-l" nor "-L" nor "-c".
		 */
		if (count == 1 &&
		    !opt->name_only &&
		    !opt->unmatch_name_only &&
		    !opt->count) {
			argv[argc++] = "/dev/null";
			argv[argc] = NULL;
		}
	}

	else if (kept) {
		/*
		 * Called because we found many paths and haven't finished
		 * iterating over the cache yet.  We keep two paths
		 * for the concluding call.  argv[argc-2] and argv[argc-1]
		 * has the last two paths, so save the first one away,
		 * replace it with NULL while sending the list to grep,
		 * and recover them after we are done.
		 */
		*kept = 2;
		kept_0 = argv[argc-2];
		argv[argc-2] = NULL;
		argc -= 2;
	}

	status = exec_grep(argc, argv);

	if (kept_0) {
		/*
		 * Then recover them.  Now the last arg is beyond the
		 * terminating NULL which is at argc, and the second
		 * from the last is what we saved away in kept_0
		 */
		argv[arg0++] = kept_0;
		argv[arg0] = argv[argc+1];
	}
	return status;
}

static int external_grep(struct grep_opt *opt, const char **paths, int cached)
{
	int i, nr, argc, hit, len, status;
	const char *argv[MAXARGS+1];
	char randarg[ARGBUF];
	char *argptr = randarg;
	struct grep_pat *p;

	if (opt->extended || (opt->relative && opt->prefix_length))
		return -1;
	len = nr = 0;
	push_arg("grep");
	if (opt->fixed)
		push_arg("-F");
	if (opt->linenum)
		push_arg("-n");
	if (!opt->pathname)
		push_arg("-h");
	if (opt->regflags & REG_EXTENDED)
		push_arg("-E");
	if (opt->regflags & REG_ICASE)
		push_arg("-i");
	if (opt->word_regexp)
		push_arg("-w");
	if (opt->name_only)
		push_arg("-l");
	if (opt->unmatch_name_only)
		push_arg("-L");
	if (opt->count)
		push_arg("-c");
	if (opt->post_context || opt->pre_context) {
		if (opt->post_context != opt->pre_context) {
			if (opt->pre_context) {
				push_arg("-B");
				len += snprintf(argptr, sizeof(randarg)-len,
						"%u", opt->pre_context) + 1;
				if (sizeof(randarg) <= len)
					die("maximum length of args exceeded");
				push_arg(argptr);
				argptr += len;
			}
			if (opt->post_context) {
				push_arg("-A");
				len += snprintf(argptr, sizeof(randarg)-len,
						"%u", opt->post_context) + 1;
				if (sizeof(randarg) <= len)
					die("maximum length of args exceeded");
				push_arg(argptr);
				argptr += len;
			}
		}
		else {
			push_arg("-C");
			len += snprintf(argptr, sizeof(randarg)-len,
					"%u", opt->post_context) + 1;
			if (sizeof(randarg) <= len)
				die("maximum length of args exceeded");
			push_arg(argptr);
			argptr += len;
		}
	}
	for (p = opt->pattern_list; p; p = p->next) {
		push_arg("-e");
		push_arg(p->pattern);
	}

	hit = 0;
	argc = nr;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		char *name;
		int kept;
		if (!S_ISREG(ce->ce_mode))
			continue;
		if (!pathspec_matches(paths, ce->name))
			continue;
		name = ce->name;
		if (name[0] == '-') {
			int len = ce_namelen(ce);
			name = xmalloc(len + 3);
			memcpy(name, "./", 2);
			memcpy(name + 2, ce->name, len + 1);
		}
		argv[argc++] = name;
		if (MAXARGS <= argc) {
			status = flush_grep(opt, argc, nr, argv, &kept);
			if (0 < status)
				hit = 1;
			argc = nr + kept;
		}
		if (ce_stage(ce)) {
			do {
				i++;
			} while (i < active_nr &&
				 !strcmp(ce->name, active_cache[i]->name));
			i--; /* compensate for loop control */
		}
	}
	if (argc > nr) {
		status = flush_grep(opt, argc, nr, argv, NULL);
		if (0 < status)
			hit = 1;
	}
	return hit;
}
#endif

static int grep_cache(struct grep_opt *opt, const char **paths, int cached)
{
	int hit = 0;
	int nr;
	read_cache();

#if !NO_EXTERNAL_GREP
	/*
	 * Use the external "grep" command for the case where
	 * we grep through the checked-out files. It tends to
	 * be a lot more optimized
	 */
	if (!cached) {
		hit = external_grep(opt, paths, cached);
		if (hit >= 0)
			return hit;
	}
#endif

	for (nr = 0; nr < active_nr; nr++) {
		struct cache_entry *ce = active_cache[nr];
		if (!S_ISREG(ce->ce_mode))
			continue;
		if (!pathspec_matches(paths, ce->name))
			continue;
		if (cached) {
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
		if (!pathspec_matches(paths, down))
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

static const char builtin_grep_usage[] =
"git grep <option>* [-e] <pattern> <rev>* [[--] <path>...]";

static const char emsg_invalid_context_len[] =
"%s: invalid context length argument";
static const char emsg_missing_context_len[] =
"missing context length argument";
static const char emsg_missing_argument[] =
"option requires an argument -%s";

int cmd_grep(int argc, const char **argv, const char *prefix)
{
	int hit = 0;
	int cached = 0;
	int seen_dashdash = 0;
	struct grep_opt opt;
	struct object_array list = { 0, 0, NULL };
	const char **paths = NULL;
	int i;

	memset(&opt, 0, sizeof(opt));
	opt.prefix_length = (prefix && *prefix) ? strlen(prefix) : 0;
	opt.relative = 1;
	opt.pathname = 1;
	opt.pattern_tail = &opt.pattern_list;
	opt.regflags = REG_NEWLINE;

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

	while (1 < argc) {
		const char *arg = argv[1];
		argc--; argv++;
		if (!strcmp("--cached", arg)) {
			cached = 1;
			continue;
		}
		if (!strcmp("-a", arg) ||
		    !strcmp("--text", arg)) {
			opt.binary = GREP_BINARY_TEXT;
			continue;
		}
		if (!strcmp("-i", arg) ||
		    !strcmp("--ignore-case", arg)) {
			opt.regflags |= REG_ICASE;
			continue;
		}
		if (!strcmp("-I", arg)) {
			opt.binary = GREP_BINARY_NOMATCH;
			continue;
		}
		if (!strcmp("-v", arg) ||
		    !strcmp("--invert-match", arg)) {
			opt.invert = 1;
			continue;
		}
		if (!strcmp("-E", arg) ||
		    !strcmp("--extended-regexp", arg)) {
			opt.regflags |= REG_EXTENDED;
			continue;
		}
		if (!strcmp("-F", arg) ||
		    !strcmp("--fixed-strings", arg)) {
			opt.fixed = 1;
			continue;
		}
		if (!strcmp("-G", arg) ||
		    !strcmp("--basic-regexp", arg)) {
			opt.regflags &= ~REG_EXTENDED;
			continue;
		}
		if (!strcmp("-n", arg)) {
			opt.linenum = 1;
			continue;
		}
		if (!strcmp("-h", arg)) {
			opt.pathname = 0;
			continue;
		}
		if (!strcmp("-H", arg)) {
			opt.pathname = 1;
			continue;
		}
		if (!strcmp("-l", arg) ||
		    !strcmp("--name-only", arg) ||
		    !strcmp("--files-with-matches", arg)) {
			opt.name_only = 1;
			continue;
		}
		if (!strcmp("-L", arg) ||
		    !strcmp("--files-without-match", arg)) {
			opt.unmatch_name_only = 1;
			continue;
		}
		if (!strcmp("-c", arg) ||
		    !strcmp("--count", arg)) {
			opt.count = 1;
			continue;
		}
		if (!strcmp("-w", arg) ||
		    !strcmp("--word-regexp", arg)) {
			opt.word_regexp = 1;
			continue;
		}
		if (!prefixcmp(arg, "-A") ||
		    !prefixcmp(arg, "-B") ||
		    !prefixcmp(arg, "-C") ||
		    (arg[0] == '-' && '1' <= arg[1] && arg[1] <= '9')) {
			unsigned num;
			const char *scan;
			switch (arg[1]) {
			case 'A': case 'B': case 'C':
				if (!arg[2]) {
					if (argc <= 1)
						die(emsg_missing_context_len);
					scan = *++argv;
					argc--;
				}
				else
					scan = arg + 2;
				break;
			default:
				scan = arg + 1;
				break;
			}
			if (strtoul_ui(scan, 10, &num))
				die(emsg_invalid_context_len, scan);
			switch (arg[1]) {
			case 'A':
				opt.post_context = num;
				break;
			default:
			case 'C':
				opt.post_context = num;
			case 'B':
				opt.pre_context = num;
				break;
			}
			continue;
		}
		if (!strcmp("-f", arg)) {
			FILE *patterns;
			int lno = 0;
			char buf[1024];
			if (argc <= 1)
				die(emsg_missing_argument, arg);
			patterns = fopen(argv[1], "r");
			if (!patterns)
				die("'%s': %s", argv[1], strerror(errno));
			while (fgets(buf, sizeof(buf), patterns)) {
				int len = strlen(buf);
				if (len && buf[len-1] == '\n')
					buf[len-1] = 0;
				/* ignore empty line like grep does */
				if (!buf[0])
					continue;
				append_grep_pattern(&opt, xstrdup(buf),
						    argv[1], ++lno,
						    GREP_PATTERN);
			}
			fclose(patterns);
			argv++;
			argc--;
			continue;
		}
		if (!strcmp("--not", arg)) {
			append_grep_pattern(&opt, arg, "command line", 0,
					    GREP_NOT);
			continue;
		}
		if (!strcmp("--and", arg)) {
			append_grep_pattern(&opt, arg, "command line", 0,
					    GREP_AND);
			continue;
		}
		if (!strcmp("--or", arg))
			continue; /* no-op */
		if (!strcmp("(", arg)) {
			append_grep_pattern(&opt, arg, "command line", 0,
					    GREP_OPEN_PAREN);
			continue;
		}
		if (!strcmp(")", arg)) {
			append_grep_pattern(&opt, arg, "command line", 0,
					    GREP_CLOSE_PAREN);
			continue;
		}
		if (!strcmp("--all-match", arg)) {
			opt.all_match = 1;
			continue;
		}
		if (!strcmp("-e", arg)) {
			if (1 < argc) {
				append_grep_pattern(&opt, argv[1],
						    "-e option", 0,
						    GREP_PATTERN);
				argv++;
				argc--;
				continue;
			}
			die(emsg_missing_argument, arg);
		}
		if (!strcmp("--full-name", arg)) {
			opt.relative = 0;
			continue;
		}
		if (!strcmp("--", arg)) {
			/* later processing wants to have this at argv[1] */
			argv--;
			argc++;
			break;
		}
		if (*arg == '-')
			usage(builtin_grep_usage);

		/* First unrecognized non-option token */
		if (!opt.pattern_list) {
			append_grep_pattern(&opt, arg, "command line", 0,
					    GREP_PATTERN);
			break;
		}
		else {
			/* We are looking at the first path or rev;
			 * it is found at argv[1] after leaving the
			 * loop.
			 */
			argc++; argv--;
			break;
		}
	}

	if (!opt.pattern_list)
		die("no pattern given.");
	if ((opt.regflags != REG_NEWLINE) && opt.fixed)
		die("cannot mix --fixed-strings and regexp");
	compile_grep_patterns(&opt);

	/* Check revs and then paths */
	for (i = 1; i < argc; i++) {
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

	if (i < argc) {
		paths = get_pathspec(prefix, argv + i);
		if (opt.prefix_length && opt.relative) {
			/* Make sure we do not get outside of paths */
			for (i = 0; paths[i]; i++)
				if (strncmp(prefix, paths[i], opt.prefix_length))
					die("git-grep: cannot generate relative filenames containing '..'");
		}
	}
	else if (prefix) {
		paths = xcalloc(2, sizeof(const char *));
		paths[0] = prefix;
		paths[1] = NULL;
	}

	if (!list.nr)
		return !grep_cache(&opt, paths, cached);

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
