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
#include <regex.h>
#include <fnmatch.h>

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

		if ((matchlen <= namelen) &&
		    !strncmp(name, match, matchlen) &&
		    (match[matchlen-1] == '/' ||
		     name[matchlen] == '\0' || name[matchlen] == '/'))
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

struct grep_pat {
	struct grep_pat *next;
	const char *pattern;
	regex_t regexp;
};

struct grep_opt {
	struct grep_pat *pattern_list;
	struct grep_pat **pattern_tail;
	regex_t regexp;
	unsigned linenum:1;
	unsigned invert:1;
	unsigned name_only:1;
	unsigned count:1;
	unsigned word_regexp:1;
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	unsigned binary:2;
	int regflags;
	unsigned pre_context;
	unsigned post_context;
};

static void add_pattern(struct grep_opt *opt, const char *pat)
{
	struct grep_pat *p = xcalloc(1, sizeof(*p));
	p->pattern = pat;
	*opt->pattern_tail = p;
	opt->pattern_tail = &p->next;
	p->next = NULL;
}

static void compile_patterns(struct grep_opt *opt)
{
	struct grep_pat *p;
	for (p = opt->pattern_list; p; p = p->next) {
		int err = regcomp(&p->regexp, p->pattern, opt->regflags);
		if (err) {
			char errbuf[1024];
			regerror(err, &p->regexp, errbuf, 1024);
			regfree(&p->regexp);
			die("'%s': %s", p->pattern, errbuf);
		}
	}
}

static char *end_of_line(char *cp, unsigned long *left)
{
	unsigned long l = *left;
	while (l && *cp != '\n') {
		l--;
		cp++;
	}
	*left = l;
	return cp;
}

static int word_char(char ch)
{
	return isalnum(ch) || ch == '_';
}

static void show_line(struct grep_opt *opt, const char *bol, const char *eol,
		      const char *name, unsigned lno, char sign)
{
	printf("%s%c", name, sign);
	if (opt->linenum)
		printf("%d%c", lno, sign);
	printf("%.*s\n", (int)(eol-bol), bol);
}

/*
 * NEEDSWORK: share code with diff.c
 */
#define FIRST_FEW_BYTES 8000
static int buffer_is_binary(const char *ptr, unsigned long size)
{
	if (FIRST_FEW_BYTES < size)
		size = FIRST_FEW_BYTES;
	if (memchr(ptr, 0, size))
		return 1;
	return 0;
}

static int grep_buffer(struct grep_opt *opt, const char *name,
		       char *buf, unsigned long size)
{
	char *bol = buf;
	unsigned long left = size;
	unsigned lno = 1;
	struct pre_context_line {
		char *bol;
		char *eol;
	} *prev = NULL, *pcl;
	unsigned last_hit = 0;
	unsigned last_shown = 0;
	int binary_match_only = 0;
	const char *hunk_mark = "";
	unsigned count = 0;

	if (buffer_is_binary(buf, size)) {
		switch (opt->binary) {
		case GREP_BINARY_DEFAULT:
			binary_match_only = 1;
			break;
		case GREP_BINARY_NOMATCH:
			return 0; /* Assume unmatch */
			break;
		default:
			break;
		}
	}

	if (opt->pre_context)
		prev = xcalloc(opt->pre_context, sizeof(*prev));
	if (opt->pre_context || opt->post_context)
		hunk_mark = "--\n";

	while (left) {
		regmatch_t pmatch[10];
		char *eol, ch;
		int hit = 0;
		struct grep_pat *p;

		eol = end_of_line(bol, &left);
		ch = *eol;
		*eol = 0;

		for (p = opt->pattern_list; p; p = p->next) {
			regex_t *exp = &p->regexp;
			hit = !regexec(exp, bol, ARRAY_SIZE(pmatch),
				       pmatch, 0);

			if (hit && opt->word_regexp) {
				/* Match beginning must be either
				 * beginning of the line, or at word
				 * boundary (i.e. the last char must
				 * not be alnum or underscore).
				 */
				if ((pmatch[0].rm_so < 0) ||
				    (eol - bol) <= pmatch[0].rm_so ||
				    (pmatch[0].rm_eo < 0) ||
				    (eol - bol) < pmatch[0].rm_eo)
					die("regexp returned nonsense");
				if (pmatch[0].rm_so != 0 &&
				    word_char(bol[pmatch[0].rm_so-1]))
					continue; /* not a word boundary */
				if ((eol-bol) < pmatch[0].rm_eo &&
				    word_char(bol[pmatch[0].rm_eo]))
					continue; /* not a word boundary */
			}
			if (hit)
				break;
		}
		/* "grep -v -e foo -e bla" should list lines
		 * that do not have either, so inversion should
		 * be done outside.
		 */
		if (opt->invert)
			hit = !hit;
		if (hit) {
			count++;
			if (binary_match_only) {
				printf("Binary file %s matches\n", name);
				return 1;
			}
			if (opt->name_only) {
				printf("%s\n", name);
				return 1;
			}
			/* Hit at this line.  If we haven't shown the
			 * pre-context lines, we would need to show them.
			 * When asked to do "count", this still show
			 * the context which is nonsense, but the user
			 * deserves to get that ;-).
			 */
			if (opt->pre_context) {
				unsigned from;
				if (opt->pre_context < lno)
					from = lno - opt->pre_context;
				else
					from = 1;
				if (from <= last_shown)
					from = last_shown + 1;
				if (last_shown && from != last_shown + 1)
					printf(hunk_mark);
				while (from < lno) {
					pcl = &prev[lno-from-1];
					show_line(opt, pcl->bol, pcl->eol,
						  name, from, '-');
					from++;
				}
				last_shown = lno-1;
			}
			if (last_shown && lno != last_shown + 1)
				printf(hunk_mark);
			if (!opt->count)
				show_line(opt, bol, eol, name, lno, ':');
			last_shown = last_hit = lno;
		}
		else if (last_hit &&
			 lno <= last_hit + opt->post_context) {
			/* If the last hit is within the post context,
			 * we need to show this line.
			 */
			if (last_shown && lno != last_shown + 1)
				printf(hunk_mark);
			show_line(opt, bol, eol, name, lno, '-');
			last_shown = lno;
		}
		if (opt->pre_context) {
			memmove(prev+1, prev,
				(opt->pre_context-1) * sizeof(*prev));
			prev->bol = bol;
			prev->eol = eol;
		}
		*eol = ch;
		bol = eol + 1;
		if (!left)
			break;
		left--;
		lno++;
	}
	/* NEEDSWORK:
	 * The real "grep -c foo *.c" gives many "bar.c:0" lines,
	 * which feels mostly useless but sometimes useful.  Maybe
	 * make it another option?  For now suppress them.
	 */
	if (opt->count && count)
		printf("%s:%u\n", name, count);
	return !!last_hit;
}

static int grep_sha1(struct grep_opt *opt, const unsigned char *sha1, const char *name)
{
	unsigned long size;
	char *data;
	char type[20];
	int hit;
	data = read_sha1_file(sha1, type, &size);
	if (!data) {
		error("'%s': unable to read %s", name, sha1_to_hex(sha1));
		return 0;
	}
	hit = grep_buffer(opt, name, data, size);
	free(data);
	return hit;
}

static int grep_file(struct grep_opt *opt, const char *filename)
{
	struct stat st;
	int i;
	char *data;
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
	i = open(filename, O_RDONLY);
	if (i < 0)
		goto err_ret;
	data = xmalloc(st.st_size + 1);
	if (st.st_size != xread(i, data, st.st_size)) {
		error("'%s': short read %s", filename, strerror(errno));
		close(i);
		free(data);
		return 0;
	}
	close(i);
	i = grep_buffer(opt, filename, data, st.st_size);
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
		if (ce_stage(ce) || !S_ISREG(ntohl(ce->ce_mode)))
			continue;
		if (!pathspec_matches(paths, ce->name))
			continue;
		if (cached)
			hit |= grep_sha1(opt, ce->sha1, ce->name);
		else
			hit |= grep_file(opt, ce->name);
	}
	return hit;
}

static int grep_tree(struct grep_opt *opt, const char **paths,
		     struct tree_desc *tree,
		     const char *tree_name, const char *base)
{
	unsigned mode;
	int len;
	int hit = 0;
	const char *path;
	const unsigned char *sha1;
	char *down;
	char *path_buf = xmalloc(PATH_MAX + strlen(tree_name) + 100);

	if (tree_name[0]) {
		int offset = sprintf(path_buf, "%s:", tree_name);
		down = path_buf + offset;
		strcat(down, base);
	}
	else {
		down = path_buf;
		strcpy(down, base);
	}
	len = strlen(path_buf);

	while (tree->size) {
		int pathlen;
		sha1 = tree_entry_extract(tree, &path, &mode);
		pathlen = strlen(path);
		strcpy(path_buf + len, path);

		if (S_ISDIR(mode))
			/* Match "abc/" against pathspec to
			 * decide if we want to descend into "abc"
			 * directory.
			 */
			strcpy(path_buf + len + pathlen, "/");

		if (!pathspec_matches(paths, down))
			;
		else if (S_ISREG(mode))
			hit |= grep_sha1(opt, sha1, path_buf);
		else if (S_ISDIR(mode)) {
			char type[20];
			struct tree_desc sub;
			void *data;
			data = read_sha1_file(sha1, type, &sub.size);
			if (!data)
				die("unable to read tree (%s)",
				    sha1_to_hex(sha1));
			sub.buf = data;
			hit |= grep_tree(opt, paths, &sub, tree_name, down);
			free(data);
		}
		update_tree_entry(tree);
	}
	return hit;
}

static int grep_object(struct grep_opt *opt, const char **paths,
		       struct object *obj, const char *name)
{
	if (!strcmp(obj->type, blob_type))
		return grep_sha1(opt, obj->sha1, name);
	if (!strcmp(obj->type, commit_type) ||
	    !strcmp(obj->type, tree_type)) {
		struct tree_desc tree;
		void *data;
		int hit;
		data = read_object_with_reference(obj->sha1, tree_type,
						  &tree.size, NULL);
		if (!data)
			die("unable to read tree (%s)", sha1_to_hex(obj->sha1));
		tree.buf = data;
		hit = grep_tree(opt, paths, &tree, name, "");
		free(data);
		return hit;
	}
	die("unable to grep from object of type %s", obj->type);
}

static const char builtin_grep_usage[] =
"git-grep <option>* <rev>* [-e] <pattern> [<path>...]";

int cmd_grep(int argc, const char **argv, char **envp)
{
	int hit = 0;
	int no_more_flags = 0;
	int seen_noncommit = 0;
	int cached = 0;
	struct grep_opt opt;
	struct object_list *list, **tail, *object_list = NULL;
	const char *prefix = setup_git_directory();
	const char **paths = NULL;

	memset(&opt, 0, sizeof(opt));
	opt.pattern_tail = &opt.pattern_list;
	opt.regflags = REG_NEWLINE;

	/*
	 * No point using rev_info, really.
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
		if (!strcmp("-G", arg) ||
		    !strcmp("--basic-regexp", arg)) {
			opt.regflags &= ~REG_EXTENDED;
			continue;
		}
		if (!strcmp("-n", arg)) {
			opt.linenum = 1;
			continue;
		}
		if (!strcmp("-H", arg)) {
			/* We always show the pathname, so this
			 * is a noop.
			 */
			continue;
		}
		if (!strcmp("-l", arg) ||
		    !strcmp("--files-with-matches", arg)) {
			opt.name_only = 1;
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
		if (!strncmp("-A", arg, 2) ||
		    !strncmp("-B", arg, 2) ||
		    !strncmp("-C", arg, 2) ||
		    (arg[0] == '-' && '1' <= arg[1] && arg[1] <= '9')) {
			unsigned num;
			const char *scan;
			switch (arg[1]) {
			case 'A': case 'B': case 'C':
				if (!arg[2]) {
					if (argc <= 1)
						usage(builtin_grep_usage);
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
			if (sscanf(scan, "%u", &num) != 1)
				usage(builtin_grep_usage);
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
		if (!strcmp("-e", arg)) {
			if (1 < argc) {
				add_pattern(&opt, argv[1]);
				argv++;
				argc--;
				continue;
			}
			usage(builtin_grep_usage);
		}
		if (!strcmp("--", arg)) {
			no_more_flags = 1;
			continue;
		}
		/* Either unrecognized option or a single pattern */
		if (!no_more_flags && *arg == '-')
			usage(builtin_grep_usage);
		if (!opt.pattern_list) {
			add_pattern(&opt, arg);
			break;
		}
		else {
			/* We are looking at the first path or rev;
			 * it is found at argv[0] after leaving the
			 * loop.
			 */
			argc++; argv--;
			break;
		}
	}
	if (!opt.pattern_list)
		die("no pattern given.");
	compile_patterns(&opt);
	tail = &object_list;
	while (1 < argc) {
		struct object *object;
		struct object_list *elem;
		const char *arg = argv[1];
		unsigned char sha1[20];
		if (get_sha1(arg, sha1) < 0)
			break;
		object = parse_object(sha1);
		if (!object)
			die("bad object %s", arg);
		elem = object_list_insert(object, tail);
		elem->name = arg;
		tail = &elem->next;
		argc--; argv++;
	}
	if (1 < argc)
		paths = get_pathspec(prefix, argv + 1);
	else if (prefix) {
		paths = xcalloc(2, sizeof(const char *));
		paths[0] = prefix;
		paths[1] = NULL;
	}

	if (!object_list)
		return !grep_cache(&opt, paths, cached);
	/*
	 * Do not walk "grep -e foo master next pu -- Documentation/"
	 * but do walk "grep -e foo master..next -- Documentation/".
	 * Ranged request mixed with a blob or tree object, like
	 * "grep -e foo v1.0.0:Documentation/ master..next"
	 * so detect that and complain.
	 */
	for (list = object_list; list; list = list->next) {
		struct object *real_obj;
		real_obj = deref_tag(list->item, NULL, 0);
		if (strcmp(real_obj->type, commit_type))
			seen_noncommit = 1;
	}
	if (cached)
		die("both --cached and revisions given.");

	for (list = object_list; list; list = list->next) {
		struct object *real_obj;
		real_obj = deref_tag(list->item, NULL, 0);
		if (grep_object(&opt, paths, real_obj, list->name))
			hit = 1;
	}
	return !hit;
}
