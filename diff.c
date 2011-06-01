/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "quote.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "xdiff-interface.h"
#include "color.h"
#include "attr.h"
#include "run-command.h"
#include "utf8.h"
#include "userdiff.h"
#include "sigchain.h"
#include "submodule.h"
#include "ll-merge.h"

#ifdef NO_FAST_WORKING_DIRECTORY
#define FAST_WORKING_DIRECTORY 0
#else
#define FAST_WORKING_DIRECTORY 1
#endif

static int diff_detect_rename_default;
static int diff_rename_limit_default = 400;
static int diff_suppress_blank_empty;
int diff_use_color_default = -1;
static const char *diff_word_regex_cfg;
static const char *external_diff_cmd_cfg;
int diff_auto_refresh_index = 1;
static int diff_mnemonic_prefix;
static int diff_no_prefix;
static int diff_dirstat_permille_default = 30;
static struct diff_options default_diff_options;

static char diff_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,	/* PLAIN */
	GIT_COLOR_BOLD,		/* METAINFO */
	GIT_COLOR_CYAN,		/* FRAGINFO */
	GIT_COLOR_RED,		/* OLD */
	GIT_COLOR_GREEN,	/* NEW */
	GIT_COLOR_YELLOW,	/* COMMIT */
	GIT_COLOR_BG_RED,	/* WHITESPACE */
	GIT_COLOR_NORMAL,	/* FUNCINFO */
};

static int parse_diff_color_slot(const char *var, int ofs)
{
	if (!strcasecmp(var+ofs, "plain"))
		return DIFF_PLAIN;
	if (!strcasecmp(var+ofs, "meta"))
		return DIFF_METAINFO;
	if (!strcasecmp(var+ofs, "frag"))
		return DIFF_FRAGINFO;
	if (!strcasecmp(var+ofs, "old"))
		return DIFF_FILE_OLD;
	if (!strcasecmp(var+ofs, "new"))
		return DIFF_FILE_NEW;
	if (!strcasecmp(var+ofs, "commit"))
		return DIFF_COMMIT;
	if (!strcasecmp(var+ofs, "whitespace"))
		return DIFF_WHITESPACE;
	if (!strcasecmp(var+ofs, "func"))
		return DIFF_FUNCINFO;
	return -1;
}

static int parse_dirstat_params(struct diff_options *options, const char *params,
				struct strbuf *errmsg)
{
	const char *p = params;
	int p_len, ret = 0;

	while (*p) {
		p_len = strchrnul(p, ',') - p;
		if (!memcmp(p, "changes", p_len)) {
			DIFF_OPT_CLR(options, DIRSTAT_BY_LINE);
			DIFF_OPT_CLR(options, DIRSTAT_BY_FILE);
		} else if (!memcmp(p, "lines", p_len)) {
			DIFF_OPT_SET(options, DIRSTAT_BY_LINE);
			DIFF_OPT_CLR(options, DIRSTAT_BY_FILE);
		} else if (!memcmp(p, "files", p_len)) {
			DIFF_OPT_CLR(options, DIRSTAT_BY_LINE);
			DIFF_OPT_SET(options, DIRSTAT_BY_FILE);
		} else if (!memcmp(p, "noncumulative", p_len)) {
			DIFF_OPT_CLR(options, DIRSTAT_CUMULATIVE);
		} else if (!memcmp(p, "cumulative", p_len)) {
			DIFF_OPT_SET(options, DIRSTAT_CUMULATIVE);
		} else if (isdigit(*p)) {
			char *end;
			int permille = strtoul(p, &end, 10) * 10;
			if (*end == '.' && isdigit(*++end)) {
				/* only use first digit */
				permille += *end - '0';
				/* .. and ignore any further digits */
				while (isdigit(*++end))
					; /* nothing */
			}
			if (end - p == p_len)
				options->dirstat_permille = permille;
			else {
				strbuf_addf(errmsg, _("  Failed to parse dirstat cut-off percentage '%.*s'\n"),
					    p_len, p);
				ret++;
			}
		} else {
			strbuf_addf(errmsg, _("  Unknown dirstat parameter '%.*s'\n"),
				    p_len, p);
			ret++;
		}

		p += p_len;

		if (*p)
			p++; /* more parameters, swallow separator */
	}
	return ret;
}

static int git_config_rename(const char *var, const char *value)
{
	if (!value)
		return DIFF_DETECT_RENAME;
	if (!strcasecmp(value, "copies") || !strcasecmp(value, "copy"))
		return  DIFF_DETECT_COPY;
	return git_config_bool(var,value) ? DIFF_DETECT_RENAME : 0;
}

/*
 * These are to give UI layer defaults.
 * The core-level commands such as git-diff-files should
 * never be affected by the setting of diff.renames
 * the user happens to have in the configuration file.
 */
int git_diff_ui_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff")) {
		diff_use_color_default = git_config_colorbool(var, value, -1);
		return 0;
	}
	if (!strcmp(var, "diff.renames")) {
		diff_detect_rename_default = git_config_rename(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.autorefreshindex")) {
		diff_auto_refresh_index = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.mnemonicprefix")) {
		diff_mnemonic_prefix = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.noprefix")) {
		diff_no_prefix = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.external"))
		return git_config_string(&external_diff_cmd_cfg, var, value);
	if (!strcmp(var, "diff.wordregex"))
		return git_config_string(&diff_word_regex_cfg, var, value);

	if (!strcmp(var, "diff.ignoresubmodules"))
		handle_ignore_submodules_arg(&default_diff_options, value);

	return git_diff_basic_config(var, value, cb);
}

int git_diff_basic_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "diff.renamelimit")) {
		diff_rename_limit_default = git_config_int(var, value);
		return 0;
	}

	switch (userdiff_config(var, value)) {
		case 0: break;
		case -1: return -1;
		default: return 0;
	}

	if (!prefixcmp(var, "diff.color.") || !prefixcmp(var, "color.diff.")) {
		int slot = parse_diff_color_slot(var, 11);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, diff_colors[slot]);
		return 0;
	}

	/* like GNU diff's --suppress-blank-empty option  */
	if (!strcmp(var, "diff.suppressblankempty") ||
			/* for backwards compatibility */
			!strcmp(var, "diff.suppress-blank-empty")) {
		diff_suppress_blank_empty = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "diff.dirstat")) {
		struct strbuf errmsg = STRBUF_INIT;
		default_diff_options.dirstat_permille = diff_dirstat_permille_default;
		if (parse_dirstat_params(&default_diff_options, value, &errmsg))
			warning(_("Found errors in 'diff.dirstat' config variable:\n%s"),
				errmsg.buf);
		strbuf_release(&errmsg);
		diff_dirstat_permille_default = default_diff_options.dirstat_permille;
		return 0;
	}

	if (!prefixcmp(var, "submodule."))
		return parse_submodule_config_option(var, value);

	return git_color_default_config(var, value, cb);
}

static char *quote_two(const char *one, const char *two)
{
	int need_one = quote_c_style(one, NULL, NULL, 1);
	int need_two = quote_c_style(two, NULL, NULL, 1);
	struct strbuf res = STRBUF_INIT;

	if (need_one + need_two) {
		strbuf_addch(&res, '"');
		quote_c_style(one, &res, NULL, 1);
		quote_c_style(two, &res, NULL, 1);
		strbuf_addch(&res, '"');
	} else {
		strbuf_addstr(&res, one);
		strbuf_addstr(&res, two);
	}
	return strbuf_detach(&res, NULL);
}

static const char *external_diff(void)
{
	static const char *external_diff_cmd = NULL;
	static int done_preparing = 0;

	if (done_preparing)
		return external_diff_cmd;
	external_diff_cmd = getenv("GIT_EXTERNAL_DIFF");
	if (!external_diff_cmd)
		external_diff_cmd = external_diff_cmd_cfg;
	done_preparing = 1;
	return external_diff_cmd;
}

static struct diff_tempfile {
	const char *name; /* filename external diff should read from */
	char hex[41];
	char mode[10];
	char tmp_path[PATH_MAX];
} diff_temp[2];

typedef unsigned long (*sane_truncate_fn)(char *line, unsigned long len);

struct emit_callback {
	int color_diff;
	unsigned ws_rule;
	int blank_at_eof_in_preimage;
	int blank_at_eof_in_postimage;
	int lno_in_preimage;
	int lno_in_postimage;
	sane_truncate_fn truncate;
	const char **label_path;
	struct diff_words_data *diff_words;
	struct diff_options *opt;
	int *found_changesp;
	struct strbuf *header;
};

static int count_lines(const char *data, int size)
{
	int count, ch, completely_empty = 1, nl_just_seen = 0;
	count = 0;
	while (0 < size--) {
		ch = *data++;
		if (ch == '\n') {
			count++;
			nl_just_seen = 1;
			completely_empty = 0;
		}
		else {
			nl_just_seen = 0;
			completely_empty = 0;
		}
	}
	if (completely_empty)
		return 0;
	if (!nl_just_seen)
		count++; /* no trailing newline */
	return count;
}

static int fill_mmfile(mmfile_t *mf, struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one)) {
		mf->ptr = (char *)""; /* does not matter */
		mf->size = 0;
		return 0;
	}
	else if (diff_populate_filespec(one, 0))
		return -1;

	mf->ptr = one->data;
	mf->size = one->size;
	return 0;
}

/* like fill_mmfile, but only for size, so we can avoid retrieving blob */
static unsigned long diff_filespec_size(struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one))
		return 0;
	diff_populate_filespec(one, 1);
	return one->size;
}

static int count_trailing_blank(mmfile_t *mf, unsigned ws_rule)
{
	char *ptr = mf->ptr;
	long size = mf->size;
	int cnt = 0;

	if (!size)
		return cnt;
	ptr += size - 1; /* pointing at the very end */
	if (*ptr != '\n')
		; /* incomplete line */
	else
		ptr--; /* skip the last LF */
	while (mf->ptr < ptr) {
		char *prev_eol;
		for (prev_eol = ptr; mf->ptr <= prev_eol; prev_eol--)
			if (*prev_eol == '\n')
				break;
		if (!ws_blank_line(prev_eol + 1, ptr - prev_eol, ws_rule))
			break;
		cnt++;
		ptr = prev_eol - 1;
	}
	return cnt;
}

static void check_blank_at_eof(mmfile_t *mf1, mmfile_t *mf2,
			       struct emit_callback *ecbdata)
{
	int l1, l2, at;
	unsigned ws_rule = ecbdata->ws_rule;
	l1 = count_trailing_blank(mf1, ws_rule);
	l2 = count_trailing_blank(mf2, ws_rule);
	if (l2 <= l1) {
		ecbdata->blank_at_eof_in_preimage = 0;
		ecbdata->blank_at_eof_in_postimage = 0;
		return;
	}
	at = count_lines(mf1->ptr, mf1->size);
	ecbdata->blank_at_eof_in_preimage = (at - l1) + 1;

	at = count_lines(mf2->ptr, mf2->size);
	ecbdata->blank_at_eof_in_postimage = (at - l2) + 1;
}

static void emit_line_0(struct diff_options *o, const char *set, const char *reset,
			int first, const char *line, int len)
{
	int has_trailing_newline, has_trailing_carriage_return;
	int nofirst;
	FILE *file = o->file;

	if (o->output_prefix) {
		struct strbuf *msg = NULL;
		msg = o->output_prefix(o, o->output_prefix_data);
		assert(msg);
		fwrite(msg->buf, msg->len, 1, file);
	}

	if (len == 0) {
		has_trailing_newline = (first == '\n');
		has_trailing_carriage_return = (!has_trailing_newline &&
						(first == '\r'));
		nofirst = has_trailing_newline || has_trailing_carriage_return;
	} else {
		has_trailing_newline = (len > 0 && line[len-1] == '\n');
		if (has_trailing_newline)
			len--;
		has_trailing_carriage_return = (len > 0 && line[len-1] == '\r');
		if (has_trailing_carriage_return)
			len--;
		nofirst = 0;
	}

	if (len || !nofirst) {
		fputs(set, file);
		if (!nofirst)
			fputc(first, file);
		fwrite(line, len, 1, file);
		fputs(reset, file);
	}
	if (has_trailing_carriage_return)
		fputc('\r', file);
	if (has_trailing_newline)
		fputc('\n', file);
}

static void emit_line(struct diff_options *o, const char *set, const char *reset,
		      const char *line, int len)
{
	emit_line_0(o, set, reset, line[0], line+1, len-1);
}

static int new_blank_line_at_eof(struct emit_callback *ecbdata, const char *line, int len)
{
	if (!((ecbdata->ws_rule & WS_BLANK_AT_EOF) &&
	      ecbdata->blank_at_eof_in_preimage &&
	      ecbdata->blank_at_eof_in_postimage &&
	      ecbdata->blank_at_eof_in_preimage <= ecbdata->lno_in_preimage &&
	      ecbdata->blank_at_eof_in_postimage <= ecbdata->lno_in_postimage))
		return 0;
	return ws_blank_line(line, len, ecbdata->ws_rule);
}

static void emit_add_line(const char *reset,
			  struct emit_callback *ecbdata,
			  const char *line, int len)
{
	const char *ws = diff_get_color(ecbdata->color_diff, DIFF_WHITESPACE);
	const char *set = diff_get_color(ecbdata->color_diff, DIFF_FILE_NEW);

	if (!*ws)
		emit_line_0(ecbdata->opt, set, reset, '+', line, len);
	else if (new_blank_line_at_eof(ecbdata, line, len))
		/* Blank line at EOF - paint '+' as well */
		emit_line_0(ecbdata->opt, ws, reset, '+', line, len);
	else {
		/* Emit just the prefix, then the rest. */
		emit_line_0(ecbdata->opt, set, reset, '+', "", 0);
		ws_check_emit(line, len, ecbdata->ws_rule,
			      ecbdata->opt->file, set, reset, ws);
	}
}

static void emit_hunk_header(struct emit_callback *ecbdata,
			     const char *line, int len)
{
	const char *plain = diff_get_color(ecbdata->color_diff, DIFF_PLAIN);
	const char *frag = diff_get_color(ecbdata->color_diff, DIFF_FRAGINFO);
	const char *func = diff_get_color(ecbdata->color_diff, DIFF_FUNCINFO);
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);
	static const char atat[2] = { '@', '@' };
	const char *cp, *ep;
	struct strbuf msgbuf = STRBUF_INIT;
	int org_len = len;
	int i = 1;

	/*
	 * As a hunk header must begin with "@@ -<old>, +<new> @@",
	 * it always is at least 10 bytes long.
	 */
	if (len < 10 ||
	    memcmp(line, atat, 2) ||
	    !(ep = memmem(line + 2, len - 2, atat, 2))) {
		emit_line(ecbdata->opt, plain, reset, line, len);
		return;
	}
	ep += 2; /* skip over @@ */

	/* The hunk header in fraginfo color */
	strbuf_add(&msgbuf, frag, strlen(frag));
	strbuf_add(&msgbuf, line, ep - line);
	strbuf_add(&msgbuf, reset, strlen(reset));

	/*
	 * trailing "\r\n"
	 */
	for ( ; i < 3; i++)
		if (line[len - i] == '\r' || line[len - i] == '\n')
			len--;

	/* blank before the func header */
	for (cp = ep; ep - line < len; ep++)
		if (*ep != ' ' && *ep != '\t')
			break;
	if (ep != cp) {
		strbuf_add(&msgbuf, plain, strlen(plain));
		strbuf_add(&msgbuf, cp, ep - cp);
		strbuf_add(&msgbuf, reset, strlen(reset));
	}

	if (ep < line + len) {
		strbuf_add(&msgbuf, func, strlen(func));
		strbuf_add(&msgbuf, ep, line + len - ep);
		strbuf_add(&msgbuf, reset, strlen(reset));
	}

	strbuf_add(&msgbuf, line + len, org_len - len);
	emit_line(ecbdata->opt, "", "", msgbuf.buf, msgbuf.len);
	strbuf_release(&msgbuf);
}

static struct diff_tempfile *claim_diff_tempfile(void) {
	int i;
	for (i = 0; i < ARRAY_SIZE(diff_temp); i++)
		if (!diff_temp[i].name)
			return diff_temp + i;
	die("BUG: diff is failing to clean up its tempfiles");
}

static int remove_tempfile_installed;

static void remove_tempfile(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(diff_temp); i++) {
		if (diff_temp[i].name == diff_temp[i].tmp_path)
			unlink_or_warn(diff_temp[i].name);
		diff_temp[i].name = NULL;
	}
}

static void remove_tempfile_on_signal(int signo)
{
	remove_tempfile();
	sigchain_pop(signo);
	raise(signo);
}

static void print_line_count(FILE *file, int count)
{
	switch (count) {
	case 0:
		fprintf(file, "0,0");
		break;
	case 1:
		fprintf(file, "1");
		break;
	default:
		fprintf(file, "1,%d", count);
		break;
	}
}

static void emit_rewrite_lines(struct emit_callback *ecb,
			       int prefix, const char *data, int size)
{
	const char *endp = NULL;
	static const char *nneof = " No newline at end of file\n";
	const char *old = diff_get_color(ecb->color_diff, DIFF_FILE_OLD);
	const char *reset = diff_get_color(ecb->color_diff, DIFF_RESET);

	while (0 < size) {
		int len;

		endp = memchr(data, '\n', size);
		len = endp ? (endp - data + 1) : size;
		if (prefix != '+') {
			ecb->lno_in_preimage++;
			emit_line_0(ecb->opt, old, reset, '-',
				    data, len);
		} else {
			ecb->lno_in_postimage++;
			emit_add_line(reset, ecb, data, len);
		}
		size -= len;
		data += len;
	}
	if (!endp) {
		const char *plain = diff_get_color(ecb->color_diff,
						   DIFF_PLAIN);
		emit_line_0(ecb->opt, plain, reset, '\\',
			    nneof, strlen(nneof));
	}
}

static void emit_rewrite_diff(const char *name_a,
			      const char *name_b,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      struct userdiff_driver *textconv_one,
			      struct userdiff_driver *textconv_two,
			      struct diff_options *o)
{
	int lc_a, lc_b;
	int color_diff = DIFF_OPT_TST(o, COLOR_DIFF);
	const char *name_a_tab, *name_b_tab;
	const char *metainfo = diff_get_color(color_diff, DIFF_METAINFO);
	const char *fraginfo = diff_get_color(color_diff, DIFF_FRAGINFO);
	const char *reset = diff_get_color(color_diff, DIFF_RESET);
	static struct strbuf a_name = STRBUF_INIT, b_name = STRBUF_INIT;
	const char *a_prefix, *b_prefix;
	char *data_one, *data_two;
	size_t size_one, size_two;
	struct emit_callback ecbdata;
	char *line_prefix = "";
	struct strbuf *msgbuf;

	if (o && o->output_prefix) {
		msgbuf = o->output_prefix(o, o->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	if (diff_mnemonic_prefix && DIFF_OPT_TST(o, REVERSE_DIFF)) {
		a_prefix = o->b_prefix;
		b_prefix = o->a_prefix;
	} else {
		a_prefix = o->a_prefix;
		b_prefix = o->b_prefix;
	}

	name_a += (*name_a == '/');
	name_b += (*name_b == '/');
	name_a_tab = strchr(name_a, ' ') ? "\t" : "";
	name_b_tab = strchr(name_b, ' ') ? "\t" : "";

	strbuf_reset(&a_name);
	strbuf_reset(&b_name);
	quote_two_c_style(&a_name, a_prefix, name_a, 0);
	quote_two_c_style(&b_name, b_prefix, name_b, 0);

	size_one = fill_textconv(textconv_one, one, &data_one);
	size_two = fill_textconv(textconv_two, two, &data_two);

	memset(&ecbdata, 0, sizeof(ecbdata));
	ecbdata.color_diff = color_diff;
	ecbdata.found_changesp = &o->found_changes;
	ecbdata.ws_rule = whitespace_rule(name_b ? name_b : name_a);
	ecbdata.opt = o;
	if (ecbdata.ws_rule & WS_BLANK_AT_EOF) {
		mmfile_t mf1, mf2;
		mf1.ptr = (char *)data_one;
		mf2.ptr = (char *)data_two;
		mf1.size = size_one;
		mf2.size = size_two;
		check_blank_at_eof(&mf1, &mf2, &ecbdata);
	}
	ecbdata.lno_in_preimage = 1;
	ecbdata.lno_in_postimage = 1;

	lc_a = count_lines(data_one, size_one);
	lc_b = count_lines(data_two, size_two);
	fprintf(o->file,
		"%s%s--- %s%s%s\n%s%s+++ %s%s%s\n%s%s@@ -",
		line_prefix, metainfo, a_name.buf, name_a_tab, reset,
		line_prefix, metainfo, b_name.buf, name_b_tab, reset,
		line_prefix, fraginfo);
	if (!o->irreversible_delete)
		print_line_count(o->file, lc_a);
	else
		fprintf(o->file, "?,?");
	fprintf(o->file, " +");
	print_line_count(o->file, lc_b);
	fprintf(o->file, " @@%s\n", reset);
	if (lc_a && !o->irreversible_delete)
		emit_rewrite_lines(&ecbdata, '-', data_one, size_one);
	if (lc_b)
		emit_rewrite_lines(&ecbdata, '+', data_two, size_two);
	if (textconv_one)
		free((char *)data_one);
	if (textconv_two)
		free((char *)data_two);
}

struct diff_words_buffer {
	mmfile_t text;
	long alloc;
	struct diff_words_orig {
		const char *begin, *end;
	} *orig;
	int orig_nr, orig_alloc;
};

static void diff_words_append(char *line, unsigned long len,
		struct diff_words_buffer *buffer)
{
	ALLOC_GROW(buffer->text.ptr, buffer->text.size + len, buffer->alloc);
	line++;
	len--;
	memcpy(buffer->text.ptr + buffer->text.size, line, len);
	buffer->text.size += len;
	buffer->text.ptr[buffer->text.size] = '\0';
}

struct diff_words_style_elem {
	const char *prefix;
	const char *suffix;
	const char *color; /* NULL; filled in by the setup code if
			    * color is enabled */
};

struct diff_words_style {
	enum diff_words_type type;
	struct diff_words_style_elem new, old, ctx;
	const char *newline;
};

static struct diff_words_style diff_words_styles[] = {
	{ DIFF_WORDS_PORCELAIN, {"+", "\n"}, {"-", "\n"}, {" ", "\n"}, "~\n" },
	{ DIFF_WORDS_PLAIN, {"{+", "+}"}, {"[-", "-]"}, {"", ""}, "\n" },
	{ DIFF_WORDS_COLOR, {"", ""}, {"", ""}, {"", ""}, "\n" }
};

struct diff_words_data {
	struct diff_words_buffer minus, plus;
	const char *current_plus;
	int last_minus;
	struct diff_options *opt;
	regex_t *word_regex;
	enum diff_words_type type;
	struct diff_words_style *style;
};

static int fn_out_diff_words_write_helper(FILE *fp,
					  struct diff_words_style_elem *st_el,
					  const char *newline,
					  size_t count, const char *buf,
					  const char *line_prefix)
{
	int print = 0;

	while (count) {
		char *p = memchr(buf, '\n', count);
		if (print)
			fputs(line_prefix, fp);
		if (p != buf) {
			if (st_el->color && fputs(st_el->color, fp) < 0)
				return -1;
			if (fputs(st_el->prefix, fp) < 0 ||
			    fwrite(buf, p ? p - buf : count, 1, fp) != 1 ||
			    fputs(st_el->suffix, fp) < 0)
				return -1;
			if (st_el->color && *st_el->color
			    && fputs(GIT_COLOR_RESET, fp) < 0)
				return -1;
		}
		if (!p)
			return 0;
		if (fputs(newline, fp) < 0)
			return -1;
		count -= p + 1 - buf;
		buf = p + 1;
		print = 1;
	}
	return 0;
}

/*
 * '--color-words' algorithm can be described as:
 *
 *   1. collect a the minus/plus lines of a diff hunk, divided into
 *      minus-lines and plus-lines;
 *
 *   2. break both minus-lines and plus-lines into words and
 *      place them into two mmfile_t with one word for each line;
 *
 *   3. use xdiff to run diff on the two mmfile_t to get the words level diff;
 *
 * And for the common parts of the both file, we output the plus side text.
 * diff_words->current_plus is used to trace the current position of the plus file
 * which printed. diff_words->last_minus is used to trace the last minus word
 * printed.
 *
 * For '--graph' to work with '--color-words', we need to output the graph prefix
 * on each line of color words output. Generally, there are two conditions on
 * which we should output the prefix.
 *
 *   1. diff_words->last_minus == 0 &&
 *      diff_words->current_plus == diff_words->plus.text.ptr
 *
 *      that is: the plus text must start as a new line, and if there is no minus
 *      word printed, a graph prefix must be printed.
 *
 *   2. diff_words->current_plus > diff_words->plus.text.ptr &&
 *      *(diff_words->current_plus - 1) == '\n'
 *
 *      that is: a graph prefix must be printed following a '\n'
 */
static int color_words_output_graph_prefix(struct diff_words_data *diff_words)
{
	if ((diff_words->last_minus == 0 &&
		diff_words->current_plus == diff_words->plus.text.ptr) ||
		(diff_words->current_plus > diff_words->plus.text.ptr &&
		*(diff_words->current_plus - 1) == '\n')) {
		return 1;
	} else {
		return 0;
	}
}

static void fn_out_diff_words_aux(void *priv, char *line, unsigned long len)
{
	struct diff_words_data *diff_words = priv;
	struct diff_words_style *style = diff_words->style;
	int minus_first, minus_len, plus_first, plus_len;
	const char *minus_begin, *minus_end, *plus_begin, *plus_end;
	struct diff_options *opt = diff_words->opt;
	struct strbuf *msgbuf;
	char *line_prefix = "";

	if (line[0] != '@' || parse_hunk_header(line, len,
			&minus_first, &minus_len, &plus_first, &plus_len))
		return;

	assert(opt);
	if (opt->output_prefix) {
		msgbuf = opt->output_prefix(opt, opt->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	/* POSIX requires that first be decremented by one if len == 0... */
	if (minus_len) {
		minus_begin = diff_words->minus.orig[minus_first].begin;
		minus_end =
			diff_words->minus.orig[minus_first + minus_len - 1].end;
	} else
		minus_begin = minus_end =
			diff_words->minus.orig[minus_first].end;

	if (plus_len) {
		plus_begin = diff_words->plus.orig[plus_first].begin;
		plus_end = diff_words->plus.orig[plus_first + plus_len - 1].end;
	} else
		plus_begin = plus_end = diff_words->plus.orig[plus_first].end;

	if (color_words_output_graph_prefix(diff_words)) {
		fputs(line_prefix, diff_words->opt->file);
	}
	if (diff_words->current_plus != plus_begin) {
		fn_out_diff_words_write_helper(diff_words->opt->file,
				&style->ctx, style->newline,
				plus_begin - diff_words->current_plus,
				diff_words->current_plus, line_prefix);
		if (*(plus_begin - 1) == '\n')
			fputs(line_prefix, diff_words->opt->file);
	}
	if (minus_begin != minus_end) {
		fn_out_diff_words_write_helper(diff_words->opt->file,
				&style->old, style->newline,
				minus_end - minus_begin, minus_begin,
				line_prefix);
	}
	if (plus_begin != plus_end) {
		fn_out_diff_words_write_helper(diff_words->opt->file,
				&style->new, style->newline,
				plus_end - plus_begin, plus_begin,
				line_prefix);
	}

	diff_words->current_plus = plus_end;
	diff_words->last_minus = minus_first;
}

/* This function starts looking at *begin, and returns 0 iff a word was found. */
static int find_word_boundaries(mmfile_t *buffer, regex_t *word_regex,
		int *begin, int *end)
{
	if (word_regex && *begin < buffer->size) {
		regmatch_t match[1];
		if (!regexec(word_regex, buffer->ptr + *begin, 1, match, 0)) {
			char *p = memchr(buffer->ptr + *begin + match[0].rm_so,
					'\n', match[0].rm_eo - match[0].rm_so);
			*end = p ? p - buffer->ptr : match[0].rm_eo + *begin;
			*begin += match[0].rm_so;
			return *begin >= *end;
		}
		return -1;
	}

	/* find the next word */
	while (*begin < buffer->size && isspace(buffer->ptr[*begin]))
		(*begin)++;
	if (*begin >= buffer->size)
		return -1;

	/* find the end of the word */
	*end = *begin + 1;
	while (*end < buffer->size && !isspace(buffer->ptr[*end]))
		(*end)++;

	return 0;
}

/*
 * This function splits the words in buffer->text, stores the list with
 * newline separator into out, and saves the offsets of the original words
 * in buffer->orig.
 */
static void diff_words_fill(struct diff_words_buffer *buffer, mmfile_t *out,
		regex_t *word_regex)
{
	int i, j;
	long alloc = 0;

	out->size = 0;
	out->ptr = NULL;

	/* fake an empty "0th" word */
	ALLOC_GROW(buffer->orig, 1, buffer->orig_alloc);
	buffer->orig[0].begin = buffer->orig[0].end = buffer->text.ptr;
	buffer->orig_nr = 1;

	for (i = 0; i < buffer->text.size; i++) {
		if (find_word_boundaries(&buffer->text, word_regex, &i, &j))
			return;

		/* store original boundaries */
		ALLOC_GROW(buffer->orig, buffer->orig_nr + 1,
				buffer->orig_alloc);
		buffer->orig[buffer->orig_nr].begin = buffer->text.ptr + i;
		buffer->orig[buffer->orig_nr].end = buffer->text.ptr + j;
		buffer->orig_nr++;

		/* store one word */
		ALLOC_GROW(out->ptr, out->size + j - i + 1, alloc);
		memcpy(out->ptr + out->size, buffer->text.ptr + i, j - i);
		out->ptr[out->size + j - i] = '\n';
		out->size += j - i + 1;

		i = j - 1;
	}
}

/* this executes the word diff on the accumulated buffers */
static void diff_words_show(struct diff_words_data *diff_words)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	mmfile_t minus, plus;
	struct diff_words_style *style = diff_words->style;

	struct diff_options *opt = diff_words->opt;
	struct strbuf *msgbuf;
	char *line_prefix = "";

	assert(opt);
	if (opt->output_prefix) {
		msgbuf = opt->output_prefix(opt, opt->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	/* special case: only removal */
	if (!diff_words->plus.text.size) {
		fputs(line_prefix, diff_words->opt->file);
		fn_out_diff_words_write_helper(diff_words->opt->file,
			&style->old, style->newline,
			diff_words->minus.text.size,
			diff_words->minus.text.ptr, line_prefix);
		diff_words->minus.text.size = 0;
		return;
	}

	diff_words->current_plus = diff_words->plus.text.ptr;
	diff_words->last_minus = 0;

	memset(&xpp, 0, sizeof(xpp));
	memset(&xecfg, 0, sizeof(xecfg));
	diff_words_fill(&diff_words->minus, &minus, diff_words->word_regex);
	diff_words_fill(&diff_words->plus, &plus, diff_words->word_regex);
	xpp.flags = 0;
	/* as only the hunk header will be parsed, we need a 0-context */
	xecfg.ctxlen = 0;
	xdi_diff_outf(&minus, &plus, fn_out_diff_words_aux, diff_words,
		      &xpp, &xecfg);
	free(minus.ptr);
	free(plus.ptr);
	if (diff_words->current_plus != diff_words->plus.text.ptr +
			diff_words->plus.text.size) {
		if (color_words_output_graph_prefix(diff_words))
			fputs(line_prefix, diff_words->opt->file);
		fn_out_diff_words_write_helper(diff_words->opt->file,
			&style->ctx, style->newline,
			diff_words->plus.text.ptr + diff_words->plus.text.size
			- diff_words->current_plus, diff_words->current_plus,
			line_prefix);
	}
	diff_words->minus.text.size = diff_words->plus.text.size = 0;
}

/* In "color-words" mode, show word-diff of words accumulated in the buffer */
static void diff_words_flush(struct emit_callback *ecbdata)
{
	if (ecbdata->diff_words->minus.text.size ||
	    ecbdata->diff_words->plus.text.size)
		diff_words_show(ecbdata->diff_words);
}

static void free_diff_words_data(struct emit_callback *ecbdata)
{
	if (ecbdata->diff_words) {
		diff_words_flush(ecbdata);
		free (ecbdata->diff_words->minus.text.ptr);
		free (ecbdata->diff_words->minus.orig);
		free (ecbdata->diff_words->plus.text.ptr);
		free (ecbdata->diff_words->plus.orig);
		if (ecbdata->diff_words->word_regex) {
			regfree(ecbdata->diff_words->word_regex);
			free(ecbdata->diff_words->word_regex);
		}
		free(ecbdata->diff_words);
		ecbdata->diff_words = NULL;
	}
}

const char *diff_get_color(int diff_use_color, enum color_diff ix)
{
	if (diff_use_color)
		return diff_colors[ix];
	return "";
}

static unsigned long sane_truncate_line(struct emit_callback *ecb, char *line, unsigned long len)
{
	const char *cp;
	unsigned long allot;
	size_t l = len;

	if (ecb->truncate)
		return ecb->truncate(line, len);
	cp = line;
	allot = l;
	while (0 < l) {
		(void) utf8_width(&cp, &l);
		if (!cp)
			break; /* truncated in the middle? */
	}
	return allot - l;
}

static void find_lno(const char *line, struct emit_callback *ecbdata)
{
	const char *p;
	ecbdata->lno_in_preimage = 0;
	ecbdata->lno_in_postimage = 0;
	p = strchr(line, '-');
	if (!p)
		return; /* cannot happen */
	ecbdata->lno_in_preimage = strtol(p + 1, NULL, 10);
	p = strchr(p, '+');
	if (!p)
		return; /* cannot happen */
	ecbdata->lno_in_postimage = strtol(p + 1, NULL, 10);
}

static void fn_out_consume(void *priv, char *line, unsigned long len)
{
	struct emit_callback *ecbdata = priv;
	const char *meta = diff_get_color(ecbdata->color_diff, DIFF_METAINFO);
	const char *plain = diff_get_color(ecbdata->color_diff, DIFF_PLAIN);
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);
	struct diff_options *o = ecbdata->opt;
	char *line_prefix = "";
	struct strbuf *msgbuf;

	if (o && o->output_prefix) {
		msgbuf = o->output_prefix(o, o->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	if (ecbdata->header) {
		fprintf(ecbdata->opt->file, "%s", ecbdata->header->buf);
		strbuf_reset(ecbdata->header);
		ecbdata->header = NULL;
	}
	*(ecbdata->found_changesp) = 1;

	if (ecbdata->label_path[0]) {
		const char *name_a_tab, *name_b_tab;

		name_a_tab = strchr(ecbdata->label_path[0], ' ') ? "\t" : "";
		name_b_tab = strchr(ecbdata->label_path[1], ' ') ? "\t" : "";

		fprintf(ecbdata->opt->file, "%s%s--- %s%s%s\n",
			line_prefix, meta, ecbdata->label_path[0], reset, name_a_tab);
		fprintf(ecbdata->opt->file, "%s%s+++ %s%s%s\n",
			line_prefix, meta, ecbdata->label_path[1], reset, name_b_tab);
		ecbdata->label_path[0] = ecbdata->label_path[1] = NULL;
	}

	if (diff_suppress_blank_empty
	    && len == 2 && line[0] == ' ' && line[1] == '\n') {
		line[0] = '\n';
		len = 1;
	}

	if (line[0] == '@') {
		if (ecbdata->diff_words)
			diff_words_flush(ecbdata);
		len = sane_truncate_line(ecbdata, line, len);
		find_lno(line, ecbdata);
		emit_hunk_header(ecbdata, line, len);
		if (line[len-1] != '\n')
			putc('\n', ecbdata->opt->file);
		return;
	}

	if (len < 1) {
		emit_line(ecbdata->opt, reset, reset, line, len);
		if (ecbdata->diff_words
		    && ecbdata->diff_words->type == DIFF_WORDS_PORCELAIN)
			fputs("~\n", ecbdata->opt->file);
		return;
	}

	if (ecbdata->diff_words) {
		if (line[0] == '-') {
			diff_words_append(line, len,
					  &ecbdata->diff_words->minus);
			return;
		} else if (line[0] == '+') {
			diff_words_append(line, len,
					  &ecbdata->diff_words->plus);
			return;
		}
		diff_words_flush(ecbdata);
		if (ecbdata->diff_words->type == DIFF_WORDS_PORCELAIN) {
			emit_line(ecbdata->opt, plain, reset, line, len);
			fputs("~\n", ecbdata->opt->file);
		} else {
			/*
			 * Skip the prefix character, if any.  With
			 * diff_suppress_blank_empty, there may be
			 * none.
			 */
			if (line[0] != '\n') {
			      line++;
			      len--;
			}
			emit_line(ecbdata->opt, plain, reset, line, len);
		}
		return;
	}

	if (line[0] != '+') {
		const char *color =
			diff_get_color(ecbdata->color_diff,
				       line[0] == '-' ? DIFF_FILE_OLD : DIFF_PLAIN);
		ecbdata->lno_in_preimage++;
		if (line[0] == ' ')
			ecbdata->lno_in_postimage++;
		emit_line(ecbdata->opt, color, reset, line, len);
	} else {
		ecbdata->lno_in_postimage++;
		emit_add_line(reset, ecbdata, line + 1, len - 1);
	}
}

static char *pprint_rename(const char *a, const char *b)
{
	const char *old = a;
	const char *new = b;
	struct strbuf name = STRBUF_INIT;
	int pfx_length, sfx_length;
	int len_a = strlen(a);
	int len_b = strlen(b);
	int a_midlen, b_midlen;
	int qlen_a = quote_c_style(a, NULL, NULL, 0);
	int qlen_b = quote_c_style(b, NULL, NULL, 0);

	if (qlen_a || qlen_b) {
		quote_c_style(a, &name, NULL, 0);
		strbuf_addstr(&name, " => ");
		quote_c_style(b, &name, NULL, 0);
		return strbuf_detach(&name, NULL);
	}

	/* Find common prefix */
	pfx_length = 0;
	while (*old && *new && *old == *new) {
		if (*old == '/')
			pfx_length = old - a + 1;
		old++;
		new++;
	}

	/* Find common suffix */
	old = a + len_a;
	new = b + len_b;
	sfx_length = 0;
	while (a <= old && b <= new && *old == *new) {
		if (*old == '/')
			sfx_length = len_a - (old - a);
		old--;
		new--;
	}

	/*
	 * pfx{mid-a => mid-b}sfx
	 * {pfx-a => pfx-b}sfx
	 * pfx{sfx-a => sfx-b}
	 * name-a => name-b
	 */
	a_midlen = len_a - pfx_length - sfx_length;
	b_midlen = len_b - pfx_length - sfx_length;
	if (a_midlen < 0)
		a_midlen = 0;
	if (b_midlen < 0)
		b_midlen = 0;

	strbuf_grow(&name, pfx_length + a_midlen + b_midlen + sfx_length + 7);
	if (pfx_length + sfx_length) {
		strbuf_add(&name, a, pfx_length);
		strbuf_addch(&name, '{');
	}
	strbuf_add(&name, a + pfx_length, a_midlen);
	strbuf_addstr(&name, " => ");
	strbuf_add(&name, b + pfx_length, b_midlen);
	if (pfx_length + sfx_length) {
		strbuf_addch(&name, '}');
		strbuf_add(&name, a + len_a - sfx_length, sfx_length);
	}
	return strbuf_detach(&name, NULL);
}

struct diffstat_t {
	int nr;
	int alloc;
	struct diffstat_file {
		char *from_name;
		char *name;
		char *print_name;
		unsigned is_unmerged:1;
		unsigned is_binary:1;
		unsigned is_renamed:1;
		uintmax_t added, deleted;
	} **files;
};

static struct diffstat_file *diffstat_add(struct diffstat_t *diffstat,
					  const char *name_a,
					  const char *name_b)
{
	struct diffstat_file *x;
	x = xcalloc(sizeof (*x), 1);
	if (diffstat->nr == diffstat->alloc) {
		diffstat->alloc = alloc_nr(diffstat->alloc);
		diffstat->files = xrealloc(diffstat->files,
				diffstat->alloc * sizeof(x));
	}
	diffstat->files[diffstat->nr++] = x;
	if (name_b) {
		x->from_name = xstrdup(name_a);
		x->name = xstrdup(name_b);
		x->is_renamed = 1;
	}
	else {
		x->from_name = NULL;
		x->name = xstrdup(name_a);
	}
	return x;
}

static void diffstat_consume(void *priv, char *line, unsigned long len)
{
	struct diffstat_t *diffstat = priv;
	struct diffstat_file *x = diffstat->files[diffstat->nr - 1];

	if (line[0] == '+')
		x->added++;
	else if (line[0] == '-')
		x->deleted++;
}

const char mime_boundary_leader[] = "------------";

static int scale_linear(int it, int width, int max_change)
{
	/*
	 * make sure that at least one '-' is printed if there were deletions,
	 * and likewise for '+'.
	 */
	if (max_change < 2)
		return it;
	return ((it - 1) * (width - 1) + max_change - 1) / (max_change - 1);
}

static void show_name(FILE *file,
		      const char *prefix, const char *name, int len)
{
	fprintf(file, " %s%-*s |", prefix, len, name);
}

static void show_graph(FILE *file, char ch, int cnt, const char *set, const char *reset)
{
	if (cnt <= 0)
		return;
	fprintf(file, "%s", set);
	while (cnt--)
		putc(ch, file);
	fprintf(file, "%s", reset);
}

static void fill_print_name(struct diffstat_file *file)
{
	char *pname;

	if (file->print_name)
		return;

	if (!file->is_renamed) {
		struct strbuf buf = STRBUF_INIT;
		if (quote_c_style(file->name, &buf, NULL, 0)) {
			pname = strbuf_detach(&buf, NULL);
		} else {
			pname = file->name;
			strbuf_release(&buf);
		}
	} else {
		pname = pprint_rename(file->from_name, file->name);
	}
	file->print_name = pname;
}

static void show_stats(struct diffstat_t *data, struct diff_options *options)
{
	int i, len, add, del, adds = 0, dels = 0;
	uintmax_t max_change = 0, max_len = 0;
	int total_files = data->nr;
	int width, name_width, count;
	const char *reset, *add_c, *del_c;
	const char *line_prefix = "";
	int extra_shown = 0;
	struct strbuf *msg = NULL;

	if (data->nr == 0)
		return;

	if (options->output_prefix) {
		msg = options->output_prefix(options, options->output_prefix_data);
		line_prefix = msg->buf;
	}

	width = options->stat_width ? options->stat_width : 80;
	name_width = options->stat_name_width ? options->stat_name_width : 50;
	count = options->stat_count ? options->stat_count : data->nr;

	/* Sanity: give at least 5 columns to the graph,
	 * but leave at least 10 columns for the name.
	 */
	if (width < 25)
		width = 25;
	if (name_width < 10)
		name_width = 10;
	else if (width < name_width + 15)
		name_width = width - 15;

	/* Find the longest filename and max number of changes */
	reset = diff_get_color_opt(options, DIFF_RESET);
	add_c = diff_get_color_opt(options, DIFF_FILE_NEW);
	del_c = diff_get_color_opt(options, DIFF_FILE_OLD);

	for (i = 0; (i < count) && (i < data->nr); i++) {
		struct diffstat_file *file = data->files[i];
		uintmax_t change = file->added + file->deleted;
		if (!data->files[i]->is_renamed &&
			 (change == 0)) {
			count++; /* not shown == room for one more */
			continue;
		}
		fill_print_name(file);
		len = strlen(file->print_name);
		if (max_len < len)
			max_len = len;

		if (file->is_binary || file->is_unmerged)
			continue;
		if (max_change < change)
			max_change = change;
	}
	count = i; /* min(count, data->nr) */

	/* Compute the width of the graph part;
	 * 10 is for one blank at the beginning of the line plus
	 * " | count " between the name and the graph.
	 *
	 * From here on, name_width is the width of the name area,
	 * and width is the width of the graph area.
	 */
	name_width = (name_width < max_len) ? name_width : max_len;
	if (width < (name_width + 10) + max_change)
		width = width - (name_width + 10);
	else
		width = max_change;

	for (i = 0; i < count; i++) {
		const char *prefix = "";
		char *name = data->files[i]->print_name;
		uintmax_t added = data->files[i]->added;
		uintmax_t deleted = data->files[i]->deleted;
		int name_len;

		if (!data->files[i]->is_renamed &&
			 (added + deleted == 0)) {
			total_files--;
			continue;
		}
		/*
		 * "scale" the filename
		 */
		len = name_width;
		name_len = strlen(name);
		if (name_width < name_len) {
			char *slash;
			prefix = "...";
			len -= 3;
			name += name_len - len;
			slash = strchr(name, '/');
			if (slash)
				name = slash;
		}

		if (data->files[i]->is_binary) {
			fprintf(options->file, "%s", line_prefix);
			show_name(options->file, prefix, name, len);
			fprintf(options->file, "  Bin ");
			fprintf(options->file, "%s%"PRIuMAX"%s",
				del_c, deleted, reset);
			fprintf(options->file, " -> ");
			fprintf(options->file, "%s%"PRIuMAX"%s",
				add_c, added, reset);
			fprintf(options->file, " bytes");
			fprintf(options->file, "\n");
			continue;
		}
		else if (data->files[i]->is_unmerged) {
			fprintf(options->file, "%s", line_prefix);
			show_name(options->file, prefix, name, len);
			fprintf(options->file, "  Unmerged\n");
			continue;
		}

		/*
		 * scale the add/delete
		 */
		add = added;
		del = deleted;
		adds += add;
		dels += del;

		if (width <= max_change) {
			add = scale_linear(add, width, max_change);
			del = scale_linear(del, width, max_change);
		}
		fprintf(options->file, "%s", line_prefix);
		show_name(options->file, prefix, name, len);
		fprintf(options->file, "%5"PRIuMAX"%s", added + deleted,
				added + deleted ? " " : "");
		show_graph(options->file, '+', add, add_c, reset);
		show_graph(options->file, '-', del, del_c, reset);
		fprintf(options->file, "\n");
	}
	for (i = count; i < data->nr; i++) {
		uintmax_t added = data->files[i]->added;
		uintmax_t deleted = data->files[i]->deleted;
		if (!data->files[i]->is_renamed &&
			 (added + deleted == 0)) {
			total_files--;
			continue;
		}
		adds += added;
		dels += deleted;
		if (!extra_shown)
			fprintf(options->file, "%s ...\n", line_prefix);
		extra_shown = 1;
	}
	fprintf(options->file, "%s", line_prefix);
	fprintf(options->file,
	       " %d files changed, %d insertions(+), %d deletions(-)\n",
	       total_files, adds, dels);
}

static void show_shortstats(struct diffstat_t *data, struct diff_options *options)
{
	int i, adds = 0, dels = 0, total_files = data->nr;

	if (data->nr == 0)
		return;

	for (i = 0; i < data->nr; i++) {
		if (!data->files[i]->is_binary &&
		    !data->files[i]->is_unmerged) {
			int added = data->files[i]->added;
			int deleted= data->files[i]->deleted;
			if (!data->files[i]->is_renamed &&
			    (added + deleted == 0)) {
				total_files--;
			} else {
				adds += added;
				dels += deleted;
			}
		}
	}
	if (options->output_prefix) {
		struct strbuf *msg = NULL;
		msg = options->output_prefix(options,
				options->output_prefix_data);
		fprintf(options->file, "%s", msg->buf);
	}
	fprintf(options->file, " %d files changed, %d insertions(+), %d deletions(-)\n",
	       total_files, adds, dels);
}

static void show_numstat(struct diffstat_t *data, struct diff_options *options)
{
	int i;

	if (data->nr == 0)
		return;

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];

		if (options->output_prefix) {
			struct strbuf *msg = NULL;
			msg = options->output_prefix(options,
					options->output_prefix_data);
			fprintf(options->file, "%s", msg->buf);
		}

		if (file->is_binary)
			fprintf(options->file, "-\t-\t");
		else
			fprintf(options->file,
				"%"PRIuMAX"\t%"PRIuMAX"\t",
				file->added, file->deleted);
		if (options->line_termination) {
			fill_print_name(file);
			if (!file->is_renamed)
				write_name_quoted(file->name, options->file,
						  options->line_termination);
			else {
				fputs(file->print_name, options->file);
				putc(options->line_termination, options->file);
			}
		} else {
			if (file->is_renamed) {
				putc('\0', options->file);
				write_name_quoted(file->from_name, options->file, '\0');
			}
			write_name_quoted(file->name, options->file, '\0');
		}
	}
}

struct dirstat_file {
	const char *name;
	unsigned long changed;
};

struct dirstat_dir {
	struct dirstat_file *files;
	int alloc, nr, permille, cumulative;
};

static long gather_dirstat(struct diff_options *opt, struct dirstat_dir *dir,
		unsigned long changed, const char *base, int baselen)
{
	unsigned long this_dir = 0;
	unsigned int sources = 0;
	const char *line_prefix = "";
	struct strbuf *msg = NULL;

	if (opt->output_prefix) {
		msg = opt->output_prefix(opt, opt->output_prefix_data);
		line_prefix = msg->buf;
	}

	while (dir->nr) {
		struct dirstat_file *f = dir->files;
		int namelen = strlen(f->name);
		unsigned long this;
		char *slash;

		if (namelen < baselen)
			break;
		if (memcmp(f->name, base, baselen))
			break;
		slash = strchr(f->name + baselen, '/');
		if (slash) {
			int newbaselen = slash + 1 - f->name;
			this = gather_dirstat(opt, dir, changed, f->name, newbaselen);
			sources++;
		} else {
			this = f->changed;
			dir->files++;
			dir->nr--;
			sources += 2;
		}
		this_dir += this;
	}

	/*
	 * We don't report dirstat's for
	 *  - the top level
	 *  - or cases where everything came from a single directory
	 *    under this directory (sources == 1).
	 */
	if (baselen && sources != 1) {
		if (this_dir) {
			int permille = this_dir * 1000 / changed;
			if (permille >= dir->permille) {
				fprintf(opt->file, "%s%4d.%01d%% %.*s\n", line_prefix,
					permille / 10, permille % 10, baselen, base);
				if (!dir->cumulative)
					return 0;
			}
		}
	}
	return this_dir;
}

static int dirstat_compare(const void *_a, const void *_b)
{
	const struct dirstat_file *a = _a;
	const struct dirstat_file *b = _b;
	return strcmp(a->name, b->name);
}

static void show_dirstat(struct diff_options *options)
{
	int i;
	unsigned long changed;
	struct dirstat_dir dir;
	struct diff_queue_struct *q = &diff_queued_diff;

	dir.files = NULL;
	dir.alloc = 0;
	dir.nr = 0;
	dir.permille = options->dirstat_permille;
	dir.cumulative = DIFF_OPT_TST(options, DIRSTAT_CUMULATIVE);

	changed = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *name;
		unsigned long copied, added, damage;
		int content_changed;

		name = p->two->path ? p->two->path : p->one->path;

		if (p->one->sha1_valid && p->two->sha1_valid)
			content_changed = hashcmp(p->one->sha1, p->two->sha1);
		else
			content_changed = 1;

		if (!content_changed) {
			/*
			 * The SHA1 has not changed, so pre-/post-content is
			 * identical. We can therefore skip looking at the
			 * file contents altogether.
			 */
			damage = 0;
			goto found_damage;
		}

		if (DIFF_OPT_TST(options, DIRSTAT_BY_FILE)) {
			/*
			 * In --dirstat-by-file mode, we don't really need to
			 * look at the actual file contents at all.
			 * The fact that the SHA1 changed is enough for us to
			 * add this file to the list of results
			 * (with each file contributing equal damage).
			 */
			damage = 1;
			goto found_damage;
		}

		if (DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two)) {
			diff_populate_filespec(p->one, 0);
			diff_populate_filespec(p->two, 0);
			diffcore_count_changes(p->one, p->two, NULL, NULL, 0,
					       &copied, &added);
			diff_free_filespec_data(p->one);
			diff_free_filespec_data(p->two);
		} else if (DIFF_FILE_VALID(p->one)) {
			diff_populate_filespec(p->one, 1);
			copied = added = 0;
			diff_free_filespec_data(p->one);
		} else if (DIFF_FILE_VALID(p->two)) {
			diff_populate_filespec(p->two, 1);
			copied = 0;
			added = p->two->size;
			diff_free_filespec_data(p->two);
		} else
			continue;

		/*
		 * Original minus copied is the removed material,
		 * added is the new material.  They are both damages
		 * made to the preimage.
		 * If the resulting damage is zero, we know that
		 * diffcore_count_changes() considers the two entries to
		 * be identical, but since content_changed is true, we
		 * know that there must have been _some_ kind of change,
		 * so we force all entries to have damage > 0.
		 */
		damage = (p->one->size - copied) + added;
		if (!damage)
			damage = 1;

found_damage:
		ALLOC_GROW(dir.files, dir.nr + 1, dir.alloc);
		dir.files[dir.nr].name = name;
		dir.files[dir.nr].changed = damage;
		changed += damage;
		dir.nr++;
	}

	/* This can happen even with many files, if everything was renames */
	if (!changed)
		return;

	/* Show all directories with more than x% of the changes */
	qsort(dir.files, dir.nr, sizeof(dir.files[0]), dirstat_compare);
	gather_dirstat(options, &dir, changed, "", 0);
}

static void show_dirstat_by_line(struct diffstat_t *data, struct diff_options *options)
{
	int i;
	unsigned long changed;
	struct dirstat_dir dir;

	if (data->nr == 0)
		return;

	dir.files = NULL;
	dir.alloc = 0;
	dir.nr = 0;
	dir.permille = options->dirstat_permille;
	dir.cumulative = DIFF_OPT_TST(options, DIRSTAT_CUMULATIVE);

	changed = 0;
	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];
		unsigned long damage = file->added + file->deleted;
		if (file->is_binary)
			/*
			 * binary files counts bytes, not lines. Must find some
			 * way to normalize binary bytes vs. textual lines.
			 * The following heuristic assumes that there are 64
			 * bytes per "line".
			 * This is stupid and ugly, but very cheap...
			 */
			damage = (damage + 63) / 64;
		ALLOC_GROW(dir.files, dir.nr + 1, dir.alloc);
		dir.files[dir.nr].name = file->name;
		dir.files[dir.nr].changed = damage;
		changed += damage;
		dir.nr++;
	}

	/* This can happen even with many files, if everything was renames */
	if (!changed)
		return;

	/* Show all directories with more than x% of the changes */
	qsort(dir.files, dir.nr, sizeof(dir.files[0]), dirstat_compare);
	gather_dirstat(options, &dir, changed, "", 0);
}

static void free_diffstat_info(struct diffstat_t *diffstat)
{
	int i;
	for (i = 0; i < diffstat->nr; i++) {
		struct diffstat_file *f = diffstat->files[i];
		if (f->name != f->print_name)
			free(f->print_name);
		free(f->name);
		free(f->from_name);
		free(f);
	}
	free(diffstat->files);
}

struct checkdiff_t {
	const char *filename;
	int lineno;
	int conflict_marker_size;
	struct diff_options *o;
	unsigned ws_rule;
	unsigned status;
};

static int is_conflict_marker(const char *line, int marker_size, unsigned long len)
{
	char firstchar;
	int cnt;

	if (len < marker_size + 1)
		return 0;
	firstchar = line[0];
	switch (firstchar) {
	case '=': case '>': case '<': case '|':
		break;
	default:
		return 0;
	}
	for (cnt = 1; cnt < marker_size; cnt++)
		if (line[cnt] != firstchar)
			return 0;
	/* line[1] thru line[marker_size-1] are same as firstchar */
	if (len < marker_size + 1 || !isspace(line[marker_size]))
		return 0;
	return 1;
}

static void checkdiff_consume(void *priv, char *line, unsigned long len)
{
	struct checkdiff_t *data = priv;
	int color_diff = DIFF_OPT_TST(data->o, COLOR_DIFF);
	int marker_size = data->conflict_marker_size;
	const char *ws = diff_get_color(color_diff, DIFF_WHITESPACE);
	const char *reset = diff_get_color(color_diff, DIFF_RESET);
	const char *set = diff_get_color(color_diff, DIFF_FILE_NEW);
	char *err;
	char *line_prefix = "";
	struct strbuf *msgbuf;

	assert(data->o);
	if (data->o->output_prefix) {
		msgbuf = data->o->output_prefix(data->o,
			data->o->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	if (line[0] == '+') {
		unsigned bad;
		data->lineno++;
		if (is_conflict_marker(line + 1, marker_size, len - 1)) {
			data->status |= 1;
			fprintf(data->o->file,
				"%s%s:%d: leftover conflict marker\n",
				line_prefix, data->filename, data->lineno);
		}
		bad = ws_check(line + 1, len - 1, data->ws_rule);
		if (!bad)
			return;
		data->status |= bad;
		err = whitespace_error_string(bad);
		fprintf(data->o->file, "%s%s:%d: %s.\n",
			line_prefix, data->filename, data->lineno, err);
		free(err);
		emit_line(data->o, set, reset, line, 1);
		ws_check_emit(line + 1, len - 1, data->ws_rule,
			      data->o->file, set, reset, ws);
	} else if (line[0] == ' ') {
		data->lineno++;
	} else if (line[0] == '@') {
		char *plus = strchr(line, '+');
		if (plus)
			data->lineno = strtol(plus, NULL, 10) - 1;
		else
			die("invalid diff");
	}
}

static unsigned char *deflate_it(char *data,
				 unsigned long size,
				 unsigned long *result_size)
{
	int bound;
	unsigned char *deflated;
	z_stream stream;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	bound = deflateBound(&stream, size);
	deflated = xmalloc(bound);
	stream.next_out = deflated;
	stream.avail_out = bound;

	stream.next_in = (unsigned char *)data;
	stream.avail_in = size;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		; /* nothing */
	deflateEnd(&stream);
	*result_size = stream.total_out;
	return deflated;
}

static void emit_binary_diff_body(FILE *file, mmfile_t *one, mmfile_t *two, char *prefix)
{
	void *cp;
	void *delta;
	void *deflated;
	void *data;
	unsigned long orig_size;
	unsigned long delta_size;
	unsigned long deflate_size;
	unsigned long data_size;

	/* We could do deflated delta, or we could do just deflated two,
	 * whichever is smaller.
	 */
	delta = NULL;
	deflated = deflate_it(two->ptr, two->size, &deflate_size);
	if (one->size && two->size) {
		delta = diff_delta(one->ptr, one->size,
				   two->ptr, two->size,
				   &delta_size, deflate_size);
		if (delta) {
			void *to_free = delta;
			orig_size = delta_size;
			delta = deflate_it(delta, delta_size, &delta_size);
			free(to_free);
		}
	}

	if (delta && delta_size < deflate_size) {
		fprintf(file, "%sdelta %lu\n", prefix, orig_size);
		free(deflated);
		data = delta;
		data_size = delta_size;
	}
	else {
		fprintf(file, "%sliteral %lu\n", prefix, two->size);
		free(delta);
		data = deflated;
		data_size = deflate_size;
	}

	/* emit data encoded in base85 */
	cp = data;
	while (data_size) {
		int bytes = (52 < data_size) ? 52 : data_size;
		char line[70];
		data_size -= bytes;
		if (bytes <= 26)
			line[0] = bytes + 'A' - 1;
		else
			line[0] = bytes - 26 + 'a' - 1;
		encode_85(line + 1, cp, bytes);
		cp = (char *) cp + bytes;
		fprintf(file, "%s", prefix);
		fputs(line, file);
		fputc('\n', file);
	}
	fprintf(file, "%s\n", prefix);
	free(data);
}

static void emit_binary_diff(FILE *file, mmfile_t *one, mmfile_t *two, char *prefix)
{
	fprintf(file, "%sGIT binary patch\n", prefix);
	emit_binary_diff_body(file, one, two, prefix);
	emit_binary_diff_body(file, two, one, prefix);
}

static void diff_filespec_load_driver(struct diff_filespec *one)
{
	/* Use already-loaded driver */
	if (one->driver)
		return;

	if (S_ISREG(one->mode))
		one->driver = userdiff_find_by_path(one->path);

	/* Fallback to default settings */
	if (!one->driver)
		one->driver = userdiff_find_by_name("default");
}

int diff_filespec_is_binary(struct diff_filespec *one)
{
	if (one->is_binary == -1) {
		diff_filespec_load_driver(one);
		if (one->driver->binary != -1)
			one->is_binary = one->driver->binary;
		else {
			if (!one->data && DIFF_FILE_VALID(one))
				diff_populate_filespec(one, 0);
			if (one->data)
				one->is_binary = buffer_is_binary(one->data,
						one->size);
			if (one->is_binary == -1)
				one->is_binary = 0;
		}
	}
	return one->is_binary;
}

static const struct userdiff_funcname *diff_funcname_pattern(struct diff_filespec *one)
{
	diff_filespec_load_driver(one);
	return one->driver->funcname.pattern ? &one->driver->funcname : NULL;
}

static const char *userdiff_word_regex(struct diff_filespec *one)
{
	diff_filespec_load_driver(one);
	return one->driver->word_regex;
}

void diff_set_mnemonic_prefix(struct diff_options *options, const char *a, const char *b)
{
	if (!options->a_prefix)
		options->a_prefix = a;
	if (!options->b_prefix)
		options->b_prefix = b;
}

struct userdiff_driver *get_textconv(struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one))
		return NULL;

	diff_filespec_load_driver(one);
	return userdiff_get_textconv(one->driver);
}

static void builtin_diff(const char *name_a,
			 const char *name_b,
			 struct diff_filespec *one,
			 struct diff_filespec *two,
			 const char *xfrm_msg,
			 int must_show_header,
			 struct diff_options *o,
			 int complete_rewrite)
{
	mmfile_t mf1, mf2;
	const char *lbl[2];
	char *a_one, *b_two;
	const char *set = diff_get_color_opt(o, DIFF_METAINFO);
	const char *reset = diff_get_color_opt(o, DIFF_RESET);
	const char *a_prefix, *b_prefix;
	struct userdiff_driver *textconv_one = NULL;
	struct userdiff_driver *textconv_two = NULL;
	struct strbuf header = STRBUF_INIT;
	struct strbuf *msgbuf;
	char *line_prefix = "";

	if (o->output_prefix) {
		msgbuf = o->output_prefix(o, o->output_prefix_data);
		line_prefix = msgbuf->buf;
	}

	if (DIFF_OPT_TST(o, SUBMODULE_LOG) &&
			(!one->mode || S_ISGITLINK(one->mode)) &&
			(!two->mode || S_ISGITLINK(two->mode))) {
		const char *del = diff_get_color_opt(o, DIFF_FILE_OLD);
		const char *add = diff_get_color_opt(o, DIFF_FILE_NEW);
		show_submodule_summary(o->file, one ? one->path : two->path,
				one->sha1, two->sha1, two->dirty_submodule,
				del, add, reset);
		return;
	}

	if (DIFF_OPT_TST(o, ALLOW_TEXTCONV)) {
		textconv_one = get_textconv(one);
		textconv_two = get_textconv(two);
	}

	diff_set_mnemonic_prefix(o, "a/", "b/");
	if (DIFF_OPT_TST(o, REVERSE_DIFF)) {
		a_prefix = o->b_prefix;
		b_prefix = o->a_prefix;
	} else {
		a_prefix = o->a_prefix;
		b_prefix = o->b_prefix;
	}

	/* Never use a non-valid filename anywhere if at all possible */
	name_a = DIFF_FILE_VALID(one) ? name_a : name_b;
	name_b = DIFF_FILE_VALID(two) ? name_b : name_a;

	a_one = quote_two(a_prefix, name_a + (*name_a == '/'));
	b_two = quote_two(b_prefix, name_b + (*name_b == '/'));
	lbl[0] = DIFF_FILE_VALID(one) ? a_one : "/dev/null";
	lbl[1] = DIFF_FILE_VALID(two) ? b_two : "/dev/null";
	strbuf_addf(&header, "%s%sdiff --git %s %s%s\n", line_prefix, set, a_one, b_two, reset);
	if (lbl[0][0] == '/') {
		/* /dev/null */
		strbuf_addf(&header, "%s%snew file mode %06o%s\n", line_prefix, set, two->mode, reset);
		if (xfrm_msg)
			strbuf_addstr(&header, xfrm_msg);
		must_show_header = 1;
	}
	else if (lbl[1][0] == '/') {
		strbuf_addf(&header, "%s%sdeleted file mode %06o%s\n", line_prefix, set, one->mode, reset);
		if (xfrm_msg)
			strbuf_addstr(&header, xfrm_msg);
		must_show_header = 1;
	}
	else {
		if (one->mode != two->mode) {
			strbuf_addf(&header, "%s%sold mode %06o%s\n", line_prefix, set, one->mode, reset);
			strbuf_addf(&header, "%s%snew mode %06o%s\n", line_prefix, set, two->mode, reset);
			must_show_header = 1;
		}
		if (xfrm_msg)
			strbuf_addstr(&header, xfrm_msg);

		/*
		 * we do not run diff between different kind
		 * of objects.
		 */
		if ((one->mode ^ two->mode) & S_IFMT)
			goto free_ab_and_return;
		if (complete_rewrite &&
		    (textconv_one || !diff_filespec_is_binary(one)) &&
		    (textconv_two || !diff_filespec_is_binary(two))) {
			fprintf(o->file, "%s", header.buf);
			strbuf_reset(&header);
			emit_rewrite_diff(name_a, name_b, one, two,
						textconv_one, textconv_two, o);
			o->found_changes = 1;
			goto free_ab_and_return;
		}
	}

	if (o->irreversible_delete && lbl[1][0] == '/') {
		fprintf(o->file, "%s", header.buf);
		strbuf_reset(&header);
		goto free_ab_and_return;
	} else if (!DIFF_OPT_TST(o, TEXT) &&
	    ( (!textconv_one && diff_filespec_is_binary(one)) ||
	      (!textconv_two && diff_filespec_is_binary(two)) )) {
		if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
			die("unable to read files to diff");
		/* Quite common confusing case */
		if (mf1.size == mf2.size &&
		    !memcmp(mf1.ptr, mf2.ptr, mf1.size)) {
			if (must_show_header)
				fprintf(o->file, "%s", header.buf);
			goto free_ab_and_return;
		}
		fprintf(o->file, "%s", header.buf);
		strbuf_reset(&header);
		if (DIFF_OPT_TST(o, BINARY))
			emit_binary_diff(o->file, &mf1, &mf2, line_prefix);
		else
			fprintf(o->file, "%sBinary files %s and %s differ\n",
				line_prefix, lbl[0], lbl[1]);
		o->found_changes = 1;
	} else {
		/* Crazy xdl interfaces.. */
		const char *diffopts = getenv("GIT_DIFF_OPTS");
		xpparam_t xpp;
		xdemitconf_t xecfg;
		struct emit_callback ecbdata;
		const struct userdiff_funcname *pe;

		if (!DIFF_XDL_TST(o, WHITESPACE_FLAGS) || must_show_header) {
			fprintf(o->file, "%s", header.buf);
			strbuf_reset(&header);
		}

		mf1.size = fill_textconv(textconv_one, one, &mf1.ptr);
		mf2.size = fill_textconv(textconv_two, two, &mf2.ptr);

		pe = diff_funcname_pattern(one);
		if (!pe)
			pe = diff_funcname_pattern(two);

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		memset(&ecbdata, 0, sizeof(ecbdata));
		ecbdata.label_path = lbl;
		ecbdata.color_diff = DIFF_OPT_TST(o, COLOR_DIFF);
		ecbdata.found_changesp = &o->found_changes;
		ecbdata.ws_rule = whitespace_rule(name_b ? name_b : name_a);
		if (ecbdata.ws_rule & WS_BLANK_AT_EOF)
			check_blank_at_eof(&mf1, &mf2, &ecbdata);
		ecbdata.opt = o;
		ecbdata.header = header.len ? &header : NULL;
		xpp.flags = o->xdl_opts;
		xecfg.ctxlen = o->context;
		xecfg.interhunkctxlen = o->interhunkcontext;
		xecfg.flags = XDL_EMIT_FUNCNAMES;
		if (pe)
			xdiff_set_find_func(&xecfg, pe->pattern, pe->cflags);
		if (!diffopts)
			;
		else if (!prefixcmp(diffopts, "--unified="))
			xecfg.ctxlen = strtoul(diffopts + 10, NULL, 10);
		else if (!prefixcmp(diffopts, "-u"))
			xecfg.ctxlen = strtoul(diffopts + 2, NULL, 10);
		if (o->word_diff) {
			int i;

			ecbdata.diff_words =
				xcalloc(1, sizeof(struct diff_words_data));
			ecbdata.diff_words->type = o->word_diff;
			ecbdata.diff_words->opt = o;
			if (!o->word_regex)
				o->word_regex = userdiff_word_regex(one);
			if (!o->word_regex)
				o->word_regex = userdiff_word_regex(two);
			if (!o->word_regex)
				o->word_regex = diff_word_regex_cfg;
			if (o->word_regex) {
				ecbdata.diff_words->word_regex = (regex_t *)
					xmalloc(sizeof(regex_t));
				if (regcomp(ecbdata.diff_words->word_regex,
						o->word_regex,
						REG_EXTENDED | REG_NEWLINE))
					die ("Invalid regular expression: %s",
							o->word_regex);
			}
			for (i = 0; i < ARRAY_SIZE(diff_words_styles); i++) {
				if (o->word_diff == diff_words_styles[i].type) {
					ecbdata.diff_words->style =
						&diff_words_styles[i];
					break;
				}
			}
			if (DIFF_OPT_TST(o, COLOR_DIFF)) {
				struct diff_words_style *st = ecbdata.diff_words->style;
				st->old.color = diff_get_color_opt(o, DIFF_FILE_OLD);
				st->new.color = diff_get_color_opt(o, DIFF_FILE_NEW);
				st->ctx.color = diff_get_color_opt(o, DIFF_PLAIN);
			}
		}
		xdi_diff_outf(&mf1, &mf2, fn_out_consume, &ecbdata,
			      &xpp, &xecfg);
		if (o->word_diff)
			free_diff_words_data(&ecbdata);
		if (textconv_one)
			free(mf1.ptr);
		if (textconv_two)
			free(mf2.ptr);
		xdiff_clear_find_func(&xecfg);
	}

 free_ab_and_return:
	strbuf_release(&header);
	diff_free_filespec_data(one);
	diff_free_filespec_data(two);
	free(a_one);
	free(b_two);
	return;
}

static void builtin_diffstat(const char *name_a, const char *name_b,
			     struct diff_filespec *one,
			     struct diff_filespec *two,
			     struct diffstat_t *diffstat,
			     struct diff_options *o,
			     int complete_rewrite)
{
	mmfile_t mf1, mf2;
	struct diffstat_file *data;

	data = diffstat_add(diffstat, name_a, name_b);

	if (!one || !two) {
		data->is_unmerged = 1;
		return;
	}

	if (diff_filespec_is_binary(one) || diff_filespec_is_binary(two)) {
		data->is_binary = 1;
		data->added = diff_filespec_size(two);
		data->deleted = diff_filespec_size(one);
	}

	else if (complete_rewrite) {
		diff_populate_filespec(one, 0);
		diff_populate_filespec(two, 0);
		data->deleted = count_lines(one->data, one->size);
		data->added = count_lines(two->data, two->size);
	}

	else {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;

		if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
			die("unable to read files to diff");

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		xpp.flags = o->xdl_opts;
		xdi_diff_outf(&mf1, &mf2, diffstat_consume, diffstat,
			      &xpp, &xecfg);
	}

	diff_free_filespec_data(one);
	diff_free_filespec_data(two);
}

static void builtin_checkdiff(const char *name_a, const char *name_b,
			      const char *attr_path,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      struct diff_options *o)
{
	mmfile_t mf1, mf2;
	struct checkdiff_t data;

	if (!two)
		return;

	memset(&data, 0, sizeof(data));
	data.filename = name_b ? name_b : name_a;
	data.lineno = 0;
	data.o = o;
	data.ws_rule = whitespace_rule(attr_path);
	data.conflict_marker_size = ll_merge_marker_size(attr_path);

	if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
		die("unable to read files to diff");

	/*
	 * All the other codepaths check both sides, but not checking
	 * the "old" side here is deliberate.  We are checking the newly
	 * introduced changes, and as long as the "new" side is text, we
	 * can and should check what it introduces.
	 */
	if (diff_filespec_is_binary(two))
		goto free_and_return;
	else {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		xecfg.ctxlen = 1; /* at least one context line */
		xpp.flags = 0;
		xdi_diff_outf(&mf1, &mf2, checkdiff_consume, &data,
			      &xpp, &xecfg);

		if (data.ws_rule & WS_BLANK_AT_EOF) {
			struct emit_callback ecbdata;
			int blank_at_eof;

			ecbdata.ws_rule = data.ws_rule;
			check_blank_at_eof(&mf1, &mf2, &ecbdata);
			blank_at_eof = ecbdata.blank_at_eof_in_postimage;

			if (blank_at_eof) {
				static char *err;
				if (!err)
					err = whitespace_error_string(WS_BLANK_AT_EOF);
				fprintf(o->file, "%s:%d: %s.\n",
					data.filename, blank_at_eof, err);
				data.status = 1; /* report errors */
			}
		}
	}
 free_and_return:
	diff_free_filespec_data(one);
	diff_free_filespec_data(two);
	if (data.status)
		DIFF_OPT_SET(o, CHECK_FAILED);
}

struct diff_filespec *alloc_filespec(const char *path)
{
	int namelen = strlen(path);
	struct diff_filespec *spec = xmalloc(sizeof(*spec) + namelen + 1);

	memset(spec, 0, sizeof(*spec));
	spec->path = (char *)(spec + 1);
	memcpy(spec->path, path, namelen+1);
	spec->count = 1;
	spec->is_binary = -1;
	return spec;
}

void free_filespec(struct diff_filespec *spec)
{
	if (!--spec->count) {
		diff_free_filespec_data(spec);
		free(spec);
	}
}

void fill_filespec(struct diff_filespec *spec, const unsigned char *sha1,
		   unsigned short mode)
{
	if (mode) {
		spec->mode = canon_mode(mode);
		hashcpy(spec->sha1, sha1);
		spec->sha1_valid = !is_null_sha1(sha1);
	}
}

/*
 * Given a name and sha1 pair, if the index tells us the file in
 * the work tree has that object contents, return true, so that
 * prepare_temp_file() does not have to inflate and extract.
 */
static int reuse_worktree_file(const char *name, const unsigned char *sha1, int want_file)
{
	struct cache_entry *ce;
	struct stat st;
	int pos, len;

	/*
	 * We do not read the cache ourselves here, because the
	 * benchmark with my previous version that always reads cache
	 * shows that it makes things worse for diff-tree comparing
	 * two linux-2.6 kernel trees in an already checked out work
	 * tree.  This is because most diff-tree comparisons deal with
	 * only a small number of files, while reading the cache is
	 * expensive for a large project, and its cost outweighs the
	 * savings we get by not inflating the object to a temporary
	 * file.  Practically, this code only helps when we are used
	 * by diff-cache --cached, which does read the cache before
	 * calling us.
	 */
	if (!active_cache)
		return 0;

	/* We want to avoid the working directory if our caller
	 * doesn't need the data in a normal file, this system
	 * is rather slow with its stat/open/mmap/close syscalls,
	 * and the object is contained in a pack file.  The pack
	 * is probably already open and will be faster to obtain
	 * the data through than the working directory.  Loose
	 * objects however would tend to be slower as they need
	 * to be individually opened and inflated.
	 */
	if (!FAST_WORKING_DIRECTORY && !want_file && has_sha1_pack(sha1))
		return 0;

	len = strlen(name);
	pos = cache_name_pos(name, len);
	if (pos < 0)
		return 0;
	ce = active_cache[pos];

	/*
	 * This is not the sha1 we are looking for, or
	 * unreusable because it is not a regular file.
	 */
	if (hashcmp(sha1, ce->sha1) || !S_ISREG(ce->ce_mode))
		return 0;

	/*
	 * If ce is marked as "assume unchanged", there is no
	 * guarantee that work tree matches what we are looking for.
	 */
	if ((ce->ce_flags & CE_VALID) || ce_skip_worktree(ce))
		return 0;

	/*
	 * If ce matches the file in the work tree, we can reuse it.
	 */
	if (ce_uptodate(ce) ||
	    (!lstat(name, &st) && !ce_match_stat(ce, &st, 0)))
		return 1;

	return 0;
}

static int populate_from_stdin(struct diff_filespec *s)
{
	struct strbuf buf = STRBUF_INIT;
	size_t size = 0;

	if (strbuf_read(&buf, 0, 0) < 0)
		return error("error while reading from stdin %s",
				     strerror(errno));

	s->should_munmap = 0;
	s->data = strbuf_detach(&buf, &size);
	s->size = size;
	s->should_free = 1;
	return 0;
}

static int diff_populate_gitlink(struct diff_filespec *s, int size_only)
{
	int len;
	char *data = xmalloc(100), *dirty = "";

	/* Are we looking at the work tree? */
	if (s->dirty_submodule)
		dirty = "-dirty";

	len = snprintf(data, 100,
		       "Subproject commit %s%s\n", sha1_to_hex(s->sha1), dirty);
	s->data = data;
	s->size = len;
	s->should_free = 1;
	if (size_only) {
		s->data = NULL;
		free(data);
	}
	return 0;
}

/*
 * While doing rename detection and pickaxe operation, we may need to
 * grab the data for the blob (or file) for our own in-core comparison.
 * diff_filespec has data and size fields for this purpose.
 */
int diff_populate_filespec(struct diff_filespec *s, int size_only)
{
	int err = 0;
	if (!DIFF_FILE_VALID(s))
		die("internal error: asking to populate invalid file.");
	if (S_ISDIR(s->mode))
		return -1;

	if (s->data)
		return 0;

	if (size_only && 0 < s->size)
		return 0;

	if (S_ISGITLINK(s->mode))
		return diff_populate_gitlink(s, size_only);

	if (!s->sha1_valid ||
	    reuse_worktree_file(s->path, s->sha1, 0)) {
		struct strbuf buf = STRBUF_INIT;
		struct stat st;
		int fd;

		if (!strcmp(s->path, "-"))
			return populate_from_stdin(s);

		if (lstat(s->path, &st) < 0) {
			if (errno == ENOENT) {
			err_empty:
				err = -1;
			empty:
				s->data = (char *)"";
				s->size = 0;
				return err;
			}
		}
		s->size = xsize_t(st.st_size);
		if (!s->size)
			goto empty;
		if (S_ISLNK(st.st_mode)) {
			struct strbuf sb = STRBUF_INIT;

			if (strbuf_readlink(&sb, s->path, s->size))
				goto err_empty;
			s->size = sb.len;
			s->data = strbuf_detach(&sb, NULL);
			s->should_free = 1;
			return 0;
		}
		if (size_only)
			return 0;
		fd = open(s->path, O_RDONLY);
		if (fd < 0)
			goto err_empty;
		s->data = xmmap(NULL, s->size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		s->should_munmap = 1;

		/*
		 * Convert from working tree format to canonical git format
		 */
		if (convert_to_git(s->path, s->data, s->size, &buf, safe_crlf)) {
			size_t size = 0;
			munmap(s->data, s->size);
			s->should_munmap = 0;
			s->data = strbuf_detach(&buf, &size);
			s->size = size;
			s->should_free = 1;
		}
	}
	else {
		enum object_type type;
		if (size_only) {
			type = sha1_object_info(s->sha1, &s->size);
			if (type < 0)
				die("unable to read %s", sha1_to_hex(s->sha1));
		} else {
			s->data = read_sha1_file(s->sha1, &type, &s->size);
			if (!s->data)
				die("unable to read %s", sha1_to_hex(s->sha1));
			s->should_free = 1;
		}
	}
	return 0;
}

void diff_free_filespec_blob(struct diff_filespec *s)
{
	if (s->should_free)
		free(s->data);
	else if (s->should_munmap)
		munmap(s->data, s->size);

	if (s->should_free || s->should_munmap) {
		s->should_free = s->should_munmap = 0;
		s->data = NULL;
	}
}

void diff_free_filespec_data(struct diff_filespec *s)
{
	diff_free_filespec_blob(s);
	free(s->cnt_data);
	s->cnt_data = NULL;
}

static void prep_temp_blob(const char *path, struct diff_tempfile *temp,
			   void *blob,
			   unsigned long size,
			   const unsigned char *sha1,
			   int mode)
{
	int fd;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf template = STRBUF_INIT;
	char *path_dup = xstrdup(path);
	const char *base = basename(path_dup);

	/* Generate "XXXXXX_basename.ext" */
	strbuf_addstr(&template, "XXXXXX_");
	strbuf_addstr(&template, base);

	fd = git_mkstemps(temp->tmp_path, PATH_MAX, template.buf,
			strlen(base) + 1);
	if (fd < 0)
		die_errno("unable to create temp-file");
	if (convert_to_working_tree(path,
			(const char *)blob, (size_t)size, &buf)) {
		blob = buf.buf;
		size = buf.len;
	}
	if (write_in_full(fd, blob, size) != size)
		die_errno("unable to write temp-file");
	close(fd);
	temp->name = temp->tmp_path;
	strcpy(temp->hex, sha1_to_hex(sha1));
	temp->hex[40] = 0;
	sprintf(temp->mode, "%06o", mode);
	strbuf_release(&buf);
	strbuf_release(&template);
	free(path_dup);
}

static struct diff_tempfile *prepare_temp_file(const char *name,
		struct diff_filespec *one)
{
	struct diff_tempfile *temp = claim_diff_tempfile();

	if (!DIFF_FILE_VALID(one)) {
	not_a_valid_file:
		/* A '-' entry produces this for file-2, and
		 * a '+' entry produces this for file-1.
		 */
		temp->name = "/dev/null";
		strcpy(temp->hex, ".");
		strcpy(temp->mode, ".");
		return temp;
	}

	if (!remove_tempfile_installed) {
		atexit(remove_tempfile);
		sigchain_push_common(remove_tempfile_on_signal);
		remove_tempfile_installed = 1;
	}

	if (!one->sha1_valid ||
	    reuse_worktree_file(name, one->sha1, 1)) {
		struct stat st;
		if (lstat(name, &st) < 0) {
			if (errno == ENOENT)
				goto not_a_valid_file;
			die_errno("stat(%s)", name);
		}
		if (S_ISLNK(st.st_mode)) {
			struct strbuf sb = STRBUF_INIT;
			if (strbuf_readlink(&sb, name, st.st_size) < 0)
				die_errno("readlink(%s)", name);
			prep_temp_blob(name, temp, sb.buf, sb.len,
				       (one->sha1_valid ?
					one->sha1 : null_sha1),
				       (one->sha1_valid ?
					one->mode : S_IFLNK));
			strbuf_release(&sb);
		}
		else {
			/* we can borrow from the file in the work tree */
			temp->name = name;
			if (!one->sha1_valid)
				strcpy(temp->hex, sha1_to_hex(null_sha1));
			else
				strcpy(temp->hex, sha1_to_hex(one->sha1));
			/* Even though we may sometimes borrow the
			 * contents from the work tree, we always want
			 * one->mode.  mode is trustworthy even when
			 * !(one->sha1_valid), as long as
			 * DIFF_FILE_VALID(one).
			 */
			sprintf(temp->mode, "%06o", one->mode);
		}
		return temp;
	}
	else {
		if (diff_populate_filespec(one, 0))
			die("cannot read data blob for %s", one->path);
		prep_temp_blob(name, temp, one->data, one->size,
			       one->sha1, one->mode);
	}
	return temp;
}

/* An external diff command takes:
 *
 * diff-cmd name infile1 infile1-sha1 infile1-mode \
 *               infile2 infile2-sha1 infile2-mode [ rename-to ]
 *
 */
static void run_external_diff(const char *pgm,
			      const char *name,
			      const char *other,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      const char *xfrm_msg,
			      int complete_rewrite)
{
	const char *spawn_arg[10];
	int retval;
	const char **arg = &spawn_arg[0];

	if (one && two) {
		struct diff_tempfile *temp_one, *temp_two;
		const char *othername = (other ? other : name);
		temp_one = prepare_temp_file(name, one);
		temp_two = prepare_temp_file(othername, two);
		*arg++ = pgm;
		*arg++ = name;
		*arg++ = temp_one->name;
		*arg++ = temp_one->hex;
		*arg++ = temp_one->mode;
		*arg++ = temp_two->name;
		*arg++ = temp_two->hex;
		*arg++ = temp_two->mode;
		if (other) {
			*arg++ = other;
			*arg++ = xfrm_msg;
		}
	} else {
		*arg++ = pgm;
		*arg++ = name;
	}
	*arg = NULL;
	fflush(NULL);
	retval = run_command_v_opt(spawn_arg, RUN_USING_SHELL);
	remove_tempfile();
	if (retval) {
		fprintf(stderr, "external diff died, stopping at %s.\n", name);
		exit(1);
	}
}

static int similarity_index(struct diff_filepair *p)
{
	return p->score * 100 / MAX_SCORE;
}

static void fill_metainfo(struct strbuf *msg,
			  const char *name,
			  const char *other,
			  struct diff_filespec *one,
			  struct diff_filespec *two,
			  struct diff_options *o,
			  struct diff_filepair *p,
			  int *must_show_header,
			  int use_color)
{
	const char *set = diff_get_color(use_color, DIFF_METAINFO);
	const char *reset = diff_get_color(use_color, DIFF_RESET);
	struct strbuf *msgbuf;
	char *line_prefix = "";

	*must_show_header = 1;
	if (o->output_prefix) {
		msgbuf = o->output_prefix(o, o->output_prefix_data);
		line_prefix = msgbuf->buf;
	}
	strbuf_init(msg, PATH_MAX * 2 + 300);
	switch (p->status) {
	case DIFF_STATUS_COPIED:
		strbuf_addf(msg, "%s%ssimilarity index %d%%",
			    line_prefix, set, similarity_index(p));
		strbuf_addf(msg, "%s\n%s%scopy from ",
			    reset,  line_prefix, set);
		quote_c_style(name, msg, NULL, 0);
		strbuf_addf(msg, "%s\n%s%scopy to ", reset, line_prefix, set);
		quote_c_style(other, msg, NULL, 0);
		strbuf_addf(msg, "%s\n", reset);
		break;
	case DIFF_STATUS_RENAMED:
		strbuf_addf(msg, "%s%ssimilarity index %d%%",
			    line_prefix, set, similarity_index(p));
		strbuf_addf(msg, "%s\n%s%srename from ",
			    reset, line_prefix, set);
		quote_c_style(name, msg, NULL, 0);
		strbuf_addf(msg, "%s\n%s%srename to ",
			    reset, line_prefix, set);
		quote_c_style(other, msg, NULL, 0);
		strbuf_addf(msg, "%s\n", reset);
		break;
	case DIFF_STATUS_MODIFIED:
		if (p->score) {
			strbuf_addf(msg, "%s%sdissimilarity index %d%%%s\n",
				    line_prefix,
				    set, similarity_index(p), reset);
			break;
		}
		/* fallthru */
	default:
		*must_show_header = 0;
	}
	if (one && two && hashcmp(one->sha1, two->sha1)) {
		int abbrev = DIFF_OPT_TST(o, FULL_INDEX) ? 40 : DEFAULT_ABBREV;

		if (DIFF_OPT_TST(o, BINARY)) {
			mmfile_t mf;
			if ((!fill_mmfile(&mf, one) && diff_filespec_is_binary(one)) ||
			    (!fill_mmfile(&mf, two) && diff_filespec_is_binary(two)))
				abbrev = 40;
		}
		strbuf_addf(msg, "%s%sindex %s..", line_prefix, set,
			    find_unique_abbrev(one->sha1, abbrev));
		strbuf_addstr(msg, find_unique_abbrev(two->sha1, abbrev));
		if (one->mode == two->mode)
			strbuf_addf(msg, " %06o", one->mode);
		strbuf_addf(msg, "%s\n", reset);
	}
}

static void run_diff_cmd(const char *pgm,
			 const char *name,
			 const char *other,
			 const char *attr_path,
			 struct diff_filespec *one,
			 struct diff_filespec *two,
			 struct strbuf *msg,
			 struct diff_options *o,
			 struct diff_filepair *p)
{
	const char *xfrm_msg = NULL;
	int complete_rewrite = (p->status == DIFF_STATUS_MODIFIED) && p->score;
	int must_show_header = 0;

	if (!DIFF_OPT_TST(o, ALLOW_EXTERNAL))
		pgm = NULL;
	else {
		struct userdiff_driver *drv = userdiff_find_by_path(attr_path);
		if (drv && drv->external)
			pgm = drv->external;
	}

	if (msg) {
		/*
		 * don't use colors when the header is intended for an
		 * external diff driver
		 */
		fill_metainfo(msg, name, other, one, two, o, p,
			      &must_show_header,
			      DIFF_OPT_TST(o, COLOR_DIFF) && !pgm);
		xfrm_msg = msg->len ? msg->buf : NULL;
	}

	if (pgm) {
		run_external_diff(pgm, name, other, one, two, xfrm_msg,
				  complete_rewrite);
		return;
	}
	if (one && two)
		builtin_diff(name, other ? other : name,
			     one, two, xfrm_msg, must_show_header,
			     o, complete_rewrite);
	else
		fprintf(o->file, "* Unmerged path %s\n", name);
}

static void diff_fill_sha1_info(struct diff_filespec *one)
{
	if (DIFF_FILE_VALID(one)) {
		if (!one->sha1_valid) {
			struct stat st;
			if (!strcmp(one->path, "-")) {
				hashcpy(one->sha1, null_sha1);
				return;
			}
			if (lstat(one->path, &st) < 0)
				die_errno("stat '%s'", one->path);
			if (index_path(one->sha1, one->path, &st, 0))
				die("cannot hash %s", one->path);
		}
	}
	else
		hashclr(one->sha1);
}

static void strip_prefix(int prefix_length, const char **namep, const char **otherp)
{
	/* Strip the prefix but do not molest /dev/null and absolute paths */
	if (*namep && **namep != '/') {
		*namep += prefix_length;
		if (**namep == '/')
			++*namep;
	}
	if (*otherp && **otherp != '/') {
		*otherp += prefix_length;
		if (**otherp == '/')
			++*otherp;
	}
}

static void run_diff(struct diff_filepair *p, struct diff_options *o)
{
	const char *pgm = external_diff();
	struct strbuf msg;
	struct diff_filespec *one = p->one;
	struct diff_filespec *two = p->two;
	const char *name;
	const char *other;
	const char *attr_path;

	name  = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);
	attr_path = name;
	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	if (DIFF_PAIR_UNMERGED(p)) {
		run_diff_cmd(pgm, name, NULL, attr_path,
			     NULL, NULL, NULL, o, p);
		return;
	}

	diff_fill_sha1_info(one);
	diff_fill_sha1_info(two);

	if (!pgm &&
	    DIFF_FILE_VALID(one) && DIFF_FILE_VALID(two) &&
	    (S_IFMT & one->mode) != (S_IFMT & two->mode)) {
		/*
		 * a filepair that changes between file and symlink
		 * needs to be split into deletion and creation.
		 */
		struct diff_filespec *null = alloc_filespec(two->path);
		run_diff_cmd(NULL, name, other, attr_path,
			     one, null, &msg, o, p);
		free(null);
		strbuf_release(&msg);

		null = alloc_filespec(one->path);
		run_diff_cmd(NULL, name, other, attr_path,
			     null, two, &msg, o, p);
		free(null);
	}
	else
		run_diff_cmd(pgm, name, other, attr_path,
			     one, two, &msg, o, p);

	strbuf_release(&msg);
}

static void run_diffstat(struct diff_filepair *p, struct diff_options *o,
			 struct diffstat_t *diffstat)
{
	const char *name;
	const char *other;
	int complete_rewrite = 0;

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		builtin_diffstat(p->one->path, NULL, NULL, NULL, diffstat, o, 0);
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);

	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	diff_fill_sha1_info(p->one);
	diff_fill_sha1_info(p->two);

	if (p->status == DIFF_STATUS_MODIFIED && p->score)
		complete_rewrite = 1;
	builtin_diffstat(name, other, p->one, p->two, diffstat, o, complete_rewrite);
}

static void run_checkdiff(struct diff_filepair *p, struct diff_options *o)
{
	const char *name;
	const char *other;
	const char *attr_path;

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);
	attr_path = other ? other : name;

	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	diff_fill_sha1_info(p->one);
	diff_fill_sha1_info(p->two);

	builtin_checkdiff(name, other, attr_path, p->one, p->two, o);
}

void diff_setup(struct diff_options *options)
{
	memcpy(options, &default_diff_options, sizeof(*options));

	options->file = stdout;

	options->line_termination = '\n';
	options->break_opt = -1;
	options->rename_limit = -1;
	options->dirstat_permille = diff_dirstat_permille_default;
	options->context = 3;

	options->change = diff_change;
	options->add_remove = diff_addremove;
	if (diff_use_color_default > 0)
		DIFF_OPT_SET(options, COLOR_DIFF);
	options->detect_rename = diff_detect_rename_default;

	if (diff_no_prefix) {
		options->a_prefix = options->b_prefix = "";
	} else if (!diff_mnemonic_prefix) {
		options->a_prefix = "a/";
		options->b_prefix = "b/";
	}
}

int diff_setup_done(struct diff_options *options)
{
	int count = 0;

	if (options->output_format & DIFF_FORMAT_NAME)
		count++;
	if (options->output_format & DIFF_FORMAT_NAME_STATUS)
		count++;
	if (options->output_format & DIFF_FORMAT_CHECKDIFF)
		count++;
	if (options->output_format & DIFF_FORMAT_NO_OUTPUT)
		count++;
	if (count > 1)
		die("--name-only, --name-status, --check and -s are mutually exclusive");

	/*
	 * Most of the time we can say "there are changes"
	 * only by checking if there are changed paths, but
	 * --ignore-whitespace* options force us to look
	 * inside contents.
	 */

	if (DIFF_XDL_TST(options, IGNORE_WHITESPACE) ||
	    DIFF_XDL_TST(options, IGNORE_WHITESPACE_CHANGE) ||
	    DIFF_XDL_TST(options, IGNORE_WHITESPACE_AT_EOL))
		DIFF_OPT_SET(options, DIFF_FROM_CONTENTS);
	else
		DIFF_OPT_CLR(options, DIFF_FROM_CONTENTS);

	if (DIFF_OPT_TST(options, FIND_COPIES_HARDER))
		options->detect_rename = DIFF_DETECT_COPY;

	if (!DIFF_OPT_TST(options, RELATIVE_NAME))
		options->prefix = NULL;
	if (options->prefix)
		options->prefix_length = strlen(options->prefix);
	else
		options->prefix_length = 0;

	if (options->output_format & (DIFF_FORMAT_NAME |
				      DIFF_FORMAT_NAME_STATUS |
				      DIFF_FORMAT_CHECKDIFF |
				      DIFF_FORMAT_NO_OUTPUT))
		options->output_format &= ~(DIFF_FORMAT_RAW |
					    DIFF_FORMAT_NUMSTAT |
					    DIFF_FORMAT_DIFFSTAT |
					    DIFF_FORMAT_SHORTSTAT |
					    DIFF_FORMAT_DIRSTAT |
					    DIFF_FORMAT_SUMMARY |
					    DIFF_FORMAT_PATCH);

	/*
	 * These cases always need recursive; we do not drop caller-supplied
	 * recursive bits for other formats here.
	 */
	if (options->output_format & (DIFF_FORMAT_PATCH |
				      DIFF_FORMAT_NUMSTAT |
				      DIFF_FORMAT_DIFFSTAT |
				      DIFF_FORMAT_SHORTSTAT |
				      DIFF_FORMAT_DIRSTAT |
				      DIFF_FORMAT_SUMMARY |
				      DIFF_FORMAT_CHECKDIFF))
		DIFF_OPT_SET(options, RECURSIVE);
	/*
	 * Also pickaxe would not work very well if you do not say recursive
	 */
	if (options->pickaxe)
		DIFF_OPT_SET(options, RECURSIVE);
	/*
	 * When patches are generated, submodules diffed against the work tree
	 * must be checked for dirtiness too so it can be shown in the output
	 */
	if (options->output_format & DIFF_FORMAT_PATCH)
		DIFF_OPT_SET(options, DIRTY_SUBMODULES);

	if (options->detect_rename && options->rename_limit < 0)
		options->rename_limit = diff_rename_limit_default;
	if (options->setup & DIFF_SETUP_USE_CACHE) {
		if (!active_cache)
			/* read-cache does not die even when it fails
			 * so it is safe for us to do this here.  Also
			 * it does not smudge active_cache or active_nr
			 * when it fails, so we do not have to worry about
			 * cleaning it up ourselves either.
			 */
			read_cache();
	}
	if (options->abbrev <= 0 || 40 < options->abbrev)
		options->abbrev = 40; /* full */

	/*
	 * It does not make sense to show the first hit we happened
	 * to have found.  It does not make sense not to return with
	 * exit code in such a case either.
	 */
	if (DIFF_OPT_TST(options, QUICK)) {
		options->output_format = DIFF_FORMAT_NO_OUTPUT;
		DIFF_OPT_SET(options, EXIT_WITH_STATUS);
	}

	return 0;
}

static int opt_arg(const char *arg, int arg_short, const char *arg_long, int *val)
{
	char c, *eq;
	int len;

	if (*arg != '-')
		return 0;
	c = *++arg;
	if (!c)
		return 0;
	if (c == arg_short) {
		c = *++arg;
		if (!c)
			return 1;
		if (val && isdigit(c)) {
			char *end;
			int n = strtoul(arg, &end, 10);
			if (*end)
				return 0;
			*val = n;
			return 1;
		}
		return 0;
	}
	if (c != '-')
		return 0;
	arg++;
	eq = strchr(arg, '=');
	if (eq)
		len = eq - arg;
	else
		len = strlen(arg);
	if (!len || strncmp(arg, arg_long, len))
		return 0;
	if (eq) {
		int n;
		char *end;
		if (!isdigit(*++eq))
			return 0;
		n = strtoul(eq, &end, 10);
		if (*end)
			return 0;
		*val = n;
	}
	return 1;
}

static int diff_scoreopt_parse(const char *opt);

static inline int short_opt(char opt, const char **argv,
			    const char **optarg)
{
	const char *arg = argv[0];
	if (arg[0] != '-' || arg[1] != opt)
		return 0;
	if (arg[2] != '\0') {
		*optarg = arg + 2;
		return 1;
	}
	if (!argv[1])
		die("Option '%c' requires a value", opt);
	*optarg = argv[1];
	return 2;
}

int parse_long_opt(const char *opt, const char **argv,
		   const char **optarg)
{
	const char *arg = argv[0];
	if (arg[0] != '-' || arg[1] != '-')
		return 0;
	arg += strlen("--");
	if (prefixcmp(arg, opt))
		return 0;
	arg += strlen(opt);
	if (*arg == '=') { /* sticked form: --option=value */
		*optarg = arg + 1;
		return 1;
	}
	if (*arg != '\0')
		return 0;
	/* separate form: --option value */
	if (!argv[1])
		die("Option '--%s' requires a value", opt);
	*optarg = argv[1];
	return 2;
}

static int stat_opt(struct diff_options *options, const char **av)
{
	const char *arg = av[0];
	char *end;
	int width = options->stat_width;
	int name_width = options->stat_name_width;
	int count = options->stat_count;
	int argcount = 1;

	arg += strlen("--stat");
	end = (char *)arg;

	switch (*arg) {
	case '-':
		if (!prefixcmp(arg, "-width")) {
			arg += strlen("-width");
			if (*arg == '=')
				width = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die("Option '--stat-width' requires a value");
			else if (!*arg) {
				width = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		} else if (!prefixcmp(arg, "-name-width")) {
			arg += strlen("-name-width");
			if (*arg == '=')
				name_width = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die("Option '--stat-name-width' requires a value");
			else if (!*arg) {
				name_width = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		} else if (!prefixcmp(arg, "-count")) {
			arg += strlen("-count");
			if (*arg == '=')
				count = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die("Option '--stat-count' requires a value");
			else if (!*arg) {
				count = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		}
		break;
	case '=':
		width = strtoul(arg+1, &end, 10);
		if (*end == ',')
			name_width = strtoul(end+1, &end, 10);
		if (*end == ',')
			count = strtoul(end+1, &end, 10);
	}

	/* Important! This checks all the error cases! */
	if (*end)
		return 0;
	options->output_format |= DIFF_FORMAT_DIFFSTAT;
	options->stat_name_width = name_width;
	options->stat_width = width;
	options->stat_count = count;
	return argcount;
}

static int parse_dirstat_opt(struct diff_options *options, const char *params)
{
	struct strbuf errmsg = STRBUF_INIT;
	if (parse_dirstat_params(options, params, &errmsg))
		die(_("Failed to parse --dirstat/-X option parameter:\n%s"),
		    errmsg.buf);
	strbuf_release(&errmsg);
	/*
	 * The caller knows a dirstat-related option is given from the command
	 * line; allow it to say "return this_function();"
	 */
	options->output_format |= DIFF_FORMAT_DIRSTAT;
	return 1;
}

int diff_opt_parse(struct diff_options *options, const char **av, int ac)
{
	const char *arg = av[0];
	const char *optarg;
	int argcount;

	/* Output format options */
	if (!strcmp(arg, "-p") || !strcmp(arg, "-u") || !strcmp(arg, "--patch"))
		options->output_format |= DIFF_FORMAT_PATCH;
	else if (opt_arg(arg, 'U', "unified", &options->context))
		options->output_format |= DIFF_FORMAT_PATCH;
	else if (!strcmp(arg, "--raw"))
		options->output_format |= DIFF_FORMAT_RAW;
	else if (!strcmp(arg, "--patch-with-raw"))
		options->output_format |= DIFF_FORMAT_PATCH | DIFF_FORMAT_RAW;
	else if (!strcmp(arg, "--numstat"))
		options->output_format |= DIFF_FORMAT_NUMSTAT;
	else if (!strcmp(arg, "--shortstat"))
		options->output_format |= DIFF_FORMAT_SHORTSTAT;
	else if (!strcmp(arg, "-X") || !strcmp(arg, "--dirstat"))
		return parse_dirstat_opt(options, "");
	else if (!prefixcmp(arg, "-X"))
		return parse_dirstat_opt(options, arg + 2);
	else if (!prefixcmp(arg, "--dirstat="))
		return parse_dirstat_opt(options, arg + 10);
	else if (!strcmp(arg, "--cumulative"))
		return parse_dirstat_opt(options, "cumulative");
	else if (!strcmp(arg, "--dirstat-by-file"))
		return parse_dirstat_opt(options, "files");
	else if (!prefixcmp(arg, "--dirstat-by-file=")) {
		parse_dirstat_opt(options, "files");
		return parse_dirstat_opt(options, arg + 18);
	}
	else if (!strcmp(arg, "--check"))
		options->output_format |= DIFF_FORMAT_CHECKDIFF;
	else if (!strcmp(arg, "--summary"))
		options->output_format |= DIFF_FORMAT_SUMMARY;
	else if (!strcmp(arg, "--patch-with-stat"))
		options->output_format |= DIFF_FORMAT_PATCH | DIFF_FORMAT_DIFFSTAT;
	else if (!strcmp(arg, "--name-only"))
		options->output_format |= DIFF_FORMAT_NAME;
	else if (!strcmp(arg, "--name-status"))
		options->output_format |= DIFF_FORMAT_NAME_STATUS;
	else if (!strcmp(arg, "-s"))
		options->output_format |= DIFF_FORMAT_NO_OUTPUT;
	else if (!prefixcmp(arg, "--stat"))
		/* --stat, --stat-width, --stat-name-width, or --stat-count */
		return stat_opt(options, av);

	/* renames options */
	else if (!prefixcmp(arg, "-B") || !prefixcmp(arg, "--break-rewrites=") ||
		 !strcmp(arg, "--break-rewrites")) {
		if ((options->break_opt = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -B: %s", arg+2);
	}
	else if (!prefixcmp(arg, "-M") || !prefixcmp(arg, "--find-renames=") ||
		 !strcmp(arg, "--find-renames")) {
		if ((options->rename_score = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -M: %s", arg+2);
		options->detect_rename = DIFF_DETECT_RENAME;
	}
	else if (!strcmp(arg, "-D") || !strcmp(arg, "--irreversible-delete")) {
		options->irreversible_delete = 1;
	}
	else if (!prefixcmp(arg, "-C") || !prefixcmp(arg, "--find-copies=") ||
		 !strcmp(arg, "--find-copies")) {
		if (options->detect_rename == DIFF_DETECT_COPY)
			DIFF_OPT_SET(options, FIND_COPIES_HARDER);
		if ((options->rename_score = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -C: %s", arg+2);
		options->detect_rename = DIFF_DETECT_COPY;
	}
	else if (!strcmp(arg, "--no-renames"))
		options->detect_rename = 0;
	else if (!strcmp(arg, "--relative"))
		DIFF_OPT_SET(options, RELATIVE_NAME);
	else if (!prefixcmp(arg, "--relative=")) {
		DIFF_OPT_SET(options, RELATIVE_NAME);
		options->prefix = arg + 11;
	}

	/* xdiff options */
	else if (!strcmp(arg, "-w") || !strcmp(arg, "--ignore-all-space"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE);
	else if (!strcmp(arg, "-b") || !strcmp(arg, "--ignore-space-change"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE_CHANGE);
	else if (!strcmp(arg, "--ignore-space-at-eol"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE_AT_EOL);
	else if (!strcmp(arg, "--patience"))
		DIFF_XDL_SET(options, PATIENCE_DIFF);

	/* flags options */
	else if (!strcmp(arg, "--binary")) {
		options->output_format |= DIFF_FORMAT_PATCH;
		DIFF_OPT_SET(options, BINARY);
	}
	else if (!strcmp(arg, "--full-index"))
		DIFF_OPT_SET(options, FULL_INDEX);
	else if (!strcmp(arg, "-a") || !strcmp(arg, "--text"))
		DIFF_OPT_SET(options, TEXT);
	else if (!strcmp(arg, "-R"))
		DIFF_OPT_SET(options, REVERSE_DIFF);
	else if (!strcmp(arg, "--find-copies-harder"))
		DIFF_OPT_SET(options, FIND_COPIES_HARDER);
	else if (!strcmp(arg, "--follow"))
		DIFF_OPT_SET(options, FOLLOW_RENAMES);
	else if (!strcmp(arg, "--color"))
		DIFF_OPT_SET(options, COLOR_DIFF);
	else if (!prefixcmp(arg, "--color=")) {
		int value = git_config_colorbool(NULL, arg+8, -1);
		if (value == 0)
			DIFF_OPT_CLR(options, COLOR_DIFF);
		else if (value > 0)
			DIFF_OPT_SET(options, COLOR_DIFF);
		else
			return error("option `color' expects \"always\", \"auto\", or \"never\"");
	}
	else if (!strcmp(arg, "--no-color"))
		DIFF_OPT_CLR(options, COLOR_DIFF);
	else if (!strcmp(arg, "--color-words")) {
		DIFF_OPT_SET(options, COLOR_DIFF);
		options->word_diff = DIFF_WORDS_COLOR;
	}
	else if (!prefixcmp(arg, "--color-words=")) {
		DIFF_OPT_SET(options, COLOR_DIFF);
		options->word_diff = DIFF_WORDS_COLOR;
		options->word_regex = arg + 14;
	}
	else if (!strcmp(arg, "--word-diff")) {
		if (options->word_diff == DIFF_WORDS_NONE)
			options->word_diff = DIFF_WORDS_PLAIN;
	}
	else if (!prefixcmp(arg, "--word-diff=")) {
		const char *type = arg + 12;
		if (!strcmp(type, "plain"))
			options->word_diff = DIFF_WORDS_PLAIN;
		else if (!strcmp(type, "color")) {
			DIFF_OPT_SET(options, COLOR_DIFF);
			options->word_diff = DIFF_WORDS_COLOR;
		}
		else if (!strcmp(type, "porcelain"))
			options->word_diff = DIFF_WORDS_PORCELAIN;
		else if (!strcmp(type, "none"))
			options->word_diff = DIFF_WORDS_NONE;
		else
			die("bad --word-diff argument: %s", type);
	}
	else if ((argcount = parse_long_opt("word-diff-regex", av, &optarg))) {
		if (options->word_diff == DIFF_WORDS_NONE)
			options->word_diff = DIFF_WORDS_PLAIN;
		options->word_regex = optarg;
		return argcount;
	}
	else if (!strcmp(arg, "--exit-code"))
		DIFF_OPT_SET(options, EXIT_WITH_STATUS);
	else if (!strcmp(arg, "--quiet"))
		DIFF_OPT_SET(options, QUICK);
	else if (!strcmp(arg, "--ext-diff"))
		DIFF_OPT_SET(options, ALLOW_EXTERNAL);
	else if (!strcmp(arg, "--no-ext-diff"))
		DIFF_OPT_CLR(options, ALLOW_EXTERNAL);
	else if (!strcmp(arg, "--textconv"))
		DIFF_OPT_SET(options, ALLOW_TEXTCONV);
	else if (!strcmp(arg, "--no-textconv"))
		DIFF_OPT_CLR(options, ALLOW_TEXTCONV);
	else if (!strcmp(arg, "--ignore-submodules")) {
		DIFF_OPT_SET(options, OVERRIDE_SUBMODULE_CONFIG);
		handle_ignore_submodules_arg(options, "all");
	} else if (!prefixcmp(arg, "--ignore-submodules=")) {
		DIFF_OPT_SET(options, OVERRIDE_SUBMODULE_CONFIG);
		handle_ignore_submodules_arg(options, arg + 20);
	} else if (!strcmp(arg, "--submodule"))
		DIFF_OPT_SET(options, SUBMODULE_LOG);
	else if (!prefixcmp(arg, "--submodule=")) {
		if (!strcmp(arg + 12, "log"))
			DIFF_OPT_SET(options, SUBMODULE_LOG);
	}

	/* misc options */
	else if (!strcmp(arg, "-z"))
		options->line_termination = 0;
	else if ((argcount = short_opt('l', av, &optarg))) {
		options->rename_limit = strtoul(optarg, NULL, 10);
		return argcount;
	}
	else if ((argcount = short_opt('S', av, &optarg))) {
		options->pickaxe = optarg;
		options->pickaxe_opts |= DIFF_PICKAXE_KIND_S;
		return argcount;
	} else if ((argcount = short_opt('G', av, &optarg))) {
		options->pickaxe = optarg;
		options->pickaxe_opts |= DIFF_PICKAXE_KIND_G;
		return argcount;
	}
	else if (!strcmp(arg, "--pickaxe-all"))
		options->pickaxe_opts |= DIFF_PICKAXE_ALL;
	else if (!strcmp(arg, "--pickaxe-regex"))
		options->pickaxe_opts |= DIFF_PICKAXE_REGEX;
	else if ((argcount = short_opt('O', av, &optarg))) {
		options->orderfile = optarg;
		return argcount;
	}
	else if ((argcount = parse_long_opt("diff-filter", av, &optarg))) {
		options->filter = optarg;
		return argcount;
	}
	else if (!strcmp(arg, "--abbrev"))
		options->abbrev = DEFAULT_ABBREV;
	else if (!prefixcmp(arg, "--abbrev=")) {
		options->abbrev = strtoul(arg + 9, NULL, 10);
		if (options->abbrev < MINIMUM_ABBREV)
			options->abbrev = MINIMUM_ABBREV;
		else if (40 < options->abbrev)
			options->abbrev = 40;
	}
	else if ((argcount = parse_long_opt("src-prefix", av, &optarg))) {
		options->a_prefix = optarg;
		return argcount;
	}
	else if ((argcount = parse_long_opt("dst-prefix", av, &optarg))) {
		options->b_prefix = optarg;
		return argcount;
	}
	else if (!strcmp(arg, "--no-prefix"))
		options->a_prefix = options->b_prefix = "";
	else if (opt_arg(arg, '\0', "inter-hunk-context",
			 &options->interhunkcontext))
		;
	else if ((argcount = parse_long_opt("output", av, &optarg))) {
		options->file = fopen(optarg, "w");
		if (!options->file)
			die_errno("Could not open '%s'", optarg);
		options->close_file = 1;
		return argcount;
	} else
		return 0;
	return 1;
}

int parse_rename_score(const char **cp_p)
{
	unsigned long num, scale;
	int ch, dot;
	const char *cp = *cp_p;

	num = 0;
	scale = 1;
	dot = 0;
	for (;;) {
		ch = *cp;
		if ( !dot && ch == '.' ) {
			scale = 1;
			dot = 1;
		} else if ( ch == '%' ) {
			scale = dot ? scale*100 : 100;
			cp++;	/* % is always at the end */
			break;
		} else if ( ch >= '0' && ch <= '9' ) {
			if ( scale < 100000 ) {
				scale *= 10;
				num = (num*10) + (ch-'0');
			}
		} else {
			break;
		}
		cp++;
	}
	*cp_p = cp;

	/* user says num divided by scale and we say internally that
	 * is MAX_SCORE * num / scale.
	 */
	return (int)((num >= scale) ? MAX_SCORE : (MAX_SCORE * num / scale));
}

static int diff_scoreopt_parse(const char *opt)
{
	int opt1, opt2, cmd;

	if (*opt++ != '-')
		return -1;
	cmd = *opt++;
	if (cmd == '-') {
		/* convert the long-form arguments into short-form versions */
		if (!prefixcmp(opt, "break-rewrites")) {
			opt += strlen("break-rewrites");
			if (*opt == 0 || *opt++ == '=')
				cmd = 'B';
		} else if (!prefixcmp(opt, "find-copies")) {
			opt += strlen("find-copies");
			if (*opt == 0 || *opt++ == '=')
				cmd = 'C';
		} else if (!prefixcmp(opt, "find-renames")) {
			opt += strlen("find-renames");
			if (*opt == 0 || *opt++ == '=')
				cmd = 'M';
		}
	}
	if (cmd != 'M' && cmd != 'C' && cmd != 'B')
		return -1; /* that is not a -M, -C nor -B option */

	opt1 = parse_rename_score(&opt);
	if (cmd != 'B')
		opt2 = 0;
	else {
		if (*opt == 0)
			opt2 = 0;
		else if (*opt != '/')
			return -1; /* we expect -B80/99 or -B80 */
		else {
			opt++;
			opt2 = parse_rename_score(&opt);
		}
	}
	if (*opt != 0)
		return -1;
	return opt1 | (opt2 << 16);
}

struct diff_queue_struct diff_queued_diff;

void diff_q(struct diff_queue_struct *queue, struct diff_filepair *dp)
{
	if (queue->alloc <= queue->nr) {
		queue->alloc = alloc_nr(queue->alloc);
		queue->queue = xrealloc(queue->queue,
					sizeof(dp) * queue->alloc);
	}
	queue->queue[queue->nr++] = dp;
}

struct diff_filepair *diff_queue(struct diff_queue_struct *queue,
				 struct diff_filespec *one,
				 struct diff_filespec *two)
{
	struct diff_filepair *dp = xcalloc(1, sizeof(*dp));
	dp->one = one;
	dp->two = two;
	if (queue)
		diff_q(queue, dp);
	return dp;
}

void diff_free_filepair(struct diff_filepair *p)
{
	free_filespec(p->one);
	free_filespec(p->two);
	free(p);
}

/* This is different from find_unique_abbrev() in that
 * it stuffs the result with dots for alignment.
 */
const char *diff_unique_abbrev(const unsigned char *sha1, int len)
{
	int abblen;
	const char *abbrev;
	if (len == 40)
		return sha1_to_hex(sha1);

	abbrev = find_unique_abbrev(sha1, len);
	abblen = strlen(abbrev);
	if (abblen < 37) {
		static char hex[41];
		if (len < abblen && abblen <= len + 2)
			sprintf(hex, "%s%.*s", abbrev, len+3-abblen, "..");
		else
			sprintf(hex, "%s...", abbrev);
		return hex;
	}
	return sha1_to_hex(sha1);
}

static void diff_flush_raw(struct diff_filepair *p, struct diff_options *opt)
{
	int line_termination = opt->line_termination;
	int inter_name_termination = line_termination ? '\t' : '\0';
	if (opt->output_prefix) {
		struct strbuf *msg = NULL;
		msg = opt->output_prefix(opt, opt->output_prefix_data);
		fprintf(opt->file, "%s", msg->buf);
	}

	if (!(opt->output_format & DIFF_FORMAT_NAME_STATUS)) {
		fprintf(opt->file, ":%06o %06o %s ", p->one->mode, p->two->mode,
			diff_unique_abbrev(p->one->sha1, opt->abbrev));
		fprintf(opt->file, "%s ", diff_unique_abbrev(p->two->sha1, opt->abbrev));
	}
	if (p->score) {
		fprintf(opt->file, "%c%03d%c", p->status, similarity_index(p),
			inter_name_termination);
	} else {
		fprintf(opt->file, "%c%c", p->status, inter_name_termination);
	}

	if (p->status == DIFF_STATUS_COPIED ||
	    p->status == DIFF_STATUS_RENAMED) {
		const char *name_a, *name_b;
		name_a = p->one->path;
		name_b = p->two->path;
		strip_prefix(opt->prefix_length, &name_a, &name_b);
		write_name_quoted(name_a, opt->file, inter_name_termination);
		write_name_quoted(name_b, opt->file, line_termination);
	} else {
		const char *name_a, *name_b;
		name_a = p->one->mode ? p->one->path : p->two->path;
		name_b = NULL;
		strip_prefix(opt->prefix_length, &name_a, &name_b);
		write_name_quoted(name_a, opt->file, line_termination);
	}
}

int diff_unmodified_pair(struct diff_filepair *p)
{
	/* This function is written stricter than necessary to support
	 * the currently implemented transformers, but the idea is to
	 * let transformers to produce diff_filepairs any way they want,
	 * and filter and clean them up here before producing the output.
	 */
	struct diff_filespec *one = p->one, *two = p->two;

	if (DIFF_PAIR_UNMERGED(p))
		return 0; /* unmerged is interesting */

	/* deletion, addition, mode or type change
	 * and rename are all interesting.
	 */
	if (DIFF_FILE_VALID(one) != DIFF_FILE_VALID(two) ||
	    DIFF_PAIR_MODE_CHANGED(p) ||
	    strcmp(one->path, two->path))
		return 0;

	/* both are valid and point at the same path.  that is, we are
	 * dealing with a change.
	 */
	if (one->sha1_valid && two->sha1_valid &&
	    !hashcmp(one->sha1, two->sha1) &&
	    !one->dirty_submodule && !two->dirty_submodule)
		return 1; /* no change */
	if (!one->sha1_valid && !two->sha1_valid)
		return 1; /* both look at the same file on the filesystem. */
	return 0;
}

static void diff_flush_patch(struct diff_filepair *p, struct diff_options *o)
{
	if (diff_unmodified_pair(p))
		return;

	if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
	    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
		return; /* no tree diffs in patch format */

	run_diff(p, o);
}

static void diff_flush_stat(struct diff_filepair *p, struct diff_options *o,
			    struct diffstat_t *diffstat)
{
	if (diff_unmodified_pair(p))
		return;

	if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
	    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
		return; /* no useful stat for tree diffs */

	run_diffstat(p, o, diffstat);
}

static void diff_flush_checkdiff(struct diff_filepair *p,
		struct diff_options *o)
{
	if (diff_unmodified_pair(p))
		return;

	if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
	    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
		return; /* nothing to check in tree diffs */

	run_checkdiff(p, o);
}

int diff_queue_is_empty(void)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	for (i = 0; i < q->nr; i++)
		if (!diff_unmodified_pair(q->queue[i]))
			return 0;
	return 1;
}

#if DIFF_DEBUG
void diff_debug_filespec(struct diff_filespec *s, int x, const char *one)
{
	fprintf(stderr, "queue[%d] %s (%s) %s %06o %s\n",
		x, one ? one : "",
		s->path,
		DIFF_FILE_VALID(s) ? "valid" : "invalid",
		s->mode,
		s->sha1_valid ? sha1_to_hex(s->sha1) : "");
	fprintf(stderr, "queue[%d] %s size %lu flags %d\n",
		x, one ? one : "",
		s->size, s->xfrm_flags);
}

void diff_debug_filepair(const struct diff_filepair *p, int i)
{
	diff_debug_filespec(p->one, i, "one");
	diff_debug_filespec(p->two, i, "two");
	fprintf(stderr, "score %d, status %c rename_used %d broken %d\n",
		p->score, p->status ? p->status : '?',
		p->one->rename_used, p->broken_pair);
}

void diff_debug_queue(const char *msg, struct diff_queue_struct *q)
{
	int i;
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "q->nr = %d\n", q->nr);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		diff_debug_filepair(p, i);
	}
}
#endif

static void diff_resolve_rename_copy(void)
{
	int i;
	struct diff_filepair *p;
	struct diff_queue_struct *q = &diff_queued_diff;

	diff_debug_queue("resolve-rename-copy", q);

	for (i = 0; i < q->nr; i++) {
		p = q->queue[i];
		p->status = 0; /* undecided */
		if (DIFF_PAIR_UNMERGED(p))
			p->status = DIFF_STATUS_UNMERGED;
		else if (!DIFF_FILE_VALID(p->one))
			p->status = DIFF_STATUS_ADDED;
		else if (!DIFF_FILE_VALID(p->two))
			p->status = DIFF_STATUS_DELETED;
		else if (DIFF_PAIR_TYPE_CHANGED(p))
			p->status = DIFF_STATUS_TYPE_CHANGED;

		/* from this point on, we are dealing with a pair
		 * whose both sides are valid and of the same type, i.e.
		 * either in-place edit or rename/copy edit.
		 */
		else if (DIFF_PAIR_RENAME(p)) {
			/*
			 * A rename might have re-connected a broken
			 * pair up, causing the pathnames to be the
			 * same again. If so, that's not a rename at
			 * all, just a modification..
			 *
			 * Otherwise, see if this source was used for
			 * multiple renames, in which case we decrement
			 * the count, and call it a copy.
			 */
			if (!strcmp(p->one->path, p->two->path))
				p->status = DIFF_STATUS_MODIFIED;
			else if (--p->one->rename_used > 0)
				p->status = DIFF_STATUS_COPIED;
			else
				p->status = DIFF_STATUS_RENAMED;
		}
		else if (hashcmp(p->one->sha1, p->two->sha1) ||
			 p->one->mode != p->two->mode ||
			 p->one->dirty_submodule ||
			 p->two->dirty_submodule ||
			 is_null_sha1(p->one->sha1))
			p->status = DIFF_STATUS_MODIFIED;
		else {
			/* This is a "no-change" entry and should not
			 * happen anymore, but prepare for broken callers.
			 */
			error("feeding unmodified %s to diffcore",
			      p->one->path);
			p->status = DIFF_STATUS_UNKNOWN;
		}
	}
	diff_debug_queue("resolve-rename-copy done", q);
}

static int check_pair_status(struct diff_filepair *p)
{
	switch (p->status) {
	case DIFF_STATUS_UNKNOWN:
		return 0;
	case 0:
		die("internal error in diff-resolve-rename-copy");
	default:
		return 1;
	}
}

static void flush_one_pair(struct diff_filepair *p, struct diff_options *opt)
{
	int fmt = opt->output_format;

	if (fmt & DIFF_FORMAT_CHECKDIFF)
		diff_flush_checkdiff(p, opt);
	else if (fmt & (DIFF_FORMAT_RAW | DIFF_FORMAT_NAME_STATUS))
		diff_flush_raw(p, opt);
	else if (fmt & DIFF_FORMAT_NAME) {
		const char *name_a, *name_b;
		name_a = p->two->path;
		name_b = NULL;
		strip_prefix(opt->prefix_length, &name_a, &name_b);
		write_name_quoted(name_a, opt->file, opt->line_termination);
	}
}

static void show_file_mode_name(FILE *file, const char *newdelete, struct diff_filespec *fs)
{
	if (fs->mode)
		fprintf(file, " %s mode %06o ", newdelete, fs->mode);
	else
		fprintf(file, " %s ", newdelete);
	write_name_quoted(fs->path, file, '\n');
}


static void show_mode_change(FILE *file, struct diff_filepair *p, int show_name,
		const char *line_prefix)
{
	if (p->one->mode && p->two->mode && p->one->mode != p->two->mode) {
		fprintf(file, "%s mode change %06o => %06o%c", line_prefix, p->one->mode,
			p->two->mode, show_name ? ' ' : '\n');
		if (show_name) {
			write_name_quoted(p->two->path, file, '\n');
		}
	}
}

static void show_rename_copy(FILE *file, const char *renamecopy, struct diff_filepair *p,
			const char *line_prefix)
{
	char *names = pprint_rename(p->one->path, p->two->path);

	fprintf(file, " %s %s (%d%%)\n", renamecopy, names, similarity_index(p));
	free(names);
	show_mode_change(file, p, 0, line_prefix);
}

static void diff_summary(struct diff_options *opt, struct diff_filepair *p)
{
	FILE *file = opt->file;
	char *line_prefix = "";

	if (opt->output_prefix) {
		struct strbuf *buf = opt->output_prefix(opt, opt->output_prefix_data);
		line_prefix = buf->buf;
	}

	switch(p->status) {
	case DIFF_STATUS_DELETED:
		fputs(line_prefix, file);
		show_file_mode_name(file, "delete", p->one);
		break;
	case DIFF_STATUS_ADDED:
		fputs(line_prefix, file);
		show_file_mode_name(file, "create", p->two);
		break;
	case DIFF_STATUS_COPIED:
		fputs(line_prefix, file);
		show_rename_copy(file, "copy", p, line_prefix);
		break;
	case DIFF_STATUS_RENAMED:
		fputs(line_prefix, file);
		show_rename_copy(file, "rename", p, line_prefix);
		break;
	default:
		if (p->score) {
			fprintf(file, "%s rewrite ", line_prefix);
			write_name_quoted(p->two->path, file, ' ');
			fprintf(file, "(%d%%)\n", similarity_index(p));
		}
		show_mode_change(file, p, !p->score, line_prefix);
		break;
	}
}

struct patch_id_t {
	git_SHA_CTX *ctx;
	int patchlen;
};

static int remove_space(char *line, int len)
{
	int i;
	char *dst = line;
	unsigned char c;

	for (i = 0; i < len; i++)
		if (!isspace((c = line[i])))
			*dst++ = c;

	return dst - line;
}

static void patch_id_consume(void *priv, char *line, unsigned long len)
{
	struct patch_id_t *data = priv;
	int new_len;

	/* Ignore line numbers when computing the SHA1 of the patch */
	if (!prefixcmp(line, "@@ -"))
		return;

	new_len = remove_space(line, len);

	git_SHA1_Update(data->ctx, line, new_len);
	data->patchlen += new_len;
}

/* returns 0 upon success, and writes result into sha1 */
static int diff_get_patch_id(struct diff_options *options, unsigned char *sha1)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	git_SHA_CTX ctx;
	struct patch_id_t data;
	char buffer[PATH_MAX * 4 + 20];

	git_SHA1_Init(&ctx);
	memset(&data, 0, sizeof(struct patch_id_t));
	data.ctx = &ctx;

	for (i = 0; i < q->nr; i++) {
		xpparam_t xpp;
		xdemitconf_t xecfg;
		mmfile_t mf1, mf2;
		struct diff_filepair *p = q->queue[i];
		int len1, len2;

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		if (p->status == 0)
			return error("internal diff status error");
		if (p->status == DIFF_STATUS_UNKNOWN)
			continue;
		if (diff_unmodified_pair(p))
			continue;
		if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
		    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
			continue;
		if (DIFF_PAIR_UNMERGED(p))
			continue;

		diff_fill_sha1_info(p->one);
		diff_fill_sha1_info(p->two);
		if (fill_mmfile(&mf1, p->one) < 0 ||
				fill_mmfile(&mf2, p->two) < 0)
			return error("unable to read files to diff");

		len1 = remove_space(p->one->path, strlen(p->one->path));
		len2 = remove_space(p->two->path, strlen(p->two->path));
		if (p->one->mode == 0)
			len1 = snprintf(buffer, sizeof(buffer),
					"diff--gita/%.*sb/%.*s"
					"newfilemode%06o"
					"---/dev/null"
					"+++b/%.*s",
					len1, p->one->path,
					len2, p->two->path,
					p->two->mode,
					len2, p->two->path);
		else if (p->two->mode == 0)
			len1 = snprintf(buffer, sizeof(buffer),
					"diff--gita/%.*sb/%.*s"
					"deletedfilemode%06o"
					"---a/%.*s"
					"+++/dev/null",
					len1, p->one->path,
					len2, p->two->path,
					p->one->mode,
					len1, p->one->path);
		else
			len1 = snprintf(buffer, sizeof(buffer),
					"diff--gita/%.*sb/%.*s"
					"---a/%.*s"
					"+++b/%.*s",
					len1, p->one->path,
					len2, p->two->path,
					len1, p->one->path,
					len2, p->two->path);
		git_SHA1_Update(&ctx, buffer, len1);

		if (diff_filespec_is_binary(p->one) ||
		    diff_filespec_is_binary(p->two)) {
			git_SHA1_Update(&ctx, sha1_to_hex(p->one->sha1), 40);
			git_SHA1_Update(&ctx, sha1_to_hex(p->two->sha1), 40);
			continue;
		}

		xpp.flags = 0;
		xecfg.ctxlen = 3;
		xecfg.flags = 0;
		xdi_diff_outf(&mf1, &mf2, patch_id_consume, &data,
			      &xpp, &xecfg);
	}

	git_SHA1_Final(sha1, &ctx);
	return 0;
}

int diff_flush_patch_id(struct diff_options *options, unsigned char *sha1)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	int result = diff_get_patch_id(options, sha1);

	for (i = 0; i < q->nr; i++)
		diff_free_filepair(q->queue[i]);

	free(q->queue);
	DIFF_QUEUE_CLEAR(q);

	return result;
}

static int is_summary_empty(const struct diff_queue_struct *q)
{
	int i;

	for (i = 0; i < q->nr; i++) {
		const struct diff_filepair *p = q->queue[i];

		switch (p->status) {
		case DIFF_STATUS_DELETED:
		case DIFF_STATUS_ADDED:
		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			return 0;
		default:
			if (p->score)
				return 0;
			if (p->one->mode && p->two->mode &&
			    p->one->mode != p->two->mode)
				return 0;
			break;
		}
	}
	return 1;
}

static const char rename_limit_warning[] =
"inexact rename detection was skipped due to too many files.";

static const char degrade_cc_to_c_warning[] =
"only found copies from modified paths due to too many files.";

static const char rename_limit_advice[] =
"you may want to set your %s variable to at least "
"%d and retry the command.";

void diff_warn_rename_limit(const char *varname, int needed, int degraded_cc)
{
	if (degraded_cc)
		warning(degrade_cc_to_c_warning);
	else if (needed)
		warning(rename_limit_warning);
	else
		return;
	if (0 < needed && needed < 32767)
		warning(rename_limit_advice, varname, needed);
}

void diff_flush(struct diff_options *options)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i, output_format = options->output_format;
	int separator = 0;
	int dirstat_by_line = 0;

	/*
	 * Order: raw, stat, summary, patch
	 * or:    name/name-status/checkdiff (other bits clear)
	 */
	if (!q->nr)
		goto free_queue;

	if (output_format & (DIFF_FORMAT_RAW |
			     DIFF_FORMAT_NAME |
			     DIFF_FORMAT_NAME_STATUS |
			     DIFF_FORMAT_CHECKDIFF)) {
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				flush_one_pair(p, options);
		}
		separator++;
	}

	if (output_format & DIFF_FORMAT_DIRSTAT && DIFF_OPT_TST(options, DIRSTAT_BY_LINE))
		dirstat_by_line = 1;

	if (output_format & (DIFF_FORMAT_DIFFSTAT|DIFF_FORMAT_SHORTSTAT|DIFF_FORMAT_NUMSTAT) ||
	    dirstat_by_line) {
		struct diffstat_t diffstat;

		memset(&diffstat, 0, sizeof(struct diffstat_t));
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				diff_flush_stat(p, options, &diffstat);
		}
		if (output_format & DIFF_FORMAT_NUMSTAT)
			show_numstat(&diffstat, options);
		if (output_format & DIFF_FORMAT_DIFFSTAT)
			show_stats(&diffstat, options);
		if (output_format & DIFF_FORMAT_SHORTSTAT)
			show_shortstats(&diffstat, options);
		if (output_format & DIFF_FORMAT_DIRSTAT)
			show_dirstat_by_line(&diffstat, options);
		free_diffstat_info(&diffstat);
		separator++;
	}
	if ((output_format & DIFF_FORMAT_DIRSTAT) && !dirstat_by_line)
		show_dirstat(options);

	if (output_format & DIFF_FORMAT_SUMMARY && !is_summary_empty(q)) {
		for (i = 0; i < q->nr; i++) {
			diff_summary(options, q->queue[i]);
		}
		separator++;
	}

	if (output_format & DIFF_FORMAT_NO_OUTPUT &&
	    DIFF_OPT_TST(options, EXIT_WITH_STATUS) &&
	    DIFF_OPT_TST(options, DIFF_FROM_CONTENTS)) {
		/*
		 * run diff_flush_patch for the exit status. setting
		 * options->file to /dev/null should be safe, becaue we
		 * aren't supposed to produce any output anyway.
		 */
		if (options->close_file)
			fclose(options->file);
		options->file = fopen("/dev/null", "w");
		if (!options->file)
			die_errno("Could not open /dev/null");
		options->close_file = 1;
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				diff_flush_patch(p, options);
			if (options->found_changes)
				break;
		}
	}

	if (output_format & DIFF_FORMAT_PATCH) {
		if (separator) {
			putc(options->line_termination, options->file);
			if (options->stat_sep) {
				/* attach patch instead of inline */
				fputs(options->stat_sep, options->file);
			}
		}

		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				diff_flush_patch(p, options);
		}
	}

	if (output_format & DIFF_FORMAT_CALLBACK)
		options->format_callback(q, options, options->format_callback_data);

	for (i = 0; i < q->nr; i++)
		diff_free_filepair(q->queue[i]);
free_queue:
	free(q->queue);
	DIFF_QUEUE_CLEAR(q);
	if (options->close_file)
		fclose(options->file);

	/*
	 * Report the content-level differences with HAS_CHANGES;
	 * diff_addremove/diff_change does not set the bit when
	 * DIFF_FROM_CONTENTS is in effect (e.g. with -w).
	 */
	if (DIFF_OPT_TST(options, DIFF_FROM_CONTENTS)) {
		if (options->found_changes)
			DIFF_OPT_SET(options, HAS_CHANGES);
		else
			DIFF_OPT_CLR(options, HAS_CHANGES);
	}
}

static void diffcore_apply_filter(const char *filter)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	DIFF_QUEUE_CLEAR(&outq);

	if (!filter)
		return;

	if (strchr(filter, DIFF_STATUS_FILTER_AON)) {
		int found;
		for (i = found = 0; !found && i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (((p->status == DIFF_STATUS_MODIFIED) &&
			     ((p->score &&
			       strchr(filter, DIFF_STATUS_FILTER_BROKEN)) ||
			      (!p->score &&
			       strchr(filter, DIFF_STATUS_MODIFIED)))) ||
			    ((p->status != DIFF_STATUS_MODIFIED) &&
			     strchr(filter, p->status)))
				found++;
		}
		if (found)
			return;

		/* otherwise we will clear the whole queue
		 * by copying the empty outq at the end of this
		 * function, but first clear the current entries
		 * in the queue.
		 */
		for (i = 0; i < q->nr; i++)
			diff_free_filepair(q->queue[i]);
	}
	else {
		/* Only the matching ones */
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];

			if (((p->status == DIFF_STATUS_MODIFIED) &&
			     ((p->score &&
			       strchr(filter, DIFF_STATUS_FILTER_BROKEN)) ||
			      (!p->score &&
			       strchr(filter, DIFF_STATUS_MODIFIED)))) ||
			    ((p->status != DIFF_STATUS_MODIFIED) &&
			     strchr(filter, p->status)))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}
	free(q->queue);
	*q = outq;
}

/* Check whether two filespecs with the same mode and size are identical */
static int diff_filespec_is_identical(struct diff_filespec *one,
				      struct diff_filespec *two)
{
	if (S_ISGITLINK(one->mode))
		return 0;
	if (diff_populate_filespec(one, 0))
		return 0;
	if (diff_populate_filespec(two, 0))
		return 0;
	return !memcmp(one->data, two->data, one->size);
}

static void diffcore_skip_stat_unmatch(struct diff_options *diffopt)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	DIFF_QUEUE_CLEAR(&outq);

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];

		/*
		 * 1. Entries that come from stat info dirtiness
		 *    always have both sides (iow, not create/delete),
		 *    one side of the object name is unknown, with
		 *    the same mode and size.  Keep the ones that
		 *    do not match these criteria.  They have real
		 *    differences.
		 *
		 * 2. At this point, the file is known to be modified,
		 *    with the same mode and size, and the object
		 *    name of one side is unknown.  Need to inspect
		 *    the identical contents.
		 */
		if (!DIFF_FILE_VALID(p->one) || /* (1) */
		    !DIFF_FILE_VALID(p->two) ||
		    (p->one->sha1_valid && p->two->sha1_valid) ||
		    (p->one->mode != p->two->mode) ||
		    diff_populate_filespec(p->one, 1) ||
		    diff_populate_filespec(p->two, 1) ||
		    (p->one->size != p->two->size) ||
		    !diff_filespec_is_identical(p->one, p->two)) /* (2) */
			diff_q(&outq, p);
		else {
			/*
			 * The caller can subtract 1 from skip_stat_unmatch
			 * to determine how many paths were dirty only
			 * due to stat info mismatch.
			 */
			if (!DIFF_OPT_TST(diffopt, NO_INDEX))
				diffopt->skip_stat_unmatch++;
			diff_free_filepair(p);
		}
	}
	free(q->queue);
	*q = outq;
}

static int diffnamecmp(const void *a_, const void *b_)
{
	const struct diff_filepair *a = *((const struct diff_filepair **)a_);
	const struct diff_filepair *b = *((const struct diff_filepair **)b_);
	const char *name_a, *name_b;

	name_a = a->one ? a->one->path : a->two->path;
	name_b = b->one ? b->one->path : b->two->path;
	return strcmp(name_a, name_b);
}

void diffcore_fix_diff_index(struct diff_options *options)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	qsort(q->queue, q->nr, sizeof(q->queue[0]), diffnamecmp);
}

void diffcore_std(struct diff_options *options)
{
	if (options->skip_stat_unmatch)
		diffcore_skip_stat_unmatch(options);
	if (!options->found_follow) {
		/* See try_to_follow_renames() in tree-diff.c */
		if (options->break_opt != -1)
			diffcore_break(options->break_opt);
		if (options->detect_rename)
			diffcore_rename(options);
		if (options->break_opt != -1)
			diffcore_merge_broken();
	}
	if (options->pickaxe)
		diffcore_pickaxe(options);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	if (!options->found_follow)
		/* See try_to_follow_renames() in tree-diff.c */
		diff_resolve_rename_copy();
	diffcore_apply_filter(options->filter);

	if (diff_queued_diff.nr && !DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
		DIFF_OPT_SET(options, HAS_CHANGES);
	else
		DIFF_OPT_CLR(options, HAS_CHANGES);

	options->found_follow = 0;
}

int diff_result_code(struct diff_options *opt, int status)
{
	int result = 0;

	diff_warn_rename_limit("diff.renamelimit",
			       opt->needed_rename_limit,
			       opt->degraded_cc_to_c);
	if (!DIFF_OPT_TST(opt, EXIT_WITH_STATUS) &&
	    !(opt->output_format & DIFF_FORMAT_CHECKDIFF))
		return status;
	if (DIFF_OPT_TST(opt, EXIT_WITH_STATUS) &&
	    DIFF_OPT_TST(opt, HAS_CHANGES))
		result |= 01;
	if ((opt->output_format & DIFF_FORMAT_CHECKDIFF) &&
	    DIFF_OPT_TST(opt, CHECK_FAILED))
		result |= 02;
	return result;
}

int diff_can_quit_early(struct diff_options *opt)
{
	return (DIFF_OPT_TST(opt, QUICK) &&
		!opt->filter &&
		DIFF_OPT_TST(opt, HAS_CHANGES));
}

/*
 * Shall changes to this submodule be ignored?
 *
 * Submodule changes can be configured to be ignored separately for each path,
 * but that configuration can be overridden from the command line.
 */
static int is_submodule_ignored(const char *path, struct diff_options *options)
{
	int ignored = 0;
	unsigned orig_flags = options->flags;
	if (!DIFF_OPT_TST(options, OVERRIDE_SUBMODULE_CONFIG))
		set_diffopt_flags_from_submodule_config(options, path);
	if (DIFF_OPT_TST(options, IGNORE_SUBMODULES))
		ignored = 1;
	options->flags = orig_flags;
	return ignored;
}

void diff_addremove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *concatpath, unsigned dirty_submodule)
{
	struct diff_filespec *one, *two;

	if (S_ISGITLINK(mode) && is_submodule_ignored(concatpath, options))
		return;

	/* This may look odd, but it is a preparation for
	 * feeding "there are unchanged files which should
	 * not produce diffs, but when you are doing copy
	 * detection you would need them, so here they are"
	 * entries to the diff-core.  They will be prefixed
	 * with something like '=' or '*' (I haven't decided
	 * which but should not make any difference).
	 * Feeding the same new and old to diff_change()
	 * also has the same effect.
	 * Before the final output happens, they are pruned after
	 * merged into rename/copy pairs as appropriate.
	 */
	if (DIFF_OPT_TST(options, REVERSE_DIFF))
		addremove = (addremove == '+' ? '-' :
			     addremove == '-' ? '+' : addremove);

	if (options->prefix &&
	    strncmp(concatpath, options->prefix, options->prefix_length))
		return;

	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);

	if (addremove != '+')
		fill_filespec(one, sha1, mode);
	if (addremove != '-') {
		fill_filespec(two, sha1, mode);
		two->dirty_submodule = dirty_submodule;
	}

	diff_queue(&diff_queued_diff, one, two);
	if (!DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
		DIFF_OPT_SET(options, HAS_CHANGES);
}

void diff_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *concatpath,
		 unsigned old_dirty_submodule, unsigned new_dirty_submodule)
{
	struct diff_filespec *one, *two;

	if (S_ISGITLINK(old_mode) && S_ISGITLINK(new_mode) &&
	    is_submodule_ignored(concatpath, options))
		return;

	if (DIFF_OPT_TST(options, REVERSE_DIFF)) {
		unsigned tmp;
		const unsigned char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_c = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_c;
		tmp = old_dirty_submodule; old_dirty_submodule = new_dirty_submodule;
			new_dirty_submodule = tmp;
	}

	if (options->prefix &&
	    strncmp(concatpath, options->prefix, options->prefix_length))
		return;

	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);
	fill_filespec(one, old_sha1, old_mode);
	fill_filespec(two, new_sha1, new_mode);
	one->dirty_submodule = old_dirty_submodule;
	two->dirty_submodule = new_dirty_submodule;

	diff_queue(&diff_queued_diff, one, two);
	if (!DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
		DIFF_OPT_SET(options, HAS_CHANGES);
}

struct diff_filepair *diff_unmerge(struct diff_options *options, const char *path)
{
	struct diff_filepair *pair;
	struct diff_filespec *one, *two;

	if (options->prefix &&
	    strncmp(path, options->prefix, options->prefix_length))
		return NULL;

	one = alloc_filespec(path);
	two = alloc_filespec(path);
	pair = diff_queue(&diff_queued_diff, one, two);
	pair->is_unmerged = 1;
	return pair;
}

static char *run_textconv(const char *pgm, struct diff_filespec *spec,
		size_t *outsize)
{
	struct diff_tempfile *temp;
	const char *argv[3];
	const char **arg = argv;
	struct child_process child;
	struct strbuf buf = STRBUF_INIT;
	int err = 0;

	temp = prepare_temp_file(spec->path, spec);
	*arg++ = pgm;
	*arg++ = temp->name;
	*arg = NULL;

	memset(&child, 0, sizeof(child));
	child.use_shell = 1;
	child.argv = argv;
	child.out = -1;
	if (start_command(&child)) {
		remove_tempfile();
		return NULL;
	}

	if (strbuf_read(&buf, child.out, 0) < 0)
		err = error("error reading from textconv command '%s'", pgm);
	close(child.out);

	if (finish_command(&child) || err) {
		strbuf_release(&buf);
		remove_tempfile();
		return NULL;
	}
	remove_tempfile();

	return strbuf_detach(&buf, outsize);
}

size_t fill_textconv(struct userdiff_driver *driver,
		     struct diff_filespec *df,
		     char **outbuf)
{
	size_t size;

	if (!driver || !driver->textconv) {
		if (!DIFF_FILE_VALID(df)) {
			*outbuf = "";
			return 0;
		}
		if (diff_populate_filespec(df, 0))
			die("unable to read files to diff");
		*outbuf = df->data;
		return df->size;
	}

	if (driver->textconv_cache && df->sha1_valid) {
		*outbuf = notes_cache_get(driver->textconv_cache, df->sha1,
					  &size);
		if (*outbuf)
			return size;
	}

	*outbuf = run_textconv(driver->textconv, df, &size);
	if (!*outbuf)
		die("unable to read files to diff");

	if (driver->textconv_cache && df->sha1_valid) {
		/* ignore errors, as we might be in a readonly repository */
		notes_cache_put(driver->textconv_cache, df->sha1, *outbuf,
				size);
		/*
		 * we could save up changes and flush them all at the end,
		 * but we would need an extra call after all diffing is done.
		 * Since generating a cache entry is the slow path anyway,
		 * this extra overhead probably isn't a big deal.
		 */
		notes_cache_write(driver->textconv_cache);
	}

	return size;
}
