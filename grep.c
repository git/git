#include "cache.h"
#include "grep.h"
#include "userdiff.h"
#include "xdiff-interface.h"

void append_header_grep_pattern(struct grep_opt *opt, enum grep_header_field field, const char *pat)
{
	struct grep_pat *p = xcalloc(1, sizeof(*p));
	p->pattern = pat;
	p->patternlen = strlen(pat);
	p->origin = "header";
	p->no = 0;
	p->token = GREP_PATTERN_HEAD;
	p->field = field;
	*opt->header_tail = p;
	opt->header_tail = &p->next;
	p->next = NULL;
}

void append_grep_pattern(struct grep_opt *opt, const char *pat,
			 const char *origin, int no, enum grep_pat_token t)
{
	append_grep_pat(opt, pat, strlen(pat), origin, no, t);
}

void append_grep_pat(struct grep_opt *opt, const char *pat, size_t patlen,
		     const char *origin, int no, enum grep_pat_token t)
{
	struct grep_pat *p = xcalloc(1, sizeof(*p));
	p->pattern = pat;
	p->patternlen = patlen;
	p->origin = origin;
	p->no = no;
	p->token = t;
	*opt->pattern_tail = p;
	opt->pattern_tail = &p->next;
	p->next = NULL;
}

struct grep_opt *grep_opt_dup(const struct grep_opt *opt)
{
	struct grep_pat *pat;
	struct grep_opt *ret = xmalloc(sizeof(struct grep_opt));
	*ret = *opt;

	ret->pattern_list = NULL;
	ret->pattern_tail = &ret->pattern_list;

	for(pat = opt->pattern_list; pat != NULL; pat = pat->next)
	{
		if(pat->token == GREP_PATTERN_HEAD)
			append_header_grep_pattern(ret, pat->field,
						   pat->pattern);
		else
			append_grep_pat(ret, pat->pattern, pat->patternlen,
					pat->origin, pat->no, pat->token);
	}

	return ret;
}

static NORETURN void compile_regexp_failed(const struct grep_pat *p,
		const char *error)
{
	char where[1024];

	if (p->no)
		sprintf(where, "In '%s' at %d, ", p->origin, p->no);
	else if (p->origin)
		sprintf(where, "%s, ", p->origin);
	else
		where[0] = 0;

	die("%s'%s': %s", where, p->pattern, error);
}

#ifdef USE_LIBPCRE
static void compile_pcre_regexp(struct grep_pat *p, const struct grep_opt *opt)
{
	const char *error;
	int erroffset;
	int options = 0;

	if (opt->ignore_case)
		options |= PCRE_CASELESS;

	p->pcre_regexp = pcre_compile(p->pattern, options, &error, &erroffset,
			NULL);
	if (!p->pcre_regexp)
		compile_regexp_failed(p, error);

	p->pcre_extra_info = pcre_study(p->pcre_regexp, 0, &error);
	if (!p->pcre_extra_info && error)
		die("%s", error);
}

static int pcrematch(struct grep_pat *p, const char *line, const char *eol,
		regmatch_t *match, int eflags)
{
	int ovector[30], ret, flags = 0;

	if (eflags & REG_NOTBOL)
		flags |= PCRE_NOTBOL;

	ret = pcre_exec(p->pcre_regexp, p->pcre_extra_info, line, eol - line,
			0, flags, ovector, ARRAY_SIZE(ovector));
	if (ret < 0 && ret != PCRE_ERROR_NOMATCH)
		die("pcre_exec failed with error code %d", ret);
	if (ret > 0) {
		ret = 0;
		match->rm_so = ovector[0];
		match->rm_eo = ovector[1];
	}

	return ret;
}

static void free_pcre_regexp(struct grep_pat *p)
{
	pcre_free(p->pcre_regexp);
	pcre_free(p->pcre_extra_info);
}
#else /* !USE_LIBPCRE */
static void compile_pcre_regexp(struct grep_pat *p, const struct grep_opt *opt)
{
	die("cannot use Perl-compatible regexes when not compiled with USE_LIBPCRE");
}

static int pcrematch(struct grep_pat *p, const char *line, const char *eol,
		regmatch_t *match, int eflags)
{
	return 1;
}

static void free_pcre_regexp(struct grep_pat *p)
{
}
#endif /* !USE_LIBPCRE */

static void compile_regexp(struct grep_pat *p, struct grep_opt *opt)
{
	int err;

	p->word_regexp = opt->word_regexp;
	p->ignore_case = opt->ignore_case;
	p->fixed = opt->fixed;

	if (p->fixed)
		return;

	if (opt->pcre) {
		compile_pcre_regexp(p, opt);
		return;
	}

	err = regcomp(&p->regexp, p->pattern, opt->regflags);
	if (err) {
		char errbuf[1024];
		regerror(err, &p->regexp, errbuf, 1024);
		regfree(&p->regexp);
		compile_regexp_failed(p, errbuf);
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

static struct grep_expr *grep_true_expr(void)
{
	struct grep_expr *z = xcalloc(1, sizeof(*z));
	z->node = GREP_NODE_TRUE;
	return z;
}

static struct grep_expr *grep_or_expr(struct grep_expr *left, struct grep_expr *right)
{
	struct grep_expr *z = xcalloc(1, sizeof(*z));
	z->node = GREP_NODE_OR;
	z->u.binary.left = left;
	z->u.binary.right = right;
	return z;
}

static struct grep_expr *prep_header_patterns(struct grep_opt *opt)
{
	struct grep_pat *p;
	struct grep_expr *header_expr;
	struct grep_expr *(header_group[GREP_HEADER_FIELD_MAX]);
	enum grep_header_field fld;

	if (!opt->header_list)
		return NULL;
	p = opt->header_list;
	for (p = opt->header_list; p; p = p->next) {
		if (p->token != GREP_PATTERN_HEAD)
			die("bug: a non-header pattern in grep header list.");
		if (p->field < 0 || GREP_HEADER_FIELD_MAX <= p->field)
			die("bug: unknown header field %d", p->field);
		compile_regexp(p, opt);
	}

	for (fld = 0; fld < GREP_HEADER_FIELD_MAX; fld++)
		header_group[fld] = NULL;

	for (p = opt->header_list; p; p = p->next) {
		struct grep_expr *h;
		struct grep_pat *pp = p;

		h = compile_pattern_atom(&pp);
		if (!h || pp != p->next)
			die("bug: malformed header expr");
		if (!header_group[p->field]) {
			header_group[p->field] = h;
			continue;
		}
		header_group[p->field] = grep_or_expr(h, header_group[p->field]);
	}

	header_expr = NULL;

	for (fld = 0; fld < GREP_HEADER_FIELD_MAX; fld++) {
		if (!header_group[fld])
			continue;
		if (!header_expr)
			header_expr = grep_true_expr();
		header_expr = grep_or_expr(header_group[fld], header_expr);
	}
	return header_expr;
}

void compile_grep_patterns(struct grep_opt *opt)
{
	struct grep_pat *p;
	struct grep_expr *header_expr = prep_header_patterns(opt);

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

	if (opt->all_match || header_expr)
		opt->extended = 1;
	else if (!opt->extended)
		return;

	p = opt->pattern_list;
	if (p)
		opt->pattern_expression = compile_pattern_expr(&p);
	if (p)
		die("incomplete pattern expression: %s", p->pattern);

	if (!header_expr)
		return;

	if (!opt->pattern_expression)
		opt->pattern_expression = header_expr;
	else
		opt->pattern_expression = grep_or_expr(opt->pattern_expression,
						       header_expr);
	opt->all_match = 1;
}

static void free_pattern_expr(struct grep_expr *x)
{
	switch (x->node) {
	case GREP_NODE_TRUE:
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
			if (p->pcre_regexp)
				free_pcre_regexp(p);
			else
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

static void output_color(struct grep_opt *opt, const void *data, size_t size,
			 const char *color)
{
	if (opt->color && color && color[0]) {
		opt->output(opt, color, strlen(color));
		opt->output(opt, data, size);
		opt->output(opt, GIT_COLOR_RESET, strlen(GIT_COLOR_RESET));
	} else
		opt->output(opt, data, size);
}

static void output_sep(struct grep_opt *opt, char sign)
{
	if (opt->null_following_name)
		opt->output(opt, "\0", 1);
	else
		output_color(opt, &sign, 1, opt->color_sep);
}

static void show_name(struct grep_opt *opt, const char *name)
{
	output_color(opt, name, strlen(name), opt->color_filename);
	opt->output(opt, opt->null_following_name ? "\0" : "\n", 1);
}

static int fixmatch(struct grep_pat *p, char *line, char *eol,
		    regmatch_t *match)
{
	char *hit;

	if (p->ignore_case) {
		char *s = line;
		do {
			hit = strcasestr(s, p->pattern);
			if (hit)
				break;
			s += strlen(s) + 1;
		} while (s < eol);
	} else
		hit = memmem(line, eol - line, p->pattern, p->patternlen);

	if (!hit) {
		match->rm_so = match->rm_eo = -1;
		return REG_NOMATCH;
	}
	else {
		match->rm_so = hit - line;
		match->rm_eo = match->rm_so + p->patternlen;
		return 0;
	}
}

static int regmatch(const regex_t *preg, char *line, char *eol,
		    regmatch_t *match, int eflags)
{
#ifdef REG_STARTEND
	match->rm_so = 0;
	match->rm_eo = eol - line;
	eflags |= REG_STARTEND;
#endif
	return regexec(preg, line, 1, match, eflags);
}

static int patmatch(struct grep_pat *p, char *line, char *eol,
		    regmatch_t *match, int eflags)
{
	int hit;

	if (p->fixed)
		hit = !fixmatch(p, line, eol, match);
	else if (p->pcre_regexp)
		hit = !pcrematch(p, line, eol, match, eflags);
	else
		hit = !regmatch(&p->regexp, line, eol, match, eflags);

	return hit;
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
	const char *start = bol;

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
	hit = patmatch(p, bol, eol, pmatch, eflags);

	if (hit && p->word_regexp) {
		if ((pmatch[0].rm_so < 0) ||
		    (eol - bol) < pmatch[0].rm_so ||
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

		/* Words consist of at least one character. */
		if (pmatch->rm_so == pmatch->rm_eo)
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
			eflags |= REG_NOTBOL;
			if (bol < eol)
				goto again;
		}
	}
	if (p->token == GREP_PATTERN_HEAD && saved_ch)
		*eol = saved_ch;
	if (hit) {
		pmatch[0].rm_so += bol - start;
		pmatch[0].rm_eo += bol - start;
	}
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
	case GREP_NODE_TRUE:
		h = 1;
		break;
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
	char *line_color = NULL;

	if (opt->file_break && opt->last_shown == 0) {
		if (opt->show_hunk_mark)
			opt->output(opt, "\n", 1);
	} else if (opt->pre_context || opt->post_context) {
		if (opt->last_shown == 0) {
			if (opt->show_hunk_mark) {
				output_color(opt, "--", 2, opt->color_sep);
				opt->output(opt, "\n", 1);
			}
		} else if (lno > opt->last_shown + 1) {
			output_color(opt, "--", 2, opt->color_sep);
			opt->output(opt, "\n", 1);
		}
	}
	if (opt->heading && opt->last_shown == 0) {
		output_color(opt, name, strlen(name), opt->color_filename);
		opt->output(opt, "\n", 1);
	}
	opt->last_shown = lno;

	if (!opt->heading && opt->pathname) {
		output_color(opt, name, strlen(name), opt->color_filename);
		output_sep(opt, sign);
	}
	if (opt->linenum) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", lno);
		output_color(opt, buf, strlen(buf), opt->color_lineno);
		output_sep(opt, sign);
	}
	if (opt->color) {
		regmatch_t match;
		enum grep_context ctx = GREP_CONTEXT_BODY;
		int ch = *eol;
		int eflags = 0;

		if (sign == ':')
			line_color = opt->color_selected;
		else if (sign == '-')
			line_color = opt->color_context;
		else if (sign == '=')
			line_color = opt->color_function;
		*eol = '\0';
		while (next_match(opt, bol, eol, ctx, &match, eflags)) {
			if (match.rm_so == match.rm_eo)
				break;

			output_color(opt, bol, match.rm_so, line_color);
			output_color(opt, bol + match.rm_so,
				     match.rm_eo - match.rm_so,
				     opt->color_match);
			bol += match.rm_eo;
			rest -= match.rm_eo;
			eflags = REG_NOTBOL;
		}
		*eol = ch;
	}
	output_color(opt, bol, rest, line_color);
	opt->output(opt, "\n", 1);
}

static int match_funcname(struct grep_opt *opt, char *bol, char *eol)
{
	xdemitconf_t *xecfg = opt->priv;
	if (xecfg && xecfg->find_func) {
		char buf[1];
		return xecfg->find_func(bol, eol - bol, buf, 1,
					xecfg->find_func_priv) >= 0;
	}

	if (bol == eol)
		return 0;
	if (isalpha(*bol) || *bol == '_' || *bol == '$')
		return 1;
	return 0;
}

static void show_funcname_line(struct grep_opt *opt, const char *name,
			       char *buf, char *bol, unsigned lno)
{
	while (bol > buf) {
		char *eol = --bol;

		while (bol > buf && bol[-1] != '\n')
			bol--;
		lno--;

		if (lno <= opt->last_shown)
			break;

		if (match_funcname(opt, bol, eol)) {
			show_line(opt, bol, eol, name, lno, '=');
			break;
		}
	}
}

static void show_pre_context(struct grep_opt *opt, const char *name, char *buf,
			     char *bol, unsigned lno)
{
	unsigned cur = lno, from = 1, funcname_lno = 0;
	int funcname_needed = opt->funcname;

	if (opt->pre_context < lno)
		from = lno - opt->pre_context;
	if (from <= opt->last_shown)
		from = opt->last_shown + 1;

	/* Rewind. */
	while (bol > buf && cur > from) {
		char *eol = --bol;

		while (bol > buf && bol[-1] != '\n')
			bol--;
		cur--;
		if (funcname_needed && match_funcname(opt, bol, eol)) {
			funcname_lno = cur;
			funcname_needed = 0;
		}
	}

	/* We need to look even further back to find a function signature. */
	if (opt->funcname && funcname_needed)
		show_funcname_line(opt, name, buf, bol, cur);

	/* Back forward. */
	while (cur < lno) {
		char *eol = bol, sign = (cur == funcname_lno) ? '=' : '-';

		while (*eol != '\n')
			eol++;
		show_line(opt, bol, eol, name, cur, sign);
		bol = eol + 1;
		cur++;
	}
}

static int should_lookahead(struct grep_opt *opt)
{
	struct grep_pat *p;

	if (opt->extended)
		return 0; /* punt for too complex stuff */
	if (opt->invert)
		return 0;
	for (p = opt->pattern_list; p; p = p->next) {
		if (p->token != GREP_PATTERN)
			return 0; /* punt for "header only" and stuff */
	}
	return 1;
}

static int look_ahead(struct grep_opt *opt,
		      unsigned long *left_p,
		      unsigned *lno_p,
		      char **bol_p)
{
	unsigned lno = *lno_p;
	char *bol = *bol_p;
	struct grep_pat *p;
	char *sp, *last_bol;
	regoff_t earliest = -1;

	for (p = opt->pattern_list; p; p = p->next) {
		int hit;
		regmatch_t m;

		hit = patmatch(p, bol, bol + *left_p, &m, 0);
		if (!hit || m.rm_so < 0 || m.rm_eo < 0)
			continue;
		if (earliest < 0 || m.rm_so < earliest)
			earliest = m.rm_so;
	}

	if (earliest < 0) {
		*bol_p = bol + *left_p;
		*left_p = 0;
		return 1;
	}
	for (sp = bol + earliest; bol < sp && sp[-1] != '\n'; sp--)
		; /* find the beginning of the line */
	last_bol = sp;

	for (sp = bol; sp < last_bol; sp++) {
		if (*sp == '\n')
			lno++;
	}
	*left_p -= last_bol - bol;
	*bol_p = last_bol;
	*lno_p = lno;
	return 0;
}

int grep_threads_ok(const struct grep_opt *opt)
{
	/* If this condition is true, then we may use the attribute
	 * machinery in grep_buffer_1. The attribute code is not
	 * thread safe, so we disable the use of threads.
	 */
	if (opt->funcname && !opt->unmatch_name_only && !opt->status_only &&
	    !opt->name_only)
		return 0;

	return 1;
}

static void std_output(struct grep_opt *opt, const void *buf, size_t size)
{
	fwrite(buf, size, 1, stdout);
}

static int grep_buffer_1(struct grep_opt *opt, const char *name,
			 char *buf, unsigned long size, int collect_hits)
{
	char *bol = buf;
	unsigned long left = size;
	unsigned lno = 1;
	unsigned last_hit = 0;
	int binary_match_only = 0;
	unsigned count = 0;
	int try_lookahead = 0;
	enum grep_context ctx = GREP_CONTEXT_HEAD;
	xdemitconf_t xecfg;

	if (!opt->output)
		opt->output = std_output;

	if (opt->pre_context || opt->post_context || opt->file_break) {
		/* Show hunk marks, except for the first file. */
		if (opt->last_shown)
			opt->show_hunk_mark = 1;
		/*
		 * If we're using threads then we can't easily identify
		 * the first file.  Always put hunk marks in that case
		 * and skip the very first one later in work_done().
		 */
		if (opt->output != std_output)
			opt->show_hunk_mark = 1;
	}
	opt->last_shown = 0;

	switch (opt->binary) {
	case GREP_BINARY_DEFAULT:
		if (buffer_is_binary(buf, size))
			binary_match_only = 1;
		break;
	case GREP_BINARY_NOMATCH:
		if (buffer_is_binary(buf, size))
			return 0; /* Assume unmatch */
		break;
	case GREP_BINARY_TEXT:
		break;
	default:
		die("bug: unknown binary handling mode");
	}

	memset(&xecfg, 0, sizeof(xecfg));
	if (opt->funcname && !opt->unmatch_name_only && !opt->status_only &&
	    !opt->name_only && !binary_match_only && !collect_hits) {
		struct userdiff_driver *drv = userdiff_find_by_path(name);
		if (drv && drv->funcname.pattern) {
			const struct userdiff_funcname *pe = &drv->funcname;
			xdiff_set_find_func(&xecfg, pe->pattern, pe->cflags);
			opt->priv = &xecfg;
		}
	}
	try_lookahead = should_lookahead(opt);

	while (left) {
		char *eol, ch;
		int hit;

		/*
		 * look_ahead() skips quickly to the line that possibly
		 * has the next hit; don't call it if we need to do
		 * something more than just skipping the current line
		 * in response to an unmatch for the current line.  E.g.
		 * inside a post-context window, we will show the current
		 * line as a context around the previous hit when it
		 * doesn't hit.
		 */
		if (try_lookahead
		    && !(last_hit
			 && lno <= last_hit + opt->post_context)
		    && look_ahead(opt, &left, &lno, &bol))
			break;
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
			if (opt->name_only) {
				show_name(opt, name);
				return 1;
			}
			if (opt->count)
				goto next_line;
			if (binary_match_only) {
				opt->output(opt, "Binary file ", 12);
				output_color(opt, name, strlen(name),
					     opt->color_filename);
				opt->output(opt, " matches\n", 9);
				return 1;
			}
			/* Hit at this line.  If we haven't shown the
			 * pre-context lines, we would need to show them.
			 */
			if (opt->pre_context)
				show_pre_context(opt, name, buf, bol, lno);
			else if (opt->funcname)
				show_funcname_line(opt, name, buf, bol, lno);
			show_line(opt, bol, eol, name, lno, ':');
			last_hit = lno;
		}
		else if (last_hit &&
			 lno <= last_hit + opt->post_context) {
			/* If the last hit is within the post context,
			 * we need to show this line.
			 */
			show_line(opt, bol, eol, name, lno, '-');
		}

	next_line:
		bol = eol + 1;
		if (!left)
			break;
		left--;
		lno++;
	}

	if (collect_hits)
		return 0;

	if (opt->status_only)
		return 0;
	if (opt->unmatch_name_only) {
		/* We did not see any hit, so we want to show this */
		show_name(opt, name);
		return 1;
	}

	xdiff_clear_find_func(&xecfg);
	opt->priv = NULL;

	/* NEEDSWORK:
	 * The real "grep -c foo *.c" gives many "bar.c:0" lines,
	 * which feels mostly useless but sometimes useful.  Maybe
	 * make it another option?  For now suppress them.
	 */
	if (opt->count && count) {
		char buf[32];
		output_color(opt, name, strlen(name), opt->color_filename);
		output_sep(opt, ':');
		snprintf(buf, sizeof(buf), "%u\n", count);
		opt->output(opt, buf, strlen(buf));
		return 1;
	}
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
