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
#include <sys/wait.h>

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

enum grep_pat_token {
	GREP_PATTERN,
	GREP_AND,
	GREP_OPEN_PAREN,
	GREP_CLOSE_PAREN,
	GREP_NOT,
	GREP_OR,
};

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	const char *pattern;
	regex_t regexp;
};

enum grep_expr_node {
	GREP_NODE_ATOM,
	GREP_NODE_NOT,
	GREP_NODE_AND,
	GREP_NODE_OR,
};

struct grep_expr {
	enum grep_expr_node node;
	union {
		struct grep_pat *atom;
		struct grep_expr *unary;
		struct {
			struct grep_expr *left;
			struct grep_expr *right;
		} binary;
	} u;
};

struct grep_opt {
	struct grep_pat *pattern_list;
	struct grep_pat **pattern_tail;
	struct grep_expr *pattern_expression;
	regex_t regexp;
	unsigned linenum:1;
	unsigned invert:1;
	unsigned name_only:1;
	unsigned unmatch_name_only:1;
	unsigned count:1;
	unsigned word_regexp:1;
	unsigned fixed:1;
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	unsigned binary:2;
	unsigned extended:1;
	int regflags;
	unsigned pre_context;
	unsigned post_context;
};

static void add_pattern(struct grep_opt *opt, const char *pat,
			const char *origin, int no, enum grep_pat_token t)
{
	struct grep_pat *p = xcalloc(1, sizeof(*p));
	p->pattern = pat;
	p->origin = origin;
	p->no = no;
	p->token = t;
	*opt->pattern_tail = p;
	opt->pattern_tail = &p->next;
	p->next = NULL;
}

static void compile_regexp(struct grep_pat *p, struct grep_opt *opt)
{
	int err = regcomp(&p->regexp, p->pattern, opt->regflags);
	if (err) {
		char errbuf[1024];
		char where[1024];
		if (p->no)
			sprintf(where, "In '%s' at %d, ",
				p->origin, p->no);
		else if (p->origin)
			sprintf(where, "%s, ", p->origin);
		else
			where[0] = 0;
		regerror(err, &p->regexp, errbuf, 1024);
		regfree(&p->regexp);
		die("%s'%s': %s", where, p->pattern, errbuf);
	}
}

#if DEBUG
static inline void indent(int in)
{
	int i;
	for (i = 0; i < in; i++) putchar(' ');
}

static void dump_pattern_exp(struct grep_expr *x, int in)
{
	switch (x->node) {
	case GREP_NODE_ATOM:
		indent(in);
		puts(x->u.atom->pattern);
		break;
	case GREP_NODE_NOT:
		indent(in);
		puts("--not");
		dump_pattern_exp(x->u.unary, in+1);
		break;
	case GREP_NODE_AND:
		dump_pattern_exp(x->u.binary.left, in+1);
		indent(in);
		puts("--and");
		dump_pattern_exp(x->u.binary.right, in+1);
		break;
	case GREP_NODE_OR:
		dump_pattern_exp(x->u.binary.left, in+1);
		indent(in);
		puts("--or");
		dump_pattern_exp(x->u.binary.right, in+1);
		break;
	}
}

static void looking_at(const char *msg, struct grep_pat **list)
{
	struct grep_pat *p = *list;
	fprintf(stderr, "%s: looking at ", msg);
	if (!p)
		fprintf(stderr, "empty\n");
	else
		fprintf(stderr, "<%s>\n", p->pattern);
}
#else
#define looking_at(a,b) do {} while(0)
#endif

static struct grep_expr *compile_pattern_expr(struct grep_pat **);
static struct grep_expr *compile_pattern_atom(struct grep_pat **list)
{
	struct grep_pat *p;
	struct grep_expr *x;

	looking_at("atom", list);

	p = *list;
	switch (p->token) {
	case GREP_PATTERN: /* atom */
		x = xcalloc(1, sizeof (struct grep_expr));
		x->node = GREP_NODE_ATOM;
		x->u.atom = p;
		*list = p->next;
		return x;
	case GREP_OPEN_PAREN:
		*list = p->next;
		x = compile_pattern_expr(list);
		if (!x)
			return NULL;
		if (!*list || (*list)->token != GREP_CLOSE_PAREN)
			die("unmatched parenthesis");
		*list = (*list)->next;
		return x;
	default:
		return NULL;
	}
}

static struct grep_expr *compile_pattern_not(struct grep_pat **list)
{
	struct grep_pat *p;
	struct grep_expr *x;

	looking_at("not", list);

	p = *list;
	switch (p->token) {
	case GREP_NOT:
		if (!p->next)
			die("--not not followed by pattern expression");
		*list = p->next;
		x = xcalloc(1, sizeof (struct grep_expr));
		x->node = GREP_NODE_NOT;
		x->u.unary = compile_pattern_not(list);
		if (!x->u.unary)
			die("--not followed by non pattern expression");
		return x;
	default:
		return compile_pattern_atom(list);
	}
}

static struct grep_expr *compile_pattern_and(struct grep_pat **list)
{
	struct grep_pat *p;
	struct grep_expr *x, *y, *z;

	looking_at("and", list);

	x = compile_pattern_not(list);
	p = *list;
	if (p && p->token == GREP_AND) {
		if (!p->next)
			die("--and not followed by pattern expression");
		*list = p->next;
		y = compile_pattern_and(list);
		if (!y)
			die("--and not followed by pattern expression");
		z = xcalloc(1, sizeof (struct grep_expr));
		z->node = GREP_NODE_AND;
		z->u.binary.left = x;
		z->u.binary.right = y;
		return z;
	}
	return x;
}

static struct grep_expr *compile_pattern_or(struct grep_pat **list)
{
	struct grep_pat *p;
	struct grep_expr *x, *y, *z;

	looking_at("or", list);

	x = compile_pattern_and(list);
	p = *list;
	if (x && p && p->token != GREP_CLOSE_PAREN) {
		y = compile_pattern_or(list);
		if (!y)
			die("not a pattern expression %s", p->pattern);
		z = xcalloc(1, sizeof (struct grep_expr));
		z->node = GREP_NODE_OR;
		z->u.binary.left = x;
		z->u.binary.right = y;
		return z;
	}
	return x;
}

static struct grep_expr *compile_pattern_expr(struct grep_pat **list)
{
	looking_at("expr", list);

	return compile_pattern_or(list);
}

static void compile_patterns(struct grep_opt *opt)
{
	struct grep_pat *p;

	/* First compile regexps */
	for (p = opt->pattern_list; p; p = p->next) {
		if (p->token == GREP_PATTERN)
			compile_regexp(p, opt);
		else
			opt->extended = 1;
	}

	if (!opt->extended)
		return;

	/* Then bundle them up in an expression.
	 * A classic recursive descent parser would do.
	 */
	p = opt->pattern_list;
	opt->pattern_expression = compile_pattern_expr(&p);
#if DEBUG
	dump_pattern_exp(opt->pattern_expression, 0);
#endif
	if (p)
		die("incomplete pattern expression: %s", p->pattern);
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

static int fixmatch(const char *pattern, char *line, regmatch_t *match)
{
	char *hit = strstr(line, pattern);
	if (!hit) {
		match->rm_so = match->rm_eo = -1;
		return REG_NOMATCH;
	}
	else {
		match->rm_so = hit - line;
		match->rm_eo = match->rm_so + strlen(pattern);
		return 0;
	}
}

static int match_one_pattern(struct grep_opt *opt, struct grep_pat *p, char *bol, char *eol)
{
	int hit = 0;
	regmatch_t pmatch[10];

	if (!opt->fixed) {
		regex_t *exp = &p->regexp;
		hit = !regexec(exp, bol, ARRAY_SIZE(pmatch),
			       pmatch, 0);
	}
	else {
		hit = !fixmatch(p->pattern, bol, pmatch);
	}

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
			hit = 0;
		if (pmatch[0].rm_eo != (eol-bol) &&
		    word_char(bol[pmatch[0].rm_eo]))
			hit = 0;
	}
	return hit;
}

static int match_expr_eval(struct grep_opt *opt,
			   struct grep_expr *x,
			   char *bol, char *eol)
{
	switch (x->node) {
	case GREP_NODE_ATOM:
		return match_one_pattern(opt, x->u.atom, bol, eol);
		break;
	case GREP_NODE_NOT:
		return !match_expr_eval(opt, x->u.unary, bol, eol);
	case GREP_NODE_AND:
		return (match_expr_eval(opt, x->u.binary.left, bol, eol) &&
			match_expr_eval(opt, x->u.binary.right, bol, eol));
	case GREP_NODE_OR:
		return (match_expr_eval(opt, x->u.binary.left, bol, eol) ||
			match_expr_eval(opt, x->u.binary.right, bol, eol));
	}
	die("Unexpected node type (internal error) %d\n", x->node);
}

static int match_expr(struct grep_opt *opt, char *bol, char *eol)
{
	struct grep_expr *x = opt->pattern_expression;
	return match_expr_eval(opt, x, bol, eol);
}

static int match_line(struct grep_opt *opt, char *bol, char *eol)
{
	struct grep_pat *p;
	if (opt->extended)
		return match_expr(opt, bol, eol);
	for (p = opt->pattern_list; p; p = p->next) {
		if (match_one_pattern(opt, p, bol, eol))
			return 1;
	}
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
		char *eol, ch;
		int hit = 0;

		eol = end_of_line(bol, &left);
		ch = *eol;
		*eol = 0;

		hit = match_line(opt, bol, eol);

		/* "grep -v -e foo -e bla" should list lines
		 * that do not have either, so inversion should
		 * be done outside.
		 */
		if (opt->invert)
			hit = !hit;
		if (opt->unmatch_name_only) {
			if (hit)
				return 0;
			goto next_line;
		}
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

	next_line:
		*eol = ch;
		bol = eol + 1;
		if (!left)
			break;
		left--;
		lno++;
	}

	if (opt->unmatch_name_only) {
		/* We did not see any hit, so we want to show this */
		printf("%s\n", name);
		return 1;
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

static int external_grep(struct grep_opt *opt, const char **paths, int cached)
{
	int i, nr, argc, hit, len, status;
	const char *argv[MAXARGS+1];
	char randarg[ARGBUF];
	char *argptr = randarg;
	struct grep_pat *p;

	if (opt->extended)
		return -1;
	len = nr = 0;
	push_arg("grep");
	if (opt->fixed)
		push_arg("-F");
	if (opt->linenum)
		push_arg("-n");
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
						"%u", opt->pre_context);
				if (sizeof(randarg) <= len)
					die("maximum length of args exceeded");
				push_arg(argptr);
				argptr += len;
			}
			if (opt->post_context) {
				push_arg("-A");
				len += snprintf(argptr, sizeof(randarg)-len,
						"%u", opt->post_context);
				if (sizeof(randarg) <= len)
					die("maximum length of args exceeded");
				push_arg(argptr);
				argptr += len;
			}
		}
		else {
			push_arg("-C");
			len += snprintf(argptr, sizeof(randarg)-len,
					"%u", opt->post_context);
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

	/*
	 * To make sure we get the header printed out when we want it,
	 * add /dev/null to the paths to grep.  This is unnecessary
	 * (and wrong) with "-l" or "-L", which always print out the
	 * name anyway.
	 *
	 * GNU grep has "-H", but this is portable.
	 */
	if (!opt->name_only && !opt->unmatch_name_only)
		push_arg("/dev/null");

	hit = 0;
	argc = nr;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		char *name;
		if (ce_stage(ce) || !S_ISREG(ntohl(ce->ce_mode)))
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
		if (argc < MAXARGS)
			continue;
		status = exec_grep(argc, argv);
		if (0 < status)
			hit = 1;
		argc = nr;
	}
	if (argc > nr) {
		status = exec_grep(argc, argv);
		if (0 < status)
			hit = 1;
	}
	return hit;
}

static int grep_cache(struct grep_opt *opt, const char **paths, int cached)
{
	int hit = 0;
	int nr;
	read_cache();

#ifdef __unix__
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
	int len;
	int hit = 0;
	struct name_entry entry;
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

	while (tree_entry(tree, &entry)) {
		strcpy(path_buf + len, entry.path);

		if (S_ISDIR(entry.mode))
			/* Match "abc/" against pathspec to
			 * decide if we want to descend into "abc"
			 * directory.
			 */
			strcpy(path_buf + len + entry.pathlen, "/");

		if (!pathspec_matches(paths, down))
			;
		else if (S_ISREG(entry.mode))
			hit |= grep_sha1(opt, entry.sha1, path_buf);
		else if (S_ISDIR(entry.mode)) {
			char type[20];
			struct tree_desc sub;
			void *data;
			data = read_sha1_file(entry.sha1, type, &sub.size);
			if (!data)
				die("unable to read tree (%s)",
				    sha1_to_hex(entry.sha1));
			sub.buf = data;
			hit |= grep_tree(opt, paths, &sub, tree_name, down);
			free(data);
		}
	}
	return hit;
}

static int grep_object(struct grep_opt *opt, const char **paths,
		       struct object *obj, const char *name)
{
	if (obj->type == TYPE_BLOB)
		return grep_sha1(opt, obj->sha1, name);
	if (obj->type == TYPE_COMMIT || obj->type == TYPE_TREE) {
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
	die("unable to grep from object of type %s", typename(obj->type));
}

static const char builtin_grep_usage[] =
"git-grep <option>* <rev>* [-e] <pattern> [<path>...]";

static const char emsg_invalid_context_len[] =
"%s: invalid context length argument";
static const char emsg_missing_context_len[] =
"missing context length argument";
static const char emsg_missing_argument[] =
"option requires an argument -%s";

int cmd_grep(int argc, const char **argv, char **envp)
{
	int hit = 0;
	int cached = 0;
	int seen_dashdash = 0;
	struct grep_opt opt;
	struct object_array list = { 0, 0, NULL };
	const char *prefix = setup_git_directory();
	const char **paths = NULL;
	int i;

	memset(&opt, 0, sizeof(opt));
	opt.pattern_tail = &opt.pattern_list;
	opt.regflags = REG_NEWLINE;

	/*
	 * If there is no -- then the paths must exist in the working
	 * tree.  If there is no explicit pattern specified with -e or
	 * -f, we take the first unrecognized non option to be the
	 * pattern, but then what follows it must be zero or more
	 * valid refs up to the -- (if exists), and then existing
	 * paths.  If there is an explicit pattern, then the first
	 * unrecocnized non option is the beginning of the refs list
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
			if (sscanf(scan, "%u", &num) != 1)
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
				if (buf[len-1] == '\n')
					buf[len-1] = 0;
				/* ignore empty line like grep does */
				if (!buf[0])
					continue;
				add_pattern(&opt, strdup(buf), argv[1], ++lno,
					    GREP_PATTERN);
			}
			fclose(patterns);
			argv++;
			argc--;
			continue;
		}
		if (!strcmp("--not", arg)) {
			add_pattern(&opt, arg, "command line", 0, GREP_NOT);
			continue;
		}
		if (!strcmp("--and", arg)) {
			add_pattern(&opt, arg, "command line", 0, GREP_AND);
			continue;
		}
		if (!strcmp("--or", arg))
			continue; /* no-op */
		if (!strcmp("(", arg)) {
			add_pattern(&opt, arg, "command line", 0, GREP_OPEN_PAREN);
			continue;
		}
		if (!strcmp(")", arg)) {
			add_pattern(&opt, arg, "command line", 0, GREP_CLOSE_PAREN);
			continue;
		}
		if (!strcmp("-e", arg)) {
			if (1 < argc) {
				add_pattern(&opt, argv[1], "-e option", 0,
					    GREP_PATTERN);
				argv++;
				argc--;
				continue;
			}
			die(emsg_missing_argument, arg);
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
			add_pattern(&opt, arg, "command line", 0,
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
	if (!opt.fixed)
		compile_patterns(&opt);

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

	if (i < argc)
		paths = get_pathspec(prefix, argv + i);
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
	return !hit;
}
