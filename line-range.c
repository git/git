#include "git-compat-util.h"
#include "line-range.h"
#include "xdiff-interface.h"
#include "strbuf.h"
#include "userdiff.h"

/*
 * Parse one item in the -L option
 *
 * 'begin' is applicable only to relative range anchors. Absolute anchors
 * ignore this value.
 *
 * When parsing "-L A,B", parse_loc() is called once for A and once for B.
 *
 * When parsing A, 'begin' must be a negative number, the absolute value of
 * which is the line at which relative start-of-range anchors should be
 * based. Beginning of file is represented by -1.
 *
 * When parsing B, 'begin' must be the positive line number immediately
 * following the line computed for 'A'.
 */
static const char *parse_loc(const char *spec, nth_line_fn_t nth_line,
			     void *data, long lines, long begin, long *ret)
{
	char *term;
	const char *line;
	long num;
	int reg_error;
	regex_t regexp;
	regmatch_t match[1];

	/* Allow "-L <something>,+20" to mean starting at <something>
	 * for 20 lines, or "-L <something>,-5" for 5 lines ending at
	 * <something>.
	 */
	if (1 <= begin && (spec[0] == '+' || spec[0] == '-')) {
		num = strtol(spec + 1, &term, 10);
		if (term != spec + 1) {
			if (!ret)
				return term;
			if (num == 0)
				die("-L invalid empty range");
			if (spec[0] == '-')
				num = 0 - num;
			if (0 < num)
				*ret = begin + num - 2;
			else if (!num)
				*ret = begin;
			else
				*ret = begin + num > 0 ? begin + num : 1;
			return term;
		}
		return spec;
	}
	num = strtol(spec, &term, 10);
	if (term != spec) {
		if (ret) {
			if (num <= 0)
				die("-L invalid line number: %ld", num);
			*ret = num;
		}
		return term;
	}

	if (begin < 0) {
		if (spec[0] != '^')
			begin = -begin;
		else {
			begin = 1;
			spec++;
		}
	}

	if (spec[0] != '/')
		return spec;

	/* it could be a regexp of form /.../ */
	for (term = (char *) spec + 1; *term && *term != '/'; term++) {
		if (*term == '\\')
			term++;
	}
	if (*term != '/')
		return spec;

	/* in the scan-only case we are not interested in the regex */
	if (!ret)
		return term+1;

	/* try [spec+1 .. term-1] as regexp */
	*term = 0;
	begin--; /* input is in human terms */
	line = nth_line(data, begin);

	if (!(reg_error = regcomp(&regexp, spec + 1, REG_NEWLINE)) &&
	    !(reg_error = regexec(&regexp, line, 1, match, 0))) {
		const char *cp = line + match[0].rm_so;
		const char *nline;

		while (begin++ < lines) {
			nline = nth_line(data, begin);
			if (line <= cp && cp < nline)
				break;
			line = nline;
		}
		*ret = begin;
		regfree(&regexp);
		*term++ = '/';
		return term;
	}
	else {
		char errbuf[1024];
		regerror(reg_error, &regexp, errbuf, 1024);
		die("-L parameter '%s' starting at line %ld: %s",
		    spec + 1, begin + 1, errbuf);
	}
}

static int match_funcname(xdemitconf_t *xecfg, const char *bol, const char *eol)
{
	if (xecfg) {
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

static const char *find_funcname_matching_regexp(xdemitconf_t *xecfg, const char *start,
						 regex_t *regexp)
{
	int reg_error;
	regmatch_t match[1];
	while (1) {
		const char *bol, *eol;
		reg_error = regexec(regexp, start, 1, match, 0);
		if (reg_error == REG_NOMATCH)
			return NULL;
		else if (reg_error) {
			char errbuf[1024];
			regerror(reg_error, regexp, errbuf, 1024);
			die("-L parameter: regexec() failed: %s", errbuf);
		}
		/* determine extent of line matched */
		bol = start+match[0].rm_so;
		eol = start+match[0].rm_eo;
		while (bol > start && *bol != '\n')
			bol--;
		if (*bol == '\n')
			bol++;
		while (*eol && *eol != '\n')
			eol++;
		if (*eol == '\n')
			eol++;
		/* is it a funcname line? */
		if (match_funcname(xecfg, (char*) bol, (char*) eol))
			return bol;
		start = eol;
	}
}

static const char *parse_range_funcname(
	const char *arg, nth_line_fn_t nth_line_cb,
	void *cb_data, long lines, long anchor, long *begin, long *end,
	const char *path, struct index_state *istate)
{
	char *pattern;
	const char *term;
	struct userdiff_driver *drv;
	xdemitconf_t *xecfg = NULL;
	const char *start;
	const char *p;
	int reg_error;
	regex_t regexp;

	if (*arg == '^') {
		anchor = 1;
		arg++;
	}

	assert(*arg == ':');
	term = arg+1;
	while (*term && *term != ':') {
		if (*term == '\\' && *(term+1))
			term++;
		term++;
	}
	if (term == arg+1)
		return NULL;
	if (!begin) /* skip_range_arg case */
		return term;

	pattern = xstrndup(arg+1, term-(arg+1));

	anchor--; /* input is in human terms */
	start = nth_line_cb(cb_data, anchor);

	drv = userdiff_find_by_path(istate, path);
	if (drv && drv->funcname.pattern) {
		const struct userdiff_funcname *pe = &drv->funcname;
		CALLOC_ARRAY(xecfg, 1);
		xdiff_set_find_func(xecfg, pe->pattern, pe->cflags);
	}

	reg_error = regcomp(&regexp, pattern, REG_NEWLINE);
	if (reg_error) {
		char errbuf[1024];
		regerror(reg_error, &regexp, errbuf, 1024);
		die("-L parameter '%s': %s", pattern, errbuf);
	}

	p = find_funcname_matching_regexp(xecfg, (char*) start, &regexp);
	if (!p)
		die("-L parameter '%s' starting at line %ld: no match",
		    pattern, anchor + 1);
	*begin = 0;
	while (p > nth_line_cb(cb_data, *begin))
		(*begin)++;

	if (*begin >= lines)
		die("-L parameter '%s' matches at EOF", pattern);

	*end = *begin+1;
	while (*end < lines) {
		const char *bol = nth_line_cb(cb_data, *end);
		const char *eol = nth_line_cb(cb_data, *end+1);
		if (match_funcname(xecfg, bol, eol))
			break;
		(*end)++;
	}

	regfree(&regexp);
	free(xecfg);
	free(pattern);

	/* compensate for 1-based numbering */
	(*begin)++;

	return term;
}

int parse_range_arg(const char *arg, nth_line_fn_t nth_line_cb,
		    void *cb_data, long lines, long anchor,
		    long *begin, long *end,
		    const char *path, struct index_state *istate)
{
	*begin = *end = 0;

	if (anchor < 1)
		anchor = 1;
	if (anchor > lines)
		anchor = lines + 1;

	if (*arg == ':' || (*arg == '^' && *(arg + 1) == ':')) {
		arg = parse_range_funcname(arg, nth_line_cb, cb_data,
					   lines, anchor, begin, end,
					   path, istate);
		if (!arg || *arg)
			return -1;
		return 0;
	}

	arg = parse_loc(arg, nth_line_cb, cb_data, lines, -anchor, begin);

	if (*arg == ',')
		arg = parse_loc(arg + 1, nth_line_cb, cb_data, lines, *begin + 1, end);

	if (*arg)
		return -1;

	if (*begin && *end && *end < *begin) {
		SWAP(*end, *begin);
	}

	return 0;
}

const char *skip_range_arg(const char *arg, struct index_state *istate)
{
	if (*arg == ':' || (*arg == '^' && *(arg + 1) == ':'))
		return parse_range_funcname(arg, NULL, NULL,
					    0, 0, NULL, NULL,
					    NULL, istate);

	arg = parse_loc(arg, NULL, NULL, 0, -1, NULL);

	if (*arg == ',')
		arg = parse_loc(arg+1, NULL, NULL, 0, 0, NULL);

	return arg;
}
