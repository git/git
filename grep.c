#include "cache.h"
#include "grep.h"
#include "xdiff-interface.h"

void append_header_grep_pattern(struct grep_opt *opt, enum grep_header_field field, const char *pat)
{
	struct grep_pat *p = xcalloc(1, sizeof(*p));
	p->pattern = pat;
	p->origin = "header";
	p->no = 0;
	p->token = GREP_PATTERN_HEAD;
	p->field = field;
	*opt->pattern_tail = p;
	opt->pattern_tail = &p->next;
	p->next = NULL;
}

void append_grep_pattern(struct grep_opt *opt, const char *pat,
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

static int is_fixed(const char *s)
{
	while (*s && !is_regex_special(*s))
		s++;
	return !*s;
}

static void compile_regexp(struct grep_pat *p, struct grep_opt *opt)
{
	int err;

	p->word_regexp = opt->word_regexp;

	if (opt->fixed || is_fixed(p->pattern))
		p->fixed = 1;
	if (opt->regflags & REG_ICASE)
		p->fixed = 0;
	if (p->fixed)
		return;

	err = regcomp(&p->regexp, p->pattern, opt->regflags);
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

static struct grep_expr *compile_pattern_or(struct grep_pat **);
static struct grep_expr *compile_pattern_atom(struct grep_pat **list)
{
	struct grep_pat *p;
	struct grep_expr *x;

	p = *list;
	if (!p)
		return NULL;
	switch (p->token) {
	case GREP_PATTERN: /* atom */
	case GREP_PATTERN_HEAD:
	case GREP_PATTERN_BODY:
		x = xcalloc(1, sizeof (struct grep_expr));
		x->node = GREP_NODE_ATOM;
		x->u.atom = p;
		*list = p->next;
		return x;
	case GREP_OPEN_PAREN:
		*list = p->next;
		x = compile_pattern_or(list);
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

	p = *list;
	if (!p)
		return NULL;
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
	return compile_pattern_or(list);
}

void compile_grep_patterns(struct grep_opt *opt)
{
	struct grep_pat *p;

	if (opt->all_match)
		opt->extended = 1;

	for (p = opt->pattern_list; p; p = p->next) {
		switch (p->token) {
		case GREP_PATTERN: /* atom */
		case GREP_PATTERN_HEAD:
		case GREP_PATTERN_BODY:
			compile_regexp(p, opt);
			break;
		default:
			opt->extended = 1;
			break;
		}
	}

	if (!opt->extended)
		return;

	/* Then bundle them up in an expression.
	 * A classic recursive descent parser would do.
	 */
	p = opt->pattern_list;
	if (p)
		opt->pattern_expression = compile_pattern_expr(&p);
	if (p)
		die("incomplete pattern expression: %s", p->pattern);
}

static void free_pattern_expr(struct grep_expr *x)
{
	switch (x->node) {
	case GREP_NODE_ATOM:
		break;
	case GREP_NODE_NOT:
		free_pattern_expr(x->u.unary);
		break;
	case GREP_NODE_AND:
	case GREP_NODE_OR:
		free_pattern_expr(x->u.binary.left);
		free_pattern_expr(x->u.binary.right);
		break;
	}
	free(x);
}

void free_grep_patterns(struct grep_opt *opt)
{
	struct grep_pat *p, *n;

	for (p = opt->pattern_list; p; p = n) {
		n = p->next;
		switch (p->token) {
		case GREP_PATTERN: /* atom */
		case GREP_PATTERN_HEAD:
		case GREP_PATTERN_BODY:
			regfree(&p->regexp);
			break;
		default:
			break;
		}
		free(p);
	}

	if (!opt->extended)
		return;
	free_pattern_expr(opt->pattern_expression);
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

static void show_name(struct grep_opt *opt, const char *name)
{
	printf("%s%c", name, opt->null_following_name ? '\0' : '\n');
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

static int strip_timestamp(char *bol, char **eol_p)
{
	char *eol = *eol_p;
	int ch;

	while (bol < --eol) {
		if (*eol != '>')
			continue;
		*eol_p = ++eol;
		ch = *eol;
		*eol = '\0';
		return ch;
	}
	return 0;
}

static struct {
	const char *field;
	size_t len;
} header_field[] = {
	{ "author ", 7 },
	{ "committer ", 10 },
};

static int match_one_pattern(struct grep_pat *p, char *bol, char *eol,
			     enum grep_context ctx,
			     regmatch_t *pmatch, int eflags)
{
	int hit = 0;
	int saved_ch = 0;

	if ((p->token != GREP_PATTERN) &&
	    ((p->token == GREP_PATTERN_HEAD) != (ctx == GREP_CONTEXT_HEAD)))
		return 0;

	if (p->token == GREP_PATTERN_HEAD) {
		const char *field;
		size_t len;
		assert(p->field < ARRAY_SIZE(header_field));
		field = header_field[p->field].field;
		len = header_field[p->field].len;
		if (strncmp(bol, field, len))
			return 0;
		bol += len;
		saved_ch = strip_timestamp(bol, &eol);
	}

 again:
	if (p->fixed)
		hit = !fixmatch(p->pattern, bol, pmatch);
	else
		hit = !regexec(&p->regexp, bol, 1, pmatch, eflags);

	if (hit && p->word_regexp) {
		if ((pmatch[0].rm_so < 0) ||
		    (eol - bol) <= pmatch[0].rm_so ||
		    (pmatch[0].rm_eo < 0) ||
		    (eol - bol) < pmatch[0].rm_eo)
			die("regexp returned nonsense");

		/* Match beginning must be either beginning of the
		 * line, or at word boundary (i.e. the last char must
		 * not be a word char).  Similarly, match end must be
		 * either end of the line, or at word boundary
		 * (i.e. the next char must not be a word char).
		 */
		if ( ((pmatch[0].rm_so == 0) ||
		      !word_char(bol[pmatch[0].rm_so-1])) &&
		     ((pmatch[0].rm_eo == (eol-bol)) ||
		      !word_char(bol[pmatch[0].rm_eo])) )
			;
		else
			hit = 0;

		if (!hit && pmatch[0].rm_so + bol + 1 < eol) {
			/* There could be more than one match on the
			 * line, and the first match might not be
			 * strict word match.  But later ones could be!
			 * Forward to the next possible start, i.e. the
			 * next position following a non-word char.
			 */
			bol = pmatch[0].rm_so + bol + 1;
			while (word_char(bol[-1]) && bol < eol)
				bol++;
			if (bol < eol)
				goto again;
		}
	}
	if (p->token == GREP_PATTERN_HEAD && saved_ch)
		*eol = saved_ch;
	return hit;
}

static int match_expr_eval(struct grep_expr *x, char *bol, char *eol,
			   enum grep_context ctx, int collect_hits)
{
	int h = 0;
	regmatch_t match;

	if (!x)
		die("Not a valid grep expression");
	switch (x->node) {
	case GREP_NODE_ATOM:
		h = match_one_pattern(x->u.atom, bol, eol, ctx, &match, 0);
		break;
	case GREP_NODE_NOT:
		h = !match_expr_eval(x->u.unary, bol, eol, ctx, 0);
		break;
	case GREP_NODE_AND:
		if (!match_expr_eval(x->u.binary.left, bol, eol, ctx, 0))
			return 0;
		h = match_expr_eval(x->u.binary.right, bol, eol, ctx, 0);
		break;
	case GREP_NODE_OR:
		if (!collect_hits)
			return (match_expr_eval(x->u.binary.left,
						bol, eol, ctx, 0) ||
				match_expr_eval(x->u.binary.right,
						bol, eol, ctx, 0));
		h = match_expr_eval(x->u.binary.left, bol, eol, ctx, 0);
		x->u.binary.left->hit |= h;
		h |= match_expr_eval(x->u.binary.right, bol, eol, ctx, 1);
		break;
	default:
		die("Unexpected node type (internal error) %d", x->node);
	}
	if (collect_hits)
		x->hit |= h;
	return h;
}

static int match_expr(struct grep_opt *opt, char *bol, char *eol,
		      enum grep_context ctx, int collect_hits)
{
	struct grep_expr *x = opt->pattern_expression;
	return match_expr_eval(x, bol, eol, ctx, collect_hits);
}

static int match_line(struct grep_opt *opt, char *bol, char *eol,
		      enum grep_context ctx, int collect_hits)
{
	struct grep_pat *p;
	regmatch_t match;

	if (opt->extended)
		return match_expr(opt, bol, eol, ctx, collect_hits);

	/* we do not call with collect_hits without being extended */
	for (p = opt->pattern_list; p; p = p->next) {
		if (match_one_pattern(p, bol, eol, ctx, &match, 0))
			return 1;
	}
	return 0;
}

static int match_next_pattern(struct grep_pat *p, char *bol, char *eol,
			      enum grep_context ctx,
			      regmatch_t *pmatch, int eflags)
{
	regmatch_t match;

	if (!match_one_pattern(p, bol, eol, ctx, &match, eflags))
		return 0;
	if (match.rm_so < 0 || match.rm_eo < 0)
		return 0;
	if (pmatch->rm_so >= 0 && pmatch->rm_eo >= 0) {
		if (match.rm_so > pmatch->rm_so)
			return 1;
		if (match.rm_so == pmatch->rm_so && match.rm_eo < pmatch->rm_eo)
			return 1;
	}
	pmatch->rm_so = match.rm_so;
	pmatch->rm_eo = match.rm_eo;
	return 1;
}

static int next_match(struct grep_opt *opt, char *bol, char *eol,
		      enum grep_context ctx, regmatch_t *pmatch, int eflags)
{
	struct grep_pat *p;
	int hit = 0;

	pmatch->rm_so = pmatch->rm_eo = -1;
	if (bol < eol) {
		for (p = opt->pattern_list; p; p = p->next) {
			switch (p->token) {
			case GREP_PATTERN: /* atom */
			case GREP_PATTERN_HEAD:
			case GREP_PATTERN_BODY:
				hit |= match_next_pattern(p, bol, eol, ctx,
							  pmatch, eflags);
				break;
			default:
				break;
			}
		}
	}
	return hit;
}

static void show_line(struct grep_opt *opt, char *bol, char *eol,
		      const char *name, unsigned lno, char sign)
{
	int rest = eol - bol;

	if (opt->null_following_name)
		sign = '\0';
	if (opt->pathname)
		printf("%s%c", name, sign);
	if (opt->linenum)
		printf("%d%c", lno, sign);
	if (opt->color) {
		regmatch_t match;
		enum grep_context ctx = GREP_CONTEXT_BODY;
		int ch = *eol;
		int eflags = 0;

		*eol = '\0';
		while (next_match(opt, bol, eol, ctx, &match, eflags)) {
			printf("%.*s%s%.*s%s",
			       (int)match.rm_so, bol,
			       opt->color_match,
			       (int)(match.rm_eo - match.rm_so), bol + match.rm_so,
			       GIT_COLOR_RESET);
			bol += match.rm_eo;
			rest -= match.rm_eo;
			eflags = REG_NOTBOL;
		}
		*eol = ch;
	}
	printf("%.*s\n", rest, bol);
}

static int grep_buffer_1(struct grep_opt *opt, const char *name,
			 char *buf, unsigned long size, int collect_hits)
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
	enum grep_context ctx = GREP_CONTEXT_HEAD;

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
		int hit;

		eol = end_of_line(bol, &left);
		ch = *eol;
		*eol = 0;

		if ((ctx == GREP_CONTEXT_HEAD) && (eol == bol))
			ctx = GREP_CONTEXT_BODY;

		hit = match_line(opt, bol, eol, ctx, collect_hits);
		*eol = ch;

		if (collect_hits)
			goto next_line;

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
			if (opt->status_only)
				return 1;
			if (binary_match_only) {
				printf("Binary file %s matches\n", name);
				return 1;
			}
			if (opt->name_only) {
				show_name(opt, name);
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
					fputs(hunk_mark, stdout);
				while (from < lno) {
					pcl = &prev[lno-from-1];
					show_line(opt, pcl->bol, pcl->eol,
						  name, from, '-');
					from++;
				}
				last_shown = lno-1;
			}
			if (last_shown && lno != last_shown + 1)
				fputs(hunk_mark, stdout);
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
				fputs(hunk_mark, stdout);
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
		bol = eol + 1;
		if (!left)
			break;
		left--;
		lno++;
	}

	free(prev);
	if (collect_hits)
		return 0;

	if (opt->status_only)
		return 0;
	if (opt->unmatch_name_only) {
		/* We did not see any hit, so we want to show this */
		show_name(opt, name);
		return 1;
	}

	/* NEEDSWORK:
	 * The real "grep -c foo *.c" gives many "bar.c:0" lines,
	 * which feels mostly useless but sometimes useful.  Maybe
	 * make it another option?  For now suppress them.
	 */
	if (opt->count && count)
		printf("%s%c%u\n", name,
		       opt->null_following_name ? '\0' : ':', count);
	return !!last_hit;
}

static void clr_hit_marker(struct grep_expr *x)
{
	/* All-hit markers are meaningful only at the very top level
	 * OR node.
	 */
	while (1) {
		x->hit = 0;
		if (x->node != GREP_NODE_OR)
			return;
		x->u.binary.left->hit = 0;
		x = x->u.binary.right;
	}
}

static int chk_hit_marker(struct grep_expr *x)
{
	/* Top level nodes have hit markers.  See if they all are hits */
	while (1) {
		if (x->node != GREP_NODE_OR)
			return x->hit;
		if (!x->u.binary.left->hit)
			return 0;
		x = x->u.binary.right;
	}
}

int grep_buffer(struct grep_opt *opt, const char *name, char *buf, unsigned long size)
{
	/*
	 * we do not have to do the two-pass grep when we do not check
	 * buffer-wide "all-match".
	 */
	if (!opt->all_match)
		return grep_buffer_1(opt, name, buf, size, 0);

	/* Otherwise the toplevel "or" terms hit a bit differently.
	 * We first clear hit markers from them.
	 */
	clr_hit_marker(opt->pattern_expression);
	grep_buffer_1(opt, name, buf, size, 1);

	if (!chk_hit_marker(opt->pattern_expression))
		return 0;

	return grep_buffer_1(opt, name, buf, size, 0);
}
