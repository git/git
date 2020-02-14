/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "config.h"
#include "tempfile.h"
#include "quote.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "xdiff-interface.h"
#include "color.h"
#include "attr.h"
#include "run-command.h"
#include "utf8.h"
#include "object-store.h"
#include "userdiff.h"
#include "submodule-config.h"
#include "submodule.h"
#include "hashmap.h"
#include "ll-merge.h"
#include "string-list.h"
#include "argv-array.h"
#include "graph.h"
#include "packfile.h"
#include "parse-options.h"
#include "help.h"
#include "promisor-remote.h"

#ifdef NO_FAST_WORKING_DIRECTORY
#define FAST_WORKING_DIRECTORY 0
#else
#define FAST_WORKING_DIRECTORY 1
#endif

static int diff_detect_rename_default;
static int diff_indent_heuristic = 1;
static int diff_rename_limit_default = 400;
static int diff_suppress_blank_empty;
static int diff_use_color_default = -1;
static int diff_color_moved_default;
static int diff_color_moved_ws_default;
static int diff_context_default = 3;
static int diff_interhunk_context_default;
static const char *diff_word_regex_cfg;
static const char *external_diff_cmd_cfg;
static const char *diff_order_file_cfg;
int diff_auto_refresh_index = 1;
static int diff_mnemonic_prefix;
static int diff_no_prefix;
static int diff_stat_graph_width;
static int diff_dirstat_permille_default = 30;
static struct diff_options default_diff_options;
static long diff_algorithm;
static unsigned ws_error_highlight_default = WSEH_NEW;

static char diff_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,	/* CONTEXT */
	GIT_COLOR_BOLD,		/* METAINFO */
	GIT_COLOR_CYAN,		/* FRAGINFO */
	GIT_COLOR_RED,		/* OLD */
	GIT_COLOR_GREEN,	/* NEW */
	GIT_COLOR_YELLOW,	/* COMMIT */
	GIT_COLOR_BG_RED,	/* WHITESPACE */
	GIT_COLOR_NORMAL,	/* FUNCINFO */
	GIT_COLOR_BOLD_MAGENTA,	/* OLD_MOVED */
	GIT_COLOR_BOLD_BLUE,	/* OLD_MOVED ALTERNATIVE */
	GIT_COLOR_FAINT,	/* OLD_MOVED_DIM */
	GIT_COLOR_FAINT_ITALIC,	/* OLD_MOVED_ALTERNATIVE_DIM */
	GIT_COLOR_BOLD_CYAN,	/* NEW_MOVED */
	GIT_COLOR_BOLD_YELLOW,	/* NEW_MOVED ALTERNATIVE */
	GIT_COLOR_FAINT,	/* NEW_MOVED_DIM */
	GIT_COLOR_FAINT_ITALIC,	/* NEW_MOVED_ALTERNATIVE_DIM */
	GIT_COLOR_FAINT,	/* CONTEXT_DIM */
	GIT_COLOR_FAINT_RED,	/* OLD_DIM */
	GIT_COLOR_FAINT_GREEN,	/* NEW_DIM */
	GIT_COLOR_BOLD,		/* CONTEXT_BOLD */
	GIT_COLOR_BOLD_RED,	/* OLD_BOLD */
	GIT_COLOR_BOLD_GREEN,	/* NEW_BOLD */
};

static const char *color_diff_slots[] = {
	[DIFF_CONTEXT]		      = "context",
	[DIFF_METAINFO]		      = "meta",
	[DIFF_FRAGINFO]		      = "frag",
	[DIFF_FILE_OLD]		      = "old",
	[DIFF_FILE_NEW]		      = "new",
	[DIFF_COMMIT]		      = "commit",
	[DIFF_WHITESPACE]	      = "whitespace",
	[DIFF_FUNCINFO]		      = "func",
	[DIFF_FILE_OLD_MOVED]	      = "oldMoved",
	[DIFF_FILE_OLD_MOVED_ALT]     = "oldMovedAlternative",
	[DIFF_FILE_OLD_MOVED_DIM]     = "oldMovedDimmed",
	[DIFF_FILE_OLD_MOVED_ALT_DIM] = "oldMovedAlternativeDimmed",
	[DIFF_FILE_NEW_MOVED]	      = "newMoved",
	[DIFF_FILE_NEW_MOVED_ALT]     = "newMovedAlternative",
	[DIFF_FILE_NEW_MOVED_DIM]     = "newMovedDimmed",
	[DIFF_FILE_NEW_MOVED_ALT_DIM] = "newMovedAlternativeDimmed",
	[DIFF_CONTEXT_DIM]	      = "contextDimmed",
	[DIFF_FILE_OLD_DIM]	      = "oldDimmed",
	[DIFF_FILE_NEW_DIM]	      = "newDimmed",
	[DIFF_CONTEXT_BOLD]	      = "contextBold",
	[DIFF_FILE_OLD_BOLD]	      = "oldBold",
	[DIFF_FILE_NEW_BOLD]	      = "newBold",
};

define_list_config_array_extra(color_diff_slots, {"plain"});

static int parse_diff_color_slot(const char *var)
{
	if (!strcasecmp(var, "plain"))
		return DIFF_CONTEXT;
	return LOOKUP_CONFIG(color_diff_slots, var);
}

static int parse_dirstat_params(struct diff_options *options, const char *params_string,
				struct strbuf *errmsg)
{
	char *params_copy = xstrdup(params_string);
	struct string_list params = STRING_LIST_INIT_NODUP;
	int ret = 0;
	int i;

	if (*params_copy)
		string_list_split_in_place(&params, params_copy, ',', -1);
	for (i = 0; i < params.nr; i++) {
		const char *p = params.items[i].string;
		if (!strcmp(p, "changes")) {
			options->flags.dirstat_by_line = 0;
			options->flags.dirstat_by_file = 0;
		} else if (!strcmp(p, "lines")) {
			options->flags.dirstat_by_line = 1;
			options->flags.dirstat_by_file = 0;
		} else if (!strcmp(p, "files")) {
			options->flags.dirstat_by_line = 0;
			options->flags.dirstat_by_file = 1;
		} else if (!strcmp(p, "noncumulative")) {
			options->flags.dirstat_cumulative = 0;
		} else if (!strcmp(p, "cumulative")) {
			options->flags.dirstat_cumulative = 1;
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
			if (!*end)
				options->dirstat_permille = permille;
			else {
				strbuf_addf(errmsg, _("  Failed to parse dirstat cut-off percentage '%s'\n"),
					    p);
				ret++;
			}
		} else {
			strbuf_addf(errmsg, _("  Unknown dirstat parameter '%s'\n"), p);
			ret++;
		}

	}
	string_list_clear(&params, 0);
	free(params_copy);
	return ret;
}

static int parse_submodule_params(struct diff_options *options, const char *value)
{
	if (!strcmp(value, "log"))
		options->submodule_format = DIFF_SUBMODULE_LOG;
	else if (!strcmp(value, "short"))
		options->submodule_format = DIFF_SUBMODULE_SHORT;
	else if (!strcmp(value, "diff"))
		options->submodule_format = DIFF_SUBMODULE_INLINE_DIFF;
	/*
	 * Please update $__git_diff_submodule_formats in
	 * git-completion.bash when you add new formats.
	 */
	else
		return -1;
	return 0;
}

int git_config_rename(const char *var, const char *value)
{
	if (!value)
		return DIFF_DETECT_RENAME;
	if (!strcasecmp(value, "copies") || !strcasecmp(value, "copy"))
		return  DIFF_DETECT_COPY;
	return git_config_bool(var,value) ? DIFF_DETECT_RENAME : 0;
}

long parse_algorithm_value(const char *value)
{
	if (!value)
		return -1;
	else if (!strcasecmp(value, "myers") || !strcasecmp(value, "default"))
		return 0;
	else if (!strcasecmp(value, "minimal"))
		return XDF_NEED_MINIMAL;
	else if (!strcasecmp(value, "patience"))
		return XDF_PATIENCE_DIFF;
	else if (!strcasecmp(value, "histogram"))
		return XDF_HISTOGRAM_DIFF;
	/*
	 * Please update $__git_diff_algorithms in git-completion.bash
	 * when you add new algorithms.
	 */
	return -1;
}

static int parse_one_token(const char **arg, const char *token)
{
	const char *rest;
	if (skip_prefix(*arg, token, &rest) && (!*rest || *rest == ',')) {
		*arg = rest;
		return 1;
	}
	return 0;
}

static int parse_ws_error_highlight(const char *arg)
{
	const char *orig_arg = arg;
	unsigned val = 0;

	while (*arg) {
		if (parse_one_token(&arg, "none"))
			val = 0;
		else if (parse_one_token(&arg, "default"))
			val = WSEH_NEW;
		else if (parse_one_token(&arg, "all"))
			val = WSEH_NEW | WSEH_OLD | WSEH_CONTEXT;
		else if (parse_one_token(&arg, "new"))
			val |= WSEH_NEW;
		else if (parse_one_token(&arg, "old"))
			val |= WSEH_OLD;
		else if (parse_one_token(&arg, "context"))
			val |= WSEH_CONTEXT;
		else {
			return -1 - (int)(arg - orig_arg);
		}
		if (*arg)
			arg++;
	}
	return val;
}

/*
 * These are to give UI layer defaults.
 * The core-level commands such as git-diff-files should
 * never be affected by the setting of diff.renames
 * the user happens to have in the configuration file.
 */
void init_diff_ui_defaults(void)
{
	diff_detect_rename_default = DIFF_DETECT_RENAME;
}

int git_diff_heuristic_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "diff.indentheuristic"))
		diff_indent_heuristic = git_config_bool(var, value);
	return 0;
}

static int parse_color_moved(const char *arg)
{
	switch (git_parse_maybe_bool(arg)) {
	case 0:
		return COLOR_MOVED_NO;
	case 1:
		return COLOR_MOVED_DEFAULT;
	default:
		break;
	}

	if (!strcmp(arg, "no"))
		return COLOR_MOVED_NO;
	else if (!strcmp(arg, "plain"))
		return COLOR_MOVED_PLAIN;
	else if (!strcmp(arg, "blocks"))
		return COLOR_MOVED_BLOCKS;
	else if (!strcmp(arg, "zebra"))
		return COLOR_MOVED_ZEBRA;
	else if (!strcmp(arg, "default"))
		return COLOR_MOVED_DEFAULT;
	else if (!strcmp(arg, "dimmed-zebra"))
		return COLOR_MOVED_ZEBRA_DIM;
	else if (!strcmp(arg, "dimmed_zebra"))
		return COLOR_MOVED_ZEBRA_DIM;
	else
		return error(_("color moved setting must be one of 'no', 'default', 'blocks', 'zebra', 'dimmed-zebra', 'plain'"));
}

static unsigned parse_color_moved_ws(const char *arg)
{
	int ret = 0;
	struct string_list l = STRING_LIST_INIT_DUP;
	struct string_list_item *i;

	string_list_split(&l, arg, ',', -1);

	for_each_string_list_item(i, &l) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addstr(&sb, i->string);
		strbuf_trim(&sb);

		if (!strcmp(sb.buf, "no"))
			ret = 0;
		else if (!strcmp(sb.buf, "ignore-space-change"))
			ret |= XDF_IGNORE_WHITESPACE_CHANGE;
		else if (!strcmp(sb.buf, "ignore-space-at-eol"))
			ret |= XDF_IGNORE_WHITESPACE_AT_EOL;
		else if (!strcmp(sb.buf, "ignore-all-space"))
			ret |= XDF_IGNORE_WHITESPACE;
		else if (!strcmp(sb.buf, "allow-indentation-change"))
			ret |= COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE;
		else {
			ret |= COLOR_MOVED_WS_ERROR;
			error(_("unknown color-moved-ws mode '%s', possible values are 'ignore-space-change', 'ignore-space-at-eol', 'ignore-all-space', 'allow-indentation-change'"), sb.buf);
		}

		strbuf_release(&sb);
	}

	if ((ret & COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE) &&
	    (ret & XDF_WHITESPACE_FLAGS)) {
		error(_("color-moved-ws: allow-indentation-change cannot be combined with other whitespace modes"));
		ret |= COLOR_MOVED_WS_ERROR;
	}

	string_list_clear(&l, 0);

	return ret;
}

int git_diff_ui_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff")) {
		diff_use_color_default = git_config_colorbool(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.colormoved")) {
		int cm = parse_color_moved(value);
		if (cm < 0)
			return -1;
		diff_color_moved_default = cm;
		return 0;
	}
	if (!strcmp(var, "diff.colormovedws")) {
		unsigned cm = parse_color_moved_ws(value);
		if (cm & COLOR_MOVED_WS_ERROR)
			return -1;
		diff_color_moved_ws_default = cm;
		return 0;
	}
	if (!strcmp(var, "diff.context")) {
		diff_context_default = git_config_int(var, value);
		if (diff_context_default < 0)
			return -1;
		return 0;
	}
	if (!strcmp(var, "diff.interhunkcontext")) {
		diff_interhunk_context_default = git_config_int(var, value);
		if (diff_interhunk_context_default < 0)
			return -1;
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
	if (!strcmp(var, "diff.statgraphwidth")) {
		diff_stat_graph_width = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.external"))
		return git_config_string(&external_diff_cmd_cfg, var, value);
	if (!strcmp(var, "diff.wordregex"))
		return git_config_string(&diff_word_regex_cfg, var, value);
	if (!strcmp(var, "diff.orderfile"))
		return git_config_pathname(&diff_order_file_cfg, var, value);

	if (!strcmp(var, "diff.ignoresubmodules"))
		handle_ignore_submodules_arg(&default_diff_options, value);

	if (!strcmp(var, "diff.submodule")) {
		if (parse_submodule_params(&default_diff_options, value))
			warning(_("Unknown value for 'diff.submodule' config variable: '%s'"),
				value);
		return 0;
	}

	if (!strcmp(var, "diff.algorithm")) {
		diff_algorithm = parse_algorithm_value(value);
		if (diff_algorithm < 0)
			return -1;
		return 0;
	}

	if (git_color_config(var, value, cb) < 0)
		return -1;

	return git_diff_basic_config(var, value, cb);
}

int git_diff_basic_config(const char *var, const char *value, void *cb)
{
	const char *name;

	if (!strcmp(var, "diff.renamelimit")) {
		diff_rename_limit_default = git_config_int(var, value);
		return 0;
	}

	if (userdiff_config(var, value) < 0)
		return -1;

	if (skip_prefix(var, "diff.color.", &name) ||
	    skip_prefix(var, "color.diff.", &name)) {
		int slot = parse_diff_color_slot(name);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		return color_parse(value, diff_colors[slot]);
	}

	if (!strcmp(var, "diff.wserrorhighlight")) {
		int val = parse_ws_error_highlight(value);
		if (val < 0)
			return -1;
		ws_error_highlight_default = val;
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

	if (git_diff_heuristic_config(var, value, cb) < 0)
		return -1;

	return git_default_config(var, value, cb);
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
	external_diff_cmd = xstrdup_or_null(getenv("GIT_EXTERNAL_DIFF"));
	if (!external_diff_cmd)
		external_diff_cmd = external_diff_cmd_cfg;
	done_preparing = 1;
	return external_diff_cmd;
}

/*
 * Keep track of files used for diffing. Sometimes such an entry
 * refers to a temporary file, sometimes to an existing file, and
 * sometimes to "/dev/null".
 */
static struct diff_tempfile {
	/*
	 * filename external diff should read from, or NULL if this
	 * entry is currently not in use:
	 */
	const char *name;

	char hex[GIT_MAX_HEXSZ + 1];
	char mode[10];

	/*
	 * If this diff_tempfile instance refers to a temporary file,
	 * this tempfile object is used to manage its lifetime.
	 */
	struct tempfile *tempfile;
} diff_temp[2];

struct emit_callback {
	int color_diff;
	unsigned ws_rule;
	int blank_at_eof_in_preimage;
	int blank_at_eof_in_postimage;
	int lno_in_preimage;
	int lno_in_postimage;
	const char **label_path;
	struct diff_words_data *diff_words;
	struct diff_options *opt;
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

static int fill_mmfile(struct repository *r, mmfile_t *mf,
		       struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one)) {
		mf->ptr = (char *)""; /* does not matter */
		mf->size = 0;
		return 0;
	}
	else if (diff_populate_filespec(r, one, 0))
		return -1;

	mf->ptr = one->data;
	mf->size = one->size;
	return 0;
}

/* like fill_mmfile, but only for size, so we can avoid retrieving blob */
static unsigned long diff_filespec_size(struct repository *r,
					struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one))
		return 0;
	diff_populate_filespec(r, one, CHECK_SIZE_ONLY);
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

static void emit_line_0(struct diff_options *o,
			const char *set_sign, const char *set, unsigned reverse, const char *reset,
			int first, const char *line, int len)
{
	int has_trailing_newline, has_trailing_carriage_return;
	int needs_reset = 0; /* at the end of the line */
	FILE *file = o->file;

	fputs(diff_line_prefix(o), file);

	has_trailing_newline = (len > 0 && line[len-1] == '\n');
	if (has_trailing_newline)
		len--;

	has_trailing_carriage_return = (len > 0 && line[len-1] == '\r');
	if (has_trailing_carriage_return)
		len--;

	if (!len && !first)
		goto end_of_line;

	if (reverse && want_color(o->use_color)) {
		fputs(GIT_COLOR_REVERSE, file);
		needs_reset = 1;
	}

	if (set_sign) {
		fputs(set_sign, file);
		needs_reset = 1;
	}

	if (first)
		fputc(first, file);

	if (!len)
		goto end_of_line;

	if (set) {
		if (set_sign && set != set_sign)
			fputs(reset, file);
		fputs(set, file);
		needs_reset = 1;
	}
	fwrite(line, len, 1, file);
	needs_reset = 1; /* 'line' may contain color codes. */

end_of_line:
	if (needs_reset)
		fputs(reset, file);
	if (has_trailing_carriage_return)
		fputc('\r', file);
	if (has_trailing_newline)
		fputc('\n', file);
}

static void emit_line(struct diff_options *o, const char *set, const char *reset,
		      const char *line, int len)
{
	emit_line_0(o, set, NULL, 0, reset, 0, line, len);
}

enum diff_symbol {
	DIFF_SYMBOL_BINARY_DIFF_HEADER,
	DIFF_SYMBOL_BINARY_DIFF_HEADER_DELTA,
	DIFF_SYMBOL_BINARY_DIFF_HEADER_LITERAL,
	DIFF_SYMBOL_BINARY_DIFF_BODY,
	DIFF_SYMBOL_BINARY_DIFF_FOOTER,
	DIFF_SYMBOL_STATS_SUMMARY_NO_FILES,
	DIFF_SYMBOL_STATS_SUMMARY_ABBREV,
	DIFF_SYMBOL_STATS_SUMMARY_INSERTS_DELETES,
	DIFF_SYMBOL_STATS_LINE,
	DIFF_SYMBOL_WORD_DIFF,
	DIFF_SYMBOL_STAT_SEP,
	DIFF_SYMBOL_SUMMARY,
	DIFF_SYMBOL_SUBMODULE_ADD,
	DIFF_SYMBOL_SUBMODULE_DEL,
	DIFF_SYMBOL_SUBMODULE_UNTRACKED,
	DIFF_SYMBOL_SUBMODULE_MODIFIED,
	DIFF_SYMBOL_SUBMODULE_HEADER,
	DIFF_SYMBOL_SUBMODULE_ERROR,
	DIFF_SYMBOL_SUBMODULE_PIPETHROUGH,
	DIFF_SYMBOL_REWRITE_DIFF,
	DIFF_SYMBOL_BINARY_FILES,
	DIFF_SYMBOL_HEADER,
	DIFF_SYMBOL_FILEPAIR_PLUS,
	DIFF_SYMBOL_FILEPAIR_MINUS,
	DIFF_SYMBOL_WORDS_PORCELAIN,
	DIFF_SYMBOL_WORDS,
	DIFF_SYMBOL_CONTEXT,
	DIFF_SYMBOL_CONTEXT_INCOMPLETE,
	DIFF_SYMBOL_PLUS,
	DIFF_SYMBOL_MINUS,
	DIFF_SYMBOL_NO_LF_EOF,
	DIFF_SYMBOL_CONTEXT_FRAGINFO,
	DIFF_SYMBOL_CONTEXT_MARKER,
	DIFF_SYMBOL_SEPARATOR
};
/*
 * Flags for content lines:
 * 0..12 are whitespace rules
 * 13-15 are WSEH_NEW | WSEH_OLD | WSEH_CONTEXT
 * 16 is marking if the line is blank at EOF
 */
#define DIFF_SYMBOL_CONTENT_BLANK_LINE_EOF	(1<<16)
#define DIFF_SYMBOL_MOVED_LINE			(1<<17)
#define DIFF_SYMBOL_MOVED_LINE_ALT		(1<<18)
#define DIFF_SYMBOL_MOVED_LINE_UNINTERESTING	(1<<19)
#define DIFF_SYMBOL_CONTENT_WS_MASK (WSEH_NEW | WSEH_OLD | WSEH_CONTEXT | WS_RULE_MASK)

/*
 * This struct is used when we need to buffer the output of the diff output.
 *
 * NEEDSWORK: Instead of storing a copy of the line, add an offset pointer
 * into the pre/post image file. This pointer could be a union with the
 * line pointer. By storing an offset into the file instead of the literal line,
 * we can decrease the memory footprint for the buffered output. At first we
 * may want to only have indirection for the content lines, but we could also
 * enhance the state for emitting prefabricated lines, e.g. the similarity
 * score line or hunk/file headers would only need to store a number or path
 * and then the output can be constructed later on depending on state.
 */
struct emitted_diff_symbol {
	const char *line;
	int len;
	int flags;
	int indent_off;   /* Offset to first non-whitespace character */
	int indent_width; /* The visual width of the indentation */
	enum diff_symbol s;
};
#define EMITTED_DIFF_SYMBOL_INIT {NULL}

struct emitted_diff_symbols {
	struct emitted_diff_symbol *buf;
	int nr, alloc;
};
#define EMITTED_DIFF_SYMBOLS_INIT {NULL, 0, 0}

static void append_emitted_diff_symbol(struct diff_options *o,
				       struct emitted_diff_symbol *e)
{
	struct emitted_diff_symbol *f;

	ALLOC_GROW(o->emitted_symbols->buf,
		   o->emitted_symbols->nr + 1,
		   o->emitted_symbols->alloc);
	f = &o->emitted_symbols->buf[o->emitted_symbols->nr++];

	memcpy(f, e, sizeof(struct emitted_diff_symbol));
	f->line = e->line ? xmemdupz(e->line, e->len) : NULL;
}

struct moved_entry {
	struct hashmap_entry ent;
	const struct emitted_diff_symbol *es;
	struct moved_entry *next_line;
};

struct moved_block {
	struct moved_entry *match;
	int wsd; /* The whitespace delta of this block */
};

static void moved_block_clear(struct moved_block *b)
{
	memset(b, 0, sizeof(*b));
}

#define INDENT_BLANKLINE INT_MIN

static void fill_es_indent_data(struct emitted_diff_symbol *es)
{
	unsigned int off = 0, i;
	int width = 0, tab_width = es->flags & WS_TAB_WIDTH_MASK;
	const char *s = es->line;
	const int len = es->len;

	/* skip any \v \f \r at start of indentation */
	while (s[off] == '\f' || s[off] == '\v' ||
	       (s[off] == '\r' && off < len - 1))
		off++;

	/* calculate the visual width of indentation */
	while(1) {
		if (s[off] == ' ') {
			width++;
			off++;
		} else if (s[off] == '\t') {
			width += tab_width - (width % tab_width);
			while (s[++off] == '\t')
				width += tab_width;
		} else {
			break;
		}
	}

	/* check if this line is blank */
	for (i = off; i < len; i++)
		if (!isspace(s[i]))
		    break;

	if (i == len) {
		es->indent_width = INDENT_BLANKLINE;
		es->indent_off = len;
	} else {
		es->indent_off = off;
		es->indent_width = width;
	}
}

static int compute_ws_delta(const struct emitted_diff_symbol *a,
			    const struct emitted_diff_symbol *b,
			    int *out)
{
	int a_len = a->len,
	    b_len = b->len,
	    a_off = a->indent_off,
	    a_width = a->indent_width,
	    b_off = b->indent_off,
	    b_width = b->indent_width;
	int delta;

	if (a_width == INDENT_BLANKLINE && b_width == INDENT_BLANKLINE) {
		*out = INDENT_BLANKLINE;
		return 1;
	}

	if (a->s == DIFF_SYMBOL_PLUS)
		delta = a_width - b_width;
	else
		delta = b_width - a_width;

	if (a_len - a_off != b_len - b_off ||
	    memcmp(a->line + a_off, b->line + b_off, a_len - a_off))
		return 0;

	*out = delta;

	return 1;
}

static int cmp_in_block_with_wsd(const struct diff_options *o,
				 const struct moved_entry *cur,
				 const struct moved_entry *match,
				 struct moved_block *pmb,
				 int n)
{
	struct emitted_diff_symbol *l = &o->emitted_symbols->buf[n];
	int al = cur->es->len, bl = match->es->len, cl = l->len;
	const char *a = cur->es->line,
		   *b = match->es->line,
		   *c = l->line;
	int a_off = cur->es->indent_off,
	    a_width = cur->es->indent_width,
	    c_off = l->indent_off,
	    c_width = l->indent_width;
	int delta;

	/*
	 * We need to check if 'cur' is equal to 'match'.  As those
	 * are from the same (+/-) side, we do not need to adjust for
	 * indent changes. However these were found using fuzzy
	 * matching so we do have to check if they are equal. Here we
	 * just check the lengths. We delay calling memcmp() to check
	 * the contents until later as if the length comparison for a
	 * and c fails we can avoid the call all together.
	 */
	if (al != bl)
		return 1;

	/* If 'l' and 'cur' are both blank then they match. */
	if (a_width == INDENT_BLANKLINE && c_width == INDENT_BLANKLINE)
		return 0;

	/*
	 * The indent changes of the block are known and stored in pmb->wsd;
	 * however we need to check if the indent changes of the current line
	 * match those of the current block and that the text of 'l' and 'cur'
	 * after the indentation match.
	 */
	if (cur->es->s == DIFF_SYMBOL_PLUS)
		delta = a_width - c_width;
	else
		delta = c_width - a_width;

	/*
	 * If the previous lines of this block were all blank then set its
	 * whitespace delta.
	 */
	if (pmb->wsd == INDENT_BLANKLINE)
		pmb->wsd = delta;

	return !(delta == pmb->wsd && al - a_off == cl - c_off &&
		 !memcmp(a, b, al) && !
		 memcmp(a + a_off, c + c_off, al - a_off));
}

static int moved_entry_cmp(const void *hashmap_cmp_fn_data,
			   const struct hashmap_entry *eptr,
			   const struct hashmap_entry *entry_or_key,
			   const void *keydata)
{
	const struct diff_options *diffopt = hashmap_cmp_fn_data;
	const struct moved_entry *a, *b;
	unsigned flags = diffopt->color_moved_ws_handling
			 & XDF_WHITESPACE_FLAGS;

	a = container_of(eptr, const struct moved_entry, ent);
	b = container_of(entry_or_key, const struct moved_entry, ent);

	if (diffopt->color_moved_ws_handling &
	    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE)
		/*
		 * As there is not specific white space config given,
		 * we'd need to check for a new block, so ignore all
		 * white space. The setup of the white space
		 * configuration for the next block is done else where
		 */
		flags |= XDF_IGNORE_WHITESPACE;

	return !xdiff_compare_lines(a->es->line, a->es->len,
				    b->es->line, b->es->len,
				    flags);
}

static struct moved_entry *prepare_entry(struct diff_options *o,
					 int line_no)
{
	struct moved_entry *ret = xmalloc(sizeof(*ret));
	struct emitted_diff_symbol *l = &o->emitted_symbols->buf[line_no];
	unsigned flags = o->color_moved_ws_handling & XDF_WHITESPACE_FLAGS;
	unsigned int hash = xdiff_hash_string(l->line, l->len, flags);

	hashmap_entry_init(&ret->ent, hash);
	ret->es = l;
	ret->next_line = NULL;

	return ret;
}

static void add_lines_to_move_detection(struct diff_options *o,
					struct hashmap *add_lines,
					struct hashmap *del_lines)
{
	struct moved_entry *prev_line = NULL;

	int n;
	for (n = 0; n < o->emitted_symbols->nr; n++) {
		struct hashmap *hm;
		struct moved_entry *key;

		switch (o->emitted_symbols->buf[n].s) {
		case DIFF_SYMBOL_PLUS:
			hm = add_lines;
			break;
		case DIFF_SYMBOL_MINUS:
			hm = del_lines;
			break;
		default:
			prev_line = NULL;
			continue;
		}

		if (o->color_moved_ws_handling &
		    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE)
			fill_es_indent_data(&o->emitted_symbols->buf[n]);
		key = prepare_entry(o, n);
		if (prev_line && prev_line->es->s == o->emitted_symbols->buf[n].s)
			prev_line->next_line = key;

		hashmap_add(hm, &key->ent);
		prev_line = key;
	}
}

static void pmb_advance_or_null(struct diff_options *o,
				struct moved_entry *match,
				struct hashmap *hm,
				struct moved_block *pmb,
				int pmb_nr)
{
	int i;
	for (i = 0; i < pmb_nr; i++) {
		struct moved_entry *prev = pmb[i].match;
		struct moved_entry *cur = (prev && prev->next_line) ?
				prev->next_line : NULL;
		if (cur && !hm->cmpfn(o, &cur->ent, &match->ent, NULL)) {
			pmb[i].match = cur;
		} else {
			pmb[i].match = NULL;
		}
	}
}

static void pmb_advance_or_null_multi_match(struct diff_options *o,
					    struct moved_entry *match,
					    struct hashmap *hm,
					    struct moved_block *pmb,
					    int pmb_nr, int n)
{
	int i;
	char *got_match = xcalloc(1, pmb_nr);

	hashmap_for_each_entry_from(hm, match, ent) {
		for (i = 0; i < pmb_nr; i++) {
			struct moved_entry *prev = pmb[i].match;
			struct moved_entry *cur = (prev && prev->next_line) ?
					prev->next_line : NULL;
			if (!cur)
				continue;
			if (!cmp_in_block_with_wsd(o, cur, match, &pmb[i], n))
				got_match[i] |= 1;
		}
	}

	for (i = 0; i < pmb_nr; i++) {
		if (got_match[i]) {
			/* Advance to the next line */
			pmb[i].match = pmb[i].match->next_line;
		} else {
			moved_block_clear(&pmb[i]);
		}
	}

	free(got_match);
}

static int shrink_potential_moved_blocks(struct moved_block *pmb,
					 int pmb_nr)
{
	int lp, rp;

	/* Shrink the set of potential block to the remaining running */
	for (lp = 0, rp = pmb_nr - 1; lp <= rp;) {
		while (lp < pmb_nr && pmb[lp].match)
			lp++;
		/* lp points at the first NULL now */

		while (rp > -1 && !pmb[rp].match)
			rp--;
		/* rp points at the last non-NULL */

		if (lp < pmb_nr && rp > -1 && lp < rp) {
			pmb[lp] = pmb[rp];
			memset(&pmb[rp], 0, sizeof(pmb[rp]));
			rp--;
			lp++;
		}
	}

	/* Remember the number of running sets */
	return rp + 1;
}

/*
 * If o->color_moved is COLOR_MOVED_PLAIN, this function does nothing.
 *
 * Otherwise, if the last block has fewer alphanumeric characters than
 * COLOR_MOVED_MIN_ALNUM_COUNT, unset DIFF_SYMBOL_MOVED_LINE on all lines in
 * that block.
 *
 * The last block consists of the (n - block_length)'th line up to but not
 * including the nth line.
 *
 * Returns 0 if the last block is empty or is unset by this function, non zero
 * otherwise.
 *
 * NEEDSWORK: This uses the same heuristic as blame_entry_score() in blame.c.
 * Think of a way to unify them.
 */
static int adjust_last_block(struct diff_options *o, int n, int block_length)
{
	int i, alnum_count = 0;
	if (o->color_moved == COLOR_MOVED_PLAIN)
		return block_length;
	for (i = 1; i < block_length + 1; i++) {
		const char *c = o->emitted_symbols->buf[n - i].line;
		for (; *c; c++) {
			if (!isalnum(*c))
				continue;
			alnum_count++;
			if (alnum_count >= COLOR_MOVED_MIN_ALNUM_COUNT)
				return 1;
		}
	}
	for (i = 1; i < block_length + 1; i++)
		o->emitted_symbols->buf[n - i].flags &= ~DIFF_SYMBOL_MOVED_LINE;
	return 0;
}

/* Find blocks of moved code, delegate actual coloring decision to helper */
static void mark_color_as_moved(struct diff_options *o,
				struct hashmap *add_lines,
				struct hashmap *del_lines)
{
	struct moved_block *pmb = NULL; /* potentially moved blocks */
	int pmb_nr = 0, pmb_alloc = 0;
	int n, flipped_block = 0, block_length = 0;


	for (n = 0; n < o->emitted_symbols->nr; n++) {
		struct hashmap *hm = NULL;
		struct moved_entry *key;
		struct moved_entry *match = NULL;
		struct emitted_diff_symbol *l = &o->emitted_symbols->buf[n];
		enum diff_symbol last_symbol = 0;

		switch (l->s) {
		case DIFF_SYMBOL_PLUS:
			hm = del_lines;
			key = prepare_entry(o, n);
			match = hashmap_get_entry(hm, key, ent, NULL);
			free(key);
			break;
		case DIFF_SYMBOL_MINUS:
			hm = add_lines;
			key = prepare_entry(o, n);
			match = hashmap_get_entry(hm, key, ent, NULL);
			free(key);
			break;
		default:
			flipped_block = 0;
		}

		if (!match) {
			int i;

			adjust_last_block(o, n, block_length);
			for(i = 0; i < pmb_nr; i++)
				moved_block_clear(&pmb[i]);
			pmb_nr = 0;
			block_length = 0;
			flipped_block = 0;
			last_symbol = l->s;
			continue;
		}

		if (o->color_moved == COLOR_MOVED_PLAIN) {
			last_symbol = l->s;
			l->flags |= DIFF_SYMBOL_MOVED_LINE;
			continue;
		}

		if (o->color_moved_ws_handling &
		    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE)
			pmb_advance_or_null_multi_match(o, match, hm, pmb, pmb_nr, n);
		else
			pmb_advance_or_null(o, match, hm, pmb, pmb_nr);

		pmb_nr = shrink_potential_moved_blocks(pmb, pmb_nr);

		if (pmb_nr == 0) {
			/*
			 * The current line is the start of a new block.
			 * Setup the set of potential blocks.
			 */
			hashmap_for_each_entry_from(hm, match, ent) {
				ALLOC_GROW(pmb, pmb_nr + 1, pmb_alloc);
				if (o->color_moved_ws_handling &
				    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE) {
					if (compute_ws_delta(l, match->es,
							     &pmb[pmb_nr].wsd))
						pmb[pmb_nr++].match = match;
				} else {
					pmb[pmb_nr].wsd = 0;
					pmb[pmb_nr++].match = match;
				}
			}

			if (adjust_last_block(o, n, block_length) &&
			    pmb_nr && last_symbol != l->s)
				flipped_block = (flipped_block + 1) % 2;
			else
				flipped_block = 0;

			block_length = 0;
		}

		if (pmb_nr) {
			block_length++;
			l->flags |= DIFF_SYMBOL_MOVED_LINE;
			if (flipped_block && o->color_moved != COLOR_MOVED_BLOCKS)
				l->flags |= DIFF_SYMBOL_MOVED_LINE_ALT;
		}
		last_symbol = l->s;
	}
	adjust_last_block(o, n, block_length);

	for(n = 0; n < pmb_nr; n++)
		moved_block_clear(&pmb[n]);
	free(pmb);
}

#define DIFF_SYMBOL_MOVED_LINE_ZEBRA_MASK \
  (DIFF_SYMBOL_MOVED_LINE | DIFF_SYMBOL_MOVED_LINE_ALT)
static void dim_moved_lines(struct diff_options *o)
{
	int n;
	for (n = 0; n < o->emitted_symbols->nr; n++) {
		struct emitted_diff_symbol *prev = (n != 0) ?
				&o->emitted_symbols->buf[n - 1] : NULL;
		struct emitted_diff_symbol *l = &o->emitted_symbols->buf[n];
		struct emitted_diff_symbol *next =
				(n < o->emitted_symbols->nr - 1) ?
				&o->emitted_symbols->buf[n + 1] : NULL;

		/* Not a plus or minus line? */
		if (l->s != DIFF_SYMBOL_PLUS && l->s != DIFF_SYMBOL_MINUS)
			continue;

		/* Not a moved line? */
		if (!(l->flags & DIFF_SYMBOL_MOVED_LINE))
			continue;

		/*
		 * If prev or next are not a plus or minus line,
		 * pretend they don't exist
		 */
		if (prev && prev->s != DIFF_SYMBOL_PLUS &&
			    prev->s != DIFF_SYMBOL_MINUS)
			prev = NULL;
		if (next && next->s != DIFF_SYMBOL_PLUS &&
			    next->s != DIFF_SYMBOL_MINUS)
			next = NULL;

		/* Inside a block? */
		if ((prev &&
		    (prev->flags & DIFF_SYMBOL_MOVED_LINE_ZEBRA_MASK) ==
		    (l->flags & DIFF_SYMBOL_MOVED_LINE_ZEBRA_MASK)) &&
		    (next &&
		    (next->flags & DIFF_SYMBOL_MOVED_LINE_ZEBRA_MASK) ==
		    (l->flags & DIFF_SYMBOL_MOVED_LINE_ZEBRA_MASK))) {
			l->flags |= DIFF_SYMBOL_MOVED_LINE_UNINTERESTING;
			continue;
		}

		/* Check if we are at an interesting bound: */
		if (prev && (prev->flags & DIFF_SYMBOL_MOVED_LINE) &&
		    (prev->flags & DIFF_SYMBOL_MOVED_LINE_ALT) !=
		       (l->flags & DIFF_SYMBOL_MOVED_LINE_ALT))
			continue;
		if (next && (next->flags & DIFF_SYMBOL_MOVED_LINE) &&
		    (next->flags & DIFF_SYMBOL_MOVED_LINE_ALT) !=
		       (l->flags & DIFF_SYMBOL_MOVED_LINE_ALT))
			continue;

		/*
		 * The boundary to prev and next are not interesting,
		 * so this line is not interesting as a whole
		 */
		l->flags |= DIFF_SYMBOL_MOVED_LINE_UNINTERESTING;
	}
}

static void emit_line_ws_markup(struct diff_options *o,
				const char *set_sign, const char *set,
				const char *reset,
				int sign_index, const char *line, int len,
				unsigned ws_rule, int blank_at_eof)
{
	const char *ws = NULL;
	int sign = o->output_indicators[sign_index];

	if (o->ws_error_highlight & ws_rule) {
		ws = diff_get_color_opt(o, DIFF_WHITESPACE);
		if (!*ws)
			ws = NULL;
	}

	if (!ws && !set_sign)
		emit_line_0(o, set, NULL, 0, reset, sign, line, len);
	else if (!ws) {
		emit_line_0(o, set_sign, set, !!set_sign, reset, sign, line, len);
	} else if (blank_at_eof)
		/* Blank line at EOF - paint '+' as well */
		emit_line_0(o, ws, NULL, 0, reset, sign, line, len);
	else {
		/* Emit just the prefix, then the rest. */
		emit_line_0(o, set_sign ? set_sign : set, NULL, !!set_sign, reset,
			    sign, "", 0);
		ws_check_emit(line, len, ws_rule,
			      o->file, set, reset, ws);
	}
}

static void emit_diff_symbol_from_struct(struct diff_options *o,
					 struct emitted_diff_symbol *eds)
{
	static const char *nneof = " No newline at end of file\n";
	const char *context, *reset, *set, *set_sign, *meta, *fraginfo;
	struct strbuf sb = STRBUF_INIT;

	enum diff_symbol s = eds->s;
	const char *line = eds->line;
	int len = eds->len;
	unsigned flags = eds->flags;

	switch (s) {
	case DIFF_SYMBOL_NO_LF_EOF:
		context = diff_get_color_opt(o, DIFF_CONTEXT);
		reset = diff_get_color_opt(o, DIFF_RESET);
		putc('\n', o->file);
		emit_line_0(o, context, NULL, 0, reset, '\\',
			    nneof, strlen(nneof));
		break;
	case DIFF_SYMBOL_SUBMODULE_HEADER:
	case DIFF_SYMBOL_SUBMODULE_ERROR:
	case DIFF_SYMBOL_SUBMODULE_PIPETHROUGH:
	case DIFF_SYMBOL_STATS_SUMMARY_INSERTS_DELETES:
	case DIFF_SYMBOL_SUMMARY:
	case DIFF_SYMBOL_STATS_LINE:
	case DIFF_SYMBOL_BINARY_DIFF_BODY:
	case DIFF_SYMBOL_CONTEXT_FRAGINFO:
		emit_line(o, "", "", line, len);
		break;
	case DIFF_SYMBOL_CONTEXT_INCOMPLETE:
	case DIFF_SYMBOL_CONTEXT_MARKER:
		context = diff_get_color_opt(o, DIFF_CONTEXT);
		reset = diff_get_color_opt(o, DIFF_RESET);
		emit_line(o, context, reset, line, len);
		break;
	case DIFF_SYMBOL_SEPARATOR:
		fprintf(o->file, "%s%c",
			diff_line_prefix(o),
			o->line_termination);
		break;
	case DIFF_SYMBOL_CONTEXT:
		set = diff_get_color_opt(o, DIFF_CONTEXT);
		reset = diff_get_color_opt(o, DIFF_RESET);
		set_sign = NULL;
		if (o->flags.dual_color_diffed_diffs) {
			char c = !len ? 0 : line[0];

			if (c == '+')
				set = diff_get_color_opt(o, DIFF_FILE_NEW);
			else if (c == '@')
				set = diff_get_color_opt(o, DIFF_FRAGINFO);
			else if (c == '-')
				set = diff_get_color_opt(o, DIFF_FILE_OLD);
		}
		emit_line_ws_markup(o, set_sign, set, reset,
				    OUTPUT_INDICATOR_CONTEXT, line, len,
				    flags & (DIFF_SYMBOL_CONTENT_WS_MASK), 0);
		break;
	case DIFF_SYMBOL_PLUS:
		switch (flags & (DIFF_SYMBOL_MOVED_LINE |
				 DIFF_SYMBOL_MOVED_LINE_ALT |
				 DIFF_SYMBOL_MOVED_LINE_UNINTERESTING)) {
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_ALT |
		     DIFF_SYMBOL_MOVED_LINE_UNINTERESTING:
			set = diff_get_color_opt(o, DIFF_FILE_NEW_MOVED_ALT_DIM);
			break;
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_ALT:
			set = diff_get_color_opt(o, DIFF_FILE_NEW_MOVED_ALT);
			break;
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_UNINTERESTING:
			set = diff_get_color_opt(o, DIFF_FILE_NEW_MOVED_DIM);
			break;
		case DIFF_SYMBOL_MOVED_LINE:
			set = diff_get_color_opt(o, DIFF_FILE_NEW_MOVED);
			break;
		default:
			set = diff_get_color_opt(o, DIFF_FILE_NEW);
		}
		reset = diff_get_color_opt(o, DIFF_RESET);
		if (!o->flags.dual_color_diffed_diffs)
			set_sign = NULL;
		else {
			char c = !len ? 0 : line[0];

			set_sign = set;
			if (c == '-')
				set = diff_get_color_opt(o, DIFF_FILE_OLD_BOLD);
			else if (c == '@')
				set = diff_get_color_opt(o, DIFF_FRAGINFO);
			else if (c == '+')
				set = diff_get_color_opt(o, DIFF_FILE_NEW_BOLD);
			else
				set = diff_get_color_opt(o, DIFF_CONTEXT_BOLD);
			flags &= ~DIFF_SYMBOL_CONTENT_WS_MASK;
		}
		emit_line_ws_markup(o, set_sign, set, reset,
				    OUTPUT_INDICATOR_NEW, line, len,
				    flags & DIFF_SYMBOL_CONTENT_WS_MASK,
				    flags & DIFF_SYMBOL_CONTENT_BLANK_LINE_EOF);
		break;
	case DIFF_SYMBOL_MINUS:
		switch (flags & (DIFF_SYMBOL_MOVED_LINE |
				 DIFF_SYMBOL_MOVED_LINE_ALT |
				 DIFF_SYMBOL_MOVED_LINE_UNINTERESTING)) {
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_ALT |
		     DIFF_SYMBOL_MOVED_LINE_UNINTERESTING:
			set = diff_get_color_opt(o, DIFF_FILE_OLD_MOVED_ALT_DIM);
			break;
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_ALT:
			set = diff_get_color_opt(o, DIFF_FILE_OLD_MOVED_ALT);
			break;
		case DIFF_SYMBOL_MOVED_LINE |
		     DIFF_SYMBOL_MOVED_LINE_UNINTERESTING:
			set = diff_get_color_opt(o, DIFF_FILE_OLD_MOVED_DIM);
			break;
		case DIFF_SYMBOL_MOVED_LINE:
			set = diff_get_color_opt(o, DIFF_FILE_OLD_MOVED);
			break;
		default:
			set = diff_get_color_opt(o, DIFF_FILE_OLD);
		}
		reset = diff_get_color_opt(o, DIFF_RESET);
		if (!o->flags.dual_color_diffed_diffs)
			set_sign = NULL;
		else {
			char c = !len ? 0 : line[0];

			set_sign = set;
			if (c == '+')
				set = diff_get_color_opt(o, DIFF_FILE_NEW_DIM);
			else if (c == '@')
				set = diff_get_color_opt(o, DIFF_FRAGINFO);
			else if (c == '-')
				set = diff_get_color_opt(o, DIFF_FILE_OLD_DIM);
			else
				set = diff_get_color_opt(o, DIFF_CONTEXT_DIM);
		}
		emit_line_ws_markup(o, set_sign, set, reset,
				    OUTPUT_INDICATOR_OLD, line, len,
				    flags & DIFF_SYMBOL_CONTENT_WS_MASK, 0);
		break;
	case DIFF_SYMBOL_WORDS_PORCELAIN:
		context = diff_get_color_opt(o, DIFF_CONTEXT);
		reset = diff_get_color_opt(o, DIFF_RESET);
		emit_line(o, context, reset, line, len);
		fputs("~\n", o->file);
		break;
	case DIFF_SYMBOL_WORDS:
		context = diff_get_color_opt(o, DIFF_CONTEXT);
		reset = diff_get_color_opt(o, DIFF_RESET);
		/*
		 * Skip the prefix character, if any.  With
		 * diff_suppress_blank_empty, there may be
		 * none.
		 */
		if (line[0] != '\n') {
			line++;
			len--;
		}
		emit_line(o, context, reset, line, len);
		break;
	case DIFF_SYMBOL_FILEPAIR_PLUS:
		meta = diff_get_color_opt(o, DIFF_METAINFO);
		reset = diff_get_color_opt(o, DIFF_RESET);
		fprintf(o->file, "%s%s+++ %s%s%s\n", diff_line_prefix(o), meta,
			line, reset,
			strchr(line, ' ') ? "\t" : "");
		break;
	case DIFF_SYMBOL_FILEPAIR_MINUS:
		meta = diff_get_color_opt(o, DIFF_METAINFO);
		reset = diff_get_color_opt(o, DIFF_RESET);
		fprintf(o->file, "%s%s--- %s%s%s\n", diff_line_prefix(o), meta,
			line, reset,
			strchr(line, ' ') ? "\t" : "");
		break;
	case DIFF_SYMBOL_BINARY_FILES:
	case DIFF_SYMBOL_HEADER:
		fprintf(o->file, "%s", line);
		break;
	case DIFF_SYMBOL_BINARY_DIFF_HEADER:
		fprintf(o->file, "%sGIT binary patch\n", diff_line_prefix(o));
		break;
	case DIFF_SYMBOL_BINARY_DIFF_HEADER_DELTA:
		fprintf(o->file, "%sdelta %s\n", diff_line_prefix(o), line);
		break;
	case DIFF_SYMBOL_BINARY_DIFF_HEADER_LITERAL:
		fprintf(o->file, "%sliteral %s\n", diff_line_prefix(o), line);
		break;
	case DIFF_SYMBOL_BINARY_DIFF_FOOTER:
		fputs(diff_line_prefix(o), o->file);
		fputc('\n', o->file);
		break;
	case DIFF_SYMBOL_REWRITE_DIFF:
		fraginfo = diff_get_color(o->use_color, DIFF_FRAGINFO);
		reset = diff_get_color_opt(o, DIFF_RESET);
		emit_line(o, fraginfo, reset, line, len);
		break;
	case DIFF_SYMBOL_SUBMODULE_ADD:
		set = diff_get_color_opt(o, DIFF_FILE_NEW);
		reset = diff_get_color_opt(o, DIFF_RESET);
		emit_line(o, set, reset, line, len);
		break;
	case DIFF_SYMBOL_SUBMODULE_DEL:
		set = diff_get_color_opt(o, DIFF_FILE_OLD);
		reset = diff_get_color_opt(o, DIFF_RESET);
		emit_line(o, set, reset, line, len);
		break;
	case DIFF_SYMBOL_SUBMODULE_UNTRACKED:
		fprintf(o->file, "%sSubmodule %s contains untracked content\n",
			diff_line_prefix(o), line);
		break;
	case DIFF_SYMBOL_SUBMODULE_MODIFIED:
		fprintf(o->file, "%sSubmodule %s contains modified content\n",
			diff_line_prefix(o), line);
		break;
	case DIFF_SYMBOL_STATS_SUMMARY_NO_FILES:
		emit_line(o, "", "", " 0 files changed\n",
			  strlen(" 0 files changed\n"));
		break;
	case DIFF_SYMBOL_STATS_SUMMARY_ABBREV:
		emit_line(o, "", "", " ...\n", strlen(" ...\n"));
		break;
	case DIFF_SYMBOL_WORD_DIFF:
		fprintf(o->file, "%.*s", len, line);
		break;
	case DIFF_SYMBOL_STAT_SEP:
		fputs(o->stat_sep, o->file);
		break;
	default:
		BUG("unknown diff symbol");
	}
	strbuf_release(&sb);
}

static void emit_diff_symbol(struct diff_options *o, enum diff_symbol s,
			     const char *line, int len, unsigned flags)
{
	struct emitted_diff_symbol e = {line, len, flags, 0, 0, s};

	if (o->emitted_symbols)
		append_emitted_diff_symbol(o, &e);
	else
		emit_diff_symbol_from_struct(o, &e);
}

void diff_emit_submodule_del(struct diff_options *o, const char *line)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_DEL, line, strlen(line), 0);
}

void diff_emit_submodule_add(struct diff_options *o, const char *line)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_ADD, line, strlen(line), 0);
}

void diff_emit_submodule_untracked(struct diff_options *o, const char *path)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_UNTRACKED,
			 path, strlen(path), 0);
}

void diff_emit_submodule_modified(struct diff_options *o, const char *path)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_MODIFIED,
			 path, strlen(path), 0);
}

void diff_emit_submodule_header(struct diff_options *o, const char *header)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_HEADER,
			 header, strlen(header), 0);
}

void diff_emit_submodule_error(struct diff_options *o, const char *err)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_ERROR, err, strlen(err), 0);
}

void diff_emit_submodule_pipethrough(struct diff_options *o,
				     const char *line, int len)
{
	emit_diff_symbol(o, DIFF_SYMBOL_SUBMODULE_PIPETHROUGH, line, len, 0);
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

static void emit_add_line(struct emit_callback *ecbdata,
			  const char *line, int len)
{
	unsigned flags = WSEH_NEW | ecbdata->ws_rule;
	if (new_blank_line_at_eof(ecbdata, line, len))
		flags |= DIFF_SYMBOL_CONTENT_BLANK_LINE_EOF;

	emit_diff_symbol(ecbdata->opt, DIFF_SYMBOL_PLUS, line, len, flags);
}

static void emit_del_line(struct emit_callback *ecbdata,
			  const char *line, int len)
{
	unsigned flags = WSEH_OLD | ecbdata->ws_rule;
	emit_diff_symbol(ecbdata->opt, DIFF_SYMBOL_MINUS, line, len, flags);
}

static void emit_context_line(struct emit_callback *ecbdata,
			      const char *line, int len)
{
	unsigned flags = WSEH_CONTEXT | ecbdata->ws_rule;
	emit_diff_symbol(ecbdata->opt, DIFF_SYMBOL_CONTEXT, line, len, flags);
}

static void emit_hunk_header(struct emit_callback *ecbdata,
			     const char *line, int len)
{
	const char *context = diff_get_color(ecbdata->color_diff, DIFF_CONTEXT);
	const char *frag = diff_get_color(ecbdata->color_diff, DIFF_FRAGINFO);
	const char *func = diff_get_color(ecbdata->color_diff, DIFF_FUNCINFO);
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);
	const char *reverse = ecbdata->color_diff ? GIT_COLOR_REVERSE : "";
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
		emit_diff_symbol(ecbdata->opt,
				 DIFF_SYMBOL_CONTEXT_MARKER, line, len, 0);
		return;
	}
	ep += 2; /* skip over @@ */

	/* The hunk header in fraginfo color */
	if (ecbdata->opt->flags.dual_color_diffed_diffs)
		strbuf_addstr(&msgbuf, reverse);
	strbuf_addstr(&msgbuf, frag);
	if (ecbdata->opt->flags.suppress_hunk_header_line_count)
		strbuf_add(&msgbuf, atat, sizeof(atat));
	else
		strbuf_add(&msgbuf, line, ep - line);
	strbuf_addstr(&msgbuf, reset);

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
		strbuf_addstr(&msgbuf, context);
		strbuf_add(&msgbuf, cp, ep - cp);
		strbuf_addstr(&msgbuf, reset);
	}

	if (ep < line + len) {
		strbuf_addstr(&msgbuf, func);
		strbuf_add(&msgbuf, ep, line + len - ep);
		strbuf_addstr(&msgbuf, reset);
	}

	strbuf_add(&msgbuf, line + len, org_len - len);
	strbuf_complete_line(&msgbuf);
	emit_diff_symbol(ecbdata->opt,
			 DIFF_SYMBOL_CONTEXT_FRAGINFO, msgbuf.buf, msgbuf.len, 0);
	strbuf_release(&msgbuf);
}

static struct diff_tempfile *claim_diff_tempfile(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(diff_temp); i++)
		if (!diff_temp[i].name)
			return diff_temp + i;
	BUG("diff is failing to clean up its tempfiles");
}

static void remove_tempfile(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(diff_temp); i++) {
		if (is_tempfile_active(diff_temp[i].tempfile))
			delete_tempfile(&diff_temp[i].tempfile);
		diff_temp[i].name = NULL;
	}
}

static void add_line_count(struct strbuf *out, int count)
{
	switch (count) {
	case 0:
		strbuf_addstr(out, "0,0");
		break;
	case 1:
		strbuf_addstr(out, "1");
		break;
	default:
		strbuf_addf(out, "1,%d", count);
		break;
	}
}

static void emit_rewrite_lines(struct emit_callback *ecb,
			       int prefix, const char *data, int size)
{
	const char *endp = NULL;

	while (0 < size) {
		int len;

		endp = memchr(data, '\n', size);
		len = endp ? (endp - data + 1) : size;
		if (prefix != '+') {
			ecb->lno_in_preimage++;
			emit_del_line(ecb, data, len);
		} else {
			ecb->lno_in_postimage++;
			emit_add_line(ecb, data, len);
		}
		size -= len;
		data += len;
	}
	if (!endp)
		emit_diff_symbol(ecb->opt, DIFF_SYMBOL_NO_LF_EOF, NULL, 0, 0);
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
	static struct strbuf a_name = STRBUF_INIT, b_name = STRBUF_INIT;
	const char *a_prefix, *b_prefix;
	char *data_one, *data_two;
	size_t size_one, size_two;
	struct emit_callback ecbdata;
	struct strbuf out = STRBUF_INIT;

	if (diff_mnemonic_prefix && o->flags.reverse_diff) {
		a_prefix = o->b_prefix;
		b_prefix = o->a_prefix;
	} else {
		a_prefix = o->a_prefix;
		b_prefix = o->b_prefix;
	}

	name_a += (*name_a == '/');
	name_b += (*name_b == '/');

	strbuf_reset(&a_name);
	strbuf_reset(&b_name);
	quote_two_c_style(&a_name, a_prefix, name_a, 0);
	quote_two_c_style(&b_name, b_prefix, name_b, 0);

	size_one = fill_textconv(o->repo, textconv_one, one, &data_one);
	size_two = fill_textconv(o->repo, textconv_two, two, &data_two);

	memset(&ecbdata, 0, sizeof(ecbdata));
	ecbdata.color_diff = want_color(o->use_color);
	ecbdata.ws_rule = whitespace_rule(o->repo->index, name_b);
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

	emit_diff_symbol(o, DIFF_SYMBOL_FILEPAIR_MINUS,
			 a_name.buf, a_name.len, 0);
	emit_diff_symbol(o, DIFF_SYMBOL_FILEPAIR_PLUS,
			 b_name.buf, b_name.len, 0);

	strbuf_addstr(&out, "@@ -");
	if (!o->irreversible_delete)
		add_line_count(&out, lc_a);
	else
		strbuf_addstr(&out, "?,?");
	strbuf_addstr(&out, " +");
	add_line_count(&out, lc_b);
	strbuf_addstr(&out, " @@\n");
	emit_diff_symbol(o, DIFF_SYMBOL_REWRITE_DIFF, out.buf, out.len, 0);
	strbuf_release(&out);

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
	unsigned long alloc;
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
	struct diff_words_style_elem new_word, old_word, ctx;
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

static int fn_out_diff_words_write_helper(struct diff_options *o,
					  struct diff_words_style_elem *st_el,
					  const char *newline,
					  size_t count, const char *buf)
{
	int print = 0;
	struct strbuf sb = STRBUF_INIT;

	while (count) {
		char *p = memchr(buf, '\n', count);
		if (print)
			strbuf_addstr(&sb, diff_line_prefix(o));

		if (p != buf) {
			const char *reset = st_el->color && *st_el->color ?
					    GIT_COLOR_RESET : NULL;
			if (st_el->color && *st_el->color)
				strbuf_addstr(&sb, st_el->color);
			strbuf_addstr(&sb, st_el->prefix);
			strbuf_add(&sb, buf, p ? p - buf : count);
			strbuf_addstr(&sb, st_el->suffix);
			if (reset)
				strbuf_addstr(&sb, reset);
		}
		if (!p)
			goto out;

		strbuf_addstr(&sb, newline);
		count -= p + 1 - buf;
		buf = p + 1;
		print = 1;
		if (count) {
			emit_diff_symbol(o, DIFF_SYMBOL_WORD_DIFF,
					 sb.buf, sb.len, 0);
			strbuf_reset(&sb);
		}
	}

out:
	if (sb.len)
		emit_diff_symbol(o, DIFF_SYMBOL_WORD_DIFF,
				 sb.buf, sb.len, 0);
	strbuf_release(&sb);
	return 0;
}

/*
 * '--color-words' algorithm can be described as:
 *
 *   1. collect the minus/plus lines of a diff hunk, divided into
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

static void fn_out_diff_words_aux(void *priv,
				  long minus_first, long minus_len,
				  long plus_first, long plus_len,
				  const char *func, long funclen)
{
	struct diff_words_data *diff_words = priv;
	struct diff_words_style *style = diff_words->style;
	const char *minus_begin, *minus_end, *plus_begin, *plus_end;
	struct diff_options *opt = diff_words->opt;
	const char *line_prefix;

	assert(opt);
	line_prefix = diff_line_prefix(opt);

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
		fn_out_diff_words_write_helper(diff_words->opt,
				&style->ctx, style->newline,
				plus_begin - diff_words->current_plus,
				diff_words->current_plus);
	}
	if (minus_begin != minus_end) {
		fn_out_diff_words_write_helper(diff_words->opt,
				&style->old_word, style->newline,
				minus_end - minus_begin, minus_begin);
	}
	if (plus_begin != plus_end) {
		fn_out_diff_words_write_helper(diff_words->opt,
				&style->new_word, style->newline,
				plus_end - plus_begin, plus_begin);
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
		if (!regexec_buf(word_regex, buffer->ptr + *begin,
				 buffer->size - *begin, 1, match, 0)) {
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
	const char *line_prefix;

	assert(opt);
	line_prefix = diff_line_prefix(opt);

	/* special case: only removal */
	if (!diff_words->plus.text.size) {
		emit_diff_symbol(diff_words->opt, DIFF_SYMBOL_WORD_DIFF,
				 line_prefix, strlen(line_prefix), 0);
		fn_out_diff_words_write_helper(diff_words->opt,
			&style->old_word, style->newline,
			diff_words->minus.text.size,
			diff_words->minus.text.ptr);
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
	if (xdi_diff_outf(&minus, &plus, fn_out_diff_words_aux, NULL,
			  diff_words, &xpp, &xecfg))
		die("unable to generate word diff");
	free(minus.ptr);
	free(plus.ptr);
	if (diff_words->current_plus != diff_words->plus.text.ptr +
			diff_words->plus.text.size) {
		if (color_words_output_graph_prefix(diff_words))
			emit_diff_symbol(diff_words->opt, DIFF_SYMBOL_WORD_DIFF,
					 line_prefix, strlen(line_prefix), 0);
		fn_out_diff_words_write_helper(diff_words->opt,
			&style->ctx, style->newline,
			diff_words->plus.text.ptr + diff_words->plus.text.size
			- diff_words->current_plus, diff_words->current_plus);
	}
	diff_words->minus.text.size = diff_words->plus.text.size = 0;
}

/* In "color-words" mode, show word-diff of words accumulated in the buffer */
static void diff_words_flush(struct emit_callback *ecbdata)
{
	struct diff_options *wo = ecbdata->diff_words->opt;

	if (ecbdata->diff_words->minus.text.size ||
	    ecbdata->diff_words->plus.text.size)
		diff_words_show(ecbdata->diff_words);

	if (wo->emitted_symbols) {
		struct diff_options *o = ecbdata->opt;
		struct emitted_diff_symbols *wol = wo->emitted_symbols;
		int i;

		/*
		 * NEEDSWORK:
		 * Instead of appending each, concat all words to a line?
		 */
		for (i = 0; i < wol->nr; i++)
			append_emitted_diff_symbol(o, &wol->buf[i]);

		for (i = 0; i < wol->nr; i++)
			free((void *)wol->buf[i].line);

		wol->nr = 0;
	}
}

static void diff_filespec_load_driver(struct diff_filespec *one,
				      struct index_state *istate)
{
	/* Use already-loaded driver */
	if (one->driver)
		return;

	if (S_ISREG(one->mode))
		one->driver = userdiff_find_by_path(istate, one->path);

	/* Fallback to default settings */
	if (!one->driver)
		one->driver = userdiff_find_by_name("default");
}

static const char *userdiff_word_regex(struct diff_filespec *one,
				       struct index_state *istate)
{
	diff_filespec_load_driver(one, istate);
	return one->driver->word_regex;
}

static void init_diff_words_data(struct emit_callback *ecbdata,
				 struct diff_options *orig_opts,
				 struct diff_filespec *one,
				 struct diff_filespec *two)
{
	int i;
	struct diff_options *o = xmalloc(sizeof(struct diff_options));
	memcpy(o, orig_opts, sizeof(struct diff_options));

	ecbdata->diff_words =
		xcalloc(1, sizeof(struct diff_words_data));
	ecbdata->diff_words->type = o->word_diff;
	ecbdata->diff_words->opt = o;

	if (orig_opts->emitted_symbols)
		o->emitted_symbols =
			xcalloc(1, sizeof(struct emitted_diff_symbols));

	if (!o->word_regex)
		o->word_regex = userdiff_word_regex(one, o->repo->index);
	if (!o->word_regex)
		o->word_regex = userdiff_word_regex(two, o->repo->index);
	if (!o->word_regex)
		o->word_regex = diff_word_regex_cfg;
	if (o->word_regex) {
		ecbdata->diff_words->word_regex = (regex_t *)
			xmalloc(sizeof(regex_t));
		if (regcomp(ecbdata->diff_words->word_regex,
			    o->word_regex,
			    REG_EXTENDED | REG_NEWLINE))
			die("invalid regular expression: %s",
			    o->word_regex);
	}
	for (i = 0; i < ARRAY_SIZE(diff_words_styles); i++) {
		if (o->word_diff == diff_words_styles[i].type) {
			ecbdata->diff_words->style =
				&diff_words_styles[i];
			break;
		}
	}
	if (want_color(o->use_color)) {
		struct diff_words_style *st = ecbdata->diff_words->style;
		st->old_word.color = diff_get_color_opt(o, DIFF_FILE_OLD);
		st->new_word.color = diff_get_color_opt(o, DIFF_FILE_NEW);
		st->ctx.color = diff_get_color_opt(o, DIFF_CONTEXT);
	}
}

static void free_diff_words_data(struct emit_callback *ecbdata)
{
	if (ecbdata->diff_words) {
		diff_words_flush(ecbdata);
		free (ecbdata->diff_words->opt->emitted_symbols);
		free (ecbdata->diff_words->opt);
		free (ecbdata->diff_words->minus.text.ptr);
		free (ecbdata->diff_words->minus.orig);
		free (ecbdata->diff_words->plus.text.ptr);
		free (ecbdata->diff_words->plus.orig);
		if (ecbdata->diff_words->word_regex) {
			regfree(ecbdata->diff_words->word_regex);
			free(ecbdata->diff_words->word_regex);
		}
		FREE_AND_NULL(ecbdata->diff_words);
	}
}

const char *diff_get_color(int diff_use_color, enum color_diff ix)
{
	if (want_color(diff_use_color))
		return diff_colors[ix];
	return "";
}

const char *diff_line_prefix(struct diff_options *opt)
{
	struct strbuf *msgbuf;
	if (!opt->output_prefix)
		return "";

	msgbuf = opt->output_prefix(opt, opt->output_prefix_data);
	return msgbuf->buf;
}

static unsigned long sane_truncate_line(char *line, unsigned long len)
{
	const char *cp;
	unsigned long allot;
	size_t l = len;

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
	struct diff_options *o = ecbdata->opt;

	o->found_changes = 1;

	if (ecbdata->header) {
		emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
				 ecbdata->header->buf, ecbdata->header->len, 0);
		strbuf_reset(ecbdata->header);
		ecbdata->header = NULL;
	}

	if (ecbdata->label_path[0]) {
		emit_diff_symbol(o, DIFF_SYMBOL_FILEPAIR_MINUS,
				 ecbdata->label_path[0],
				 strlen(ecbdata->label_path[0]), 0);
		emit_diff_symbol(o, DIFF_SYMBOL_FILEPAIR_PLUS,
				 ecbdata->label_path[1],
				 strlen(ecbdata->label_path[1]), 0);
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
		len = sane_truncate_line(line, len);
		find_lno(line, ecbdata);
		emit_hunk_header(ecbdata, line, len);
		return;
	}

	if (ecbdata->diff_words) {
		enum diff_symbol s =
			ecbdata->diff_words->type == DIFF_WORDS_PORCELAIN ?
			DIFF_SYMBOL_WORDS_PORCELAIN : DIFF_SYMBOL_WORDS;
		if (line[0] == '-') {
			diff_words_append(line, len,
					  &ecbdata->diff_words->minus);
			return;
		} else if (line[0] == '+') {
			diff_words_append(line, len,
					  &ecbdata->diff_words->plus);
			return;
		} else if (starts_with(line, "\\ ")) {
			/*
			 * Eat the "no newline at eof" marker as if we
			 * saw a "+" or "-" line with nothing on it,
			 * and return without diff_words_flush() to
			 * defer processing. If this is the end of
			 * preimage, more "+" lines may come after it.
			 */
			return;
		}
		diff_words_flush(ecbdata);
		emit_diff_symbol(o, s, line, len, 0);
		return;
	}

	switch (line[0]) {
	case '+':
		ecbdata->lno_in_postimage++;
		emit_add_line(ecbdata, line + 1, len - 1);
		break;
	case '-':
		ecbdata->lno_in_preimage++;
		emit_del_line(ecbdata, line + 1, len - 1);
		break;
	case ' ':
		ecbdata->lno_in_postimage++;
		ecbdata->lno_in_preimage++;
		emit_context_line(ecbdata, line + 1, len - 1);
		break;
	default:
		/* incomplete line at the end */
		ecbdata->lno_in_preimage++;
		emit_diff_symbol(o, DIFF_SYMBOL_CONTEXT_INCOMPLETE,
				 line, len, 0);
		break;
	}
}

static void pprint_rename(struct strbuf *name, const char *a, const char *b)
{
	const char *old_name = a;
	const char *new_name = b;
	int pfx_length, sfx_length;
	int pfx_adjust_for_slash;
	int len_a = strlen(a);
	int len_b = strlen(b);
	int a_midlen, b_midlen;
	int qlen_a = quote_c_style(a, NULL, NULL, 0);
	int qlen_b = quote_c_style(b, NULL, NULL, 0);

	if (qlen_a || qlen_b) {
		quote_c_style(a, name, NULL, 0);
		strbuf_addstr(name, " => ");
		quote_c_style(b, name, NULL, 0);
		return;
	}

	/* Find common prefix */
	pfx_length = 0;
	while (*old_name && *new_name && *old_name == *new_name) {
		if (*old_name == '/')
			pfx_length = old_name - a + 1;
		old_name++;
		new_name++;
	}

	/* Find common suffix */
	old_name = a + len_a;
	new_name = b + len_b;
	sfx_length = 0;
	/*
	 * If there is a common prefix, it must end in a slash.  In
	 * that case we let this loop run 1 into the prefix to see the
	 * same slash.
	 *
	 * If there is no common prefix, we cannot do this as it would
	 * underrun the input strings.
	 */
	pfx_adjust_for_slash = (pfx_length ? 1 : 0);
	while (a + pfx_length - pfx_adjust_for_slash <= old_name &&
	       b + pfx_length - pfx_adjust_for_slash <= new_name &&
	       *old_name == *new_name) {
		if (*old_name == '/')
			sfx_length = len_a - (old_name - a);
		old_name--;
		new_name--;
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

	strbuf_grow(name, pfx_length + a_midlen + b_midlen + sfx_length + 7);
	if (pfx_length + sfx_length) {
		strbuf_add(name, a, pfx_length);
		strbuf_addch(name, '{');
	}
	strbuf_add(name, a + pfx_length, a_midlen);
	strbuf_addstr(name, " => ");
	strbuf_add(name, b + pfx_length, b_midlen);
	if (pfx_length + sfx_length) {
		strbuf_addch(name, '}');
		strbuf_add(name, a + len_a - sfx_length, sfx_length);
	}
}

static struct diffstat_file *diffstat_add(struct diffstat_t *diffstat,
					  const char *name_a,
					  const char *name_b)
{
	struct diffstat_file *x;
	x = xcalloc(1, sizeof(*x));
	ALLOC_GROW(diffstat->files, diffstat->nr + 1, diffstat->alloc);
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
	if (!it)
		return 0;
	/*
	 * make sure that at least one '-' or '+' is printed if
	 * there is any change to this path. The easiest way is to
	 * scale linearly as if the allotted width is one column shorter
	 * than it is, and then add 1 to the result.
	 */
	return 1 + (it * (width - 1) / max_change);
}

static void show_graph(struct strbuf *out, char ch, int cnt,
		       const char *set, const char *reset)
{
	if (cnt <= 0)
		return;
	strbuf_addstr(out, set);
	strbuf_addchars(out, ch, cnt);
	strbuf_addstr(out, reset);
}

static void fill_print_name(struct diffstat_file *file)
{
	struct strbuf pname = STRBUF_INIT;

	if (file->print_name)
		return;

	if (file->is_renamed)
		pprint_rename(&pname, file->from_name, file->name);
	else
		quote_c_style(file->name, &pname, NULL, 0);

	if (file->comments)
		strbuf_addf(&pname, " (%s)", file->comments);

	file->print_name = strbuf_detach(&pname, NULL);
}

static void print_stat_summary_inserts_deletes(struct diff_options *options,
		int files, int insertions, int deletions)
{
	struct strbuf sb = STRBUF_INIT;

	if (!files) {
		assert(insertions == 0 && deletions == 0);
		emit_diff_symbol(options, DIFF_SYMBOL_STATS_SUMMARY_NO_FILES,
				 NULL, 0, 0);
		return;
	}

	strbuf_addf(&sb,
		    (files == 1) ? " %d file changed" : " %d files changed",
		    files);

	/*
	 * For binary diff, the caller may want to print "x files
	 * changed" with insertions == 0 && deletions == 0.
	 *
	 * Not omitting "0 insertions(+), 0 deletions(-)" in this case
	 * is probably less confusing (i.e skip over "2 files changed
	 * but nothing about added/removed lines? Is this a bug in Git?").
	 */
	if (insertions || deletions == 0) {
		strbuf_addf(&sb,
			    (insertions == 1) ? ", %d insertion(+)" : ", %d insertions(+)",
			    insertions);
	}

	if (deletions || insertions == 0) {
		strbuf_addf(&sb,
			    (deletions == 1) ? ", %d deletion(-)" : ", %d deletions(-)",
			    deletions);
	}
	strbuf_addch(&sb, '\n');
	emit_diff_symbol(options, DIFF_SYMBOL_STATS_SUMMARY_INSERTS_DELETES,
			 sb.buf, sb.len, 0);
	strbuf_release(&sb);
}

void print_stat_summary(FILE *fp, int files,
			int insertions, int deletions)
{
	struct diff_options o;
	memset(&o, 0, sizeof(o));
	o.file = fp;

	print_stat_summary_inserts_deletes(&o, files, insertions, deletions);
}

static void show_stats(struct diffstat_t *data, struct diff_options *options)
{
	int i, len, add, del, adds = 0, dels = 0;
	uintmax_t max_change = 0, max_len = 0;
	int total_files = data->nr, count;
	int width, name_width, graph_width, number_width = 0, bin_width = 0;
	const char *reset, *add_c, *del_c;
	int extra_shown = 0;
	const char *line_prefix = diff_line_prefix(options);
	struct strbuf out = STRBUF_INIT;

	if (data->nr == 0)
		return;

	count = options->stat_count ? options->stat_count : data->nr;

	reset = diff_get_color_opt(options, DIFF_RESET);
	add_c = diff_get_color_opt(options, DIFF_FILE_NEW);
	del_c = diff_get_color_opt(options, DIFF_FILE_OLD);

	/*
	 * Find the longest filename and max number of changes
	 */
	for (i = 0; (i < count) && (i < data->nr); i++) {
		struct diffstat_file *file = data->files[i];
		uintmax_t change = file->added + file->deleted;

		if (!file->is_interesting && (change == 0)) {
			count++; /* not shown == room for one more */
			continue;
		}
		fill_print_name(file);
		len = strlen(file->print_name);
		if (max_len < len)
			max_len = len;

		if (file->is_unmerged) {
			/* "Unmerged" is 8 characters */
			bin_width = bin_width < 8 ? 8 : bin_width;
			continue;
		}
		if (file->is_binary) {
			/* "Bin XXX -> YYY bytes" */
			int w = 14 + decimal_width(file->added)
				+ decimal_width(file->deleted);
			bin_width = bin_width < w ? w : bin_width;
			/* Display change counts aligned with "Bin" */
			number_width = 3;
			continue;
		}

		if (max_change < change)
			max_change = change;
	}
	count = i; /* where we can stop scanning in data->files[] */

	/*
	 * We have width = stat_width or term_columns() columns total.
	 * We want a maximum of min(max_len, stat_name_width) for the name part.
	 * We want a maximum of min(max_change, stat_graph_width) for the +- part.
	 * We also need 1 for " " and 4 + decimal_width(max_change)
	 * for " | NNNN " and one the empty column at the end, altogether
	 * 6 + decimal_width(max_change).
	 *
	 * If there's not enough space, we will use the smaller of
	 * stat_name_width (if set) and 5/8*width for the filename,
	 * and the rest for constant elements + graph part, but no more
	 * than stat_graph_width for the graph part.
	 * (5/8 gives 50 for filename and 30 for the constant parts + graph
	 * for the standard terminal size).
	 *
	 * In other words: stat_width limits the maximum width, and
	 * stat_name_width fixes the maximum width of the filename,
	 * and is also used to divide available columns if there
	 * aren't enough.
	 *
	 * Binary files are displayed with "Bin XXX -> YYY bytes"
	 * instead of the change count and graph. This part is treated
	 * similarly to the graph part, except that it is not
	 * "scaled". If total width is too small to accommodate the
	 * guaranteed minimum width of the filename part and the
	 * separators and this message, this message will "overflow"
	 * making the line longer than the maximum width.
	 */

	if (options->stat_width == -1)
		width = term_columns() - strlen(line_prefix);
	else
		width = options->stat_width ? options->stat_width : 80;
	number_width = decimal_width(max_change) > number_width ?
		decimal_width(max_change) : number_width;

	if (options->stat_graph_width == -1)
		options->stat_graph_width = diff_stat_graph_width;

	/*
	 * Guarantee 3/8*16==6 for the graph part
	 * and 5/8*16==10 for the filename part
	 */
	if (width < 16 + 6 + number_width)
		width = 16 + 6 + number_width;

	/*
	 * First assign sizes that are wanted, ignoring available width.
	 * strlen("Bin XXX -> YYY bytes") == bin_width, and the part
	 * starting from "XXX" should fit in graph_width.
	 */
	graph_width = max_change + 4 > bin_width ? max_change : bin_width - 4;
	if (options->stat_graph_width &&
	    options->stat_graph_width < graph_width)
		graph_width = options->stat_graph_width;

	name_width = (options->stat_name_width > 0 &&
		      options->stat_name_width < max_len) ?
		options->stat_name_width : max_len;

	/*
	 * Adjust adjustable widths not to exceed maximum width
	 */
	if (name_width + number_width + 6 + graph_width > width) {
		if (graph_width > width * 3/8 - number_width - 6) {
			graph_width = width * 3/8 - number_width - 6;
			if (graph_width < 6)
				graph_width = 6;
		}

		if (options->stat_graph_width &&
		    graph_width > options->stat_graph_width)
			graph_width = options->stat_graph_width;
		if (name_width > width - number_width - 6 - graph_width)
			name_width = width - number_width - 6 - graph_width;
		else
			graph_width = width - number_width - 6 - name_width;
	}

	/*
	 * From here name_width is the width of the name area,
	 * and graph_width is the width of the graph area.
	 * max_change is used to scale graph properly.
	 */
	for (i = 0; i < count; i++) {
		const char *prefix = "";
		struct diffstat_file *file = data->files[i];
		char *name = file->print_name;
		uintmax_t added = file->added;
		uintmax_t deleted = file->deleted;
		int name_len;

		if (!file->is_interesting && (added + deleted == 0))
			continue;

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

		if (file->is_binary) {
			strbuf_addf(&out, " %s%-*s |", prefix, len, name);
			strbuf_addf(&out, " %*s", number_width, "Bin");
			if (!added && !deleted) {
				strbuf_addch(&out, '\n');
				emit_diff_symbol(options, DIFF_SYMBOL_STATS_LINE,
						 out.buf, out.len, 0);
				strbuf_reset(&out);
				continue;
			}
			strbuf_addf(&out, " %s%"PRIuMAX"%s",
				del_c, deleted, reset);
			strbuf_addstr(&out, " -> ");
			strbuf_addf(&out, "%s%"PRIuMAX"%s",
				add_c, added, reset);
			strbuf_addstr(&out, " bytes\n");
			emit_diff_symbol(options, DIFF_SYMBOL_STATS_LINE,
					 out.buf, out.len, 0);
			strbuf_reset(&out);
			continue;
		}
		else if (file->is_unmerged) {
			strbuf_addf(&out, " %s%-*s |", prefix, len, name);
			strbuf_addstr(&out, " Unmerged\n");
			emit_diff_symbol(options, DIFF_SYMBOL_STATS_LINE,
					 out.buf, out.len, 0);
			strbuf_reset(&out);
			continue;
		}

		/*
		 * scale the add/delete
		 */
		add = added;
		del = deleted;

		if (graph_width <= max_change) {
			int total = scale_linear(add + del, graph_width, max_change);
			if (total < 2 && add && del)
				/* width >= 2 due to the sanity check */
				total = 2;
			if (add < del) {
				add = scale_linear(add, graph_width, max_change);
				del = total - add;
			} else {
				del = scale_linear(del, graph_width, max_change);
				add = total - del;
			}
		}
		strbuf_addf(&out, " %s%-*s |", prefix, len, name);
		strbuf_addf(&out, " %*"PRIuMAX"%s",
			number_width, added + deleted,
			added + deleted ? " " : "");
		show_graph(&out, '+', add, add_c, reset);
		show_graph(&out, '-', del, del_c, reset);
		strbuf_addch(&out, '\n');
		emit_diff_symbol(options, DIFF_SYMBOL_STATS_LINE,
				 out.buf, out.len, 0);
		strbuf_reset(&out);
	}

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];
		uintmax_t added = file->added;
		uintmax_t deleted = file->deleted;

		if (file->is_unmerged ||
		    (!file->is_interesting && (added + deleted == 0))) {
			total_files--;
			continue;
		}

		if (!file->is_binary) {
			adds += added;
			dels += deleted;
		}
		if (i < count)
			continue;
		if (!extra_shown)
			emit_diff_symbol(options,
					 DIFF_SYMBOL_STATS_SUMMARY_ABBREV,
					 NULL, 0, 0);
		extra_shown = 1;
	}

	print_stat_summary_inserts_deletes(options, total_files, adds, dels);
	strbuf_release(&out);
}

static void show_shortstats(struct diffstat_t *data, struct diff_options *options)
{
	int i, adds = 0, dels = 0, total_files = data->nr;

	if (data->nr == 0)
		return;

	for (i = 0; i < data->nr; i++) {
		int added = data->files[i]->added;
		int deleted = data->files[i]->deleted;

		if (data->files[i]->is_unmerged ||
		    (!data->files[i]->is_interesting && (added + deleted == 0))) {
			total_files--;
		} else if (!data->files[i]->is_binary) { /* don't count bytes */
			adds += added;
			dels += deleted;
		}
	}
	print_stat_summary_inserts_deletes(options, total_files, adds, dels);
}

static void show_numstat(struct diffstat_t *data, struct diff_options *options)
{
	int i;

	if (data->nr == 0)
		return;

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];

		fprintf(options->file, "%s", diff_line_prefix(options));

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
	unsigned long sum_changes = 0;
	unsigned int sources = 0;
	const char *line_prefix = diff_line_prefix(opt);

	while (dir->nr) {
		struct dirstat_file *f = dir->files;
		int namelen = strlen(f->name);
		unsigned long changes;
		char *slash;

		if (namelen < baselen)
			break;
		if (memcmp(f->name, base, baselen))
			break;
		slash = strchr(f->name + baselen, '/');
		if (slash) {
			int newbaselen = slash + 1 - f->name;
			changes = gather_dirstat(opt, dir, changed, f->name, newbaselen);
			sources++;
		} else {
			changes = f->changed;
			dir->files++;
			dir->nr--;
			sources += 2;
		}
		sum_changes += changes;
	}

	/*
	 * We don't report dirstat's for
	 *  - the top level
	 *  - or cases where everything came from a single directory
	 *    under this directory (sources == 1).
	 */
	if (baselen && sources != 1) {
		if (sum_changes) {
			int permille = sum_changes * 1000 / changed;
			if (permille >= dir->permille) {
				fprintf(opt->file, "%s%4d.%01d%% %.*s\n", line_prefix,
					permille / 10, permille % 10, baselen, base);
				if (!dir->cumulative)
					return 0;
			}
		}
	}
	return sum_changes;
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
	dir.cumulative = options->flags.dirstat_cumulative;

	changed = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *name;
		unsigned long copied, added, damage;

		name = p->two->path ? p->two->path : p->one->path;

		if (p->one->oid_valid && p->two->oid_valid &&
		    oideq(&p->one->oid, &p->two->oid)) {
			/*
			 * The SHA1 has not changed, so pre-/post-content is
			 * identical. We can therefore skip looking at the
			 * file contents altogether.
			 */
			damage = 0;
			goto found_damage;
		}

		if (options->flags.dirstat_by_file) {
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
			diff_populate_filespec(options->repo, p->one, 0);
			diff_populate_filespec(options->repo, p->two, 0);
			diffcore_count_changes(options->repo,
					       p->one, p->two, NULL, NULL,
					       &copied, &added);
			diff_free_filespec_data(p->one);
			diff_free_filespec_data(p->two);
		} else if (DIFF_FILE_VALID(p->one)) {
			diff_populate_filespec(options->repo, p->one, CHECK_SIZE_ONLY);
			copied = added = 0;
			diff_free_filespec_data(p->one);
		} else if (DIFF_FILE_VALID(p->two)) {
			diff_populate_filespec(options->repo, p->two, CHECK_SIZE_ONLY);
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
		 * be identical, but since the oid changed, we
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
	QSORT(dir.files, dir.nr, dirstat_compare);
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
	dir.cumulative = options->flags.dirstat_cumulative;

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
			damage = DIV_ROUND_UP(damage, 64);
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
	QSORT(dir.files, dir.nr, dirstat_compare);
	gather_dirstat(options, &dir, changed, "", 0);
}

void free_diffstat_info(struct diffstat_t *diffstat)
{
	int i;
	for (i = 0; i < diffstat->nr; i++) {
		struct diffstat_file *f = diffstat->files[i];
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
	/* line[1] through line[marker_size-1] are same as firstchar */
	if (len < marker_size + 1 || !isspace(line[marker_size]))
		return 0;
	return 1;
}

static void checkdiff_consume_hunk(void *priv,
				   long ob, long on, long nb, long nn,
				   const char *func, long funclen)

{
	struct checkdiff_t *data = priv;
	data->lineno = nb - 1;
}

static void checkdiff_consume(void *priv, char *line, unsigned long len)
{
	struct checkdiff_t *data = priv;
	int marker_size = data->conflict_marker_size;
	const char *ws = diff_get_color(data->o->use_color, DIFF_WHITESPACE);
	const char *reset = diff_get_color(data->o->use_color, DIFF_RESET);
	const char *set = diff_get_color(data->o->use_color, DIFF_FILE_NEW);
	char *err;
	const char *line_prefix;

	assert(data->o);
	line_prefix = diff_line_prefix(data->o);

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
	}
}

static unsigned char *deflate_it(char *data,
				 unsigned long size,
				 unsigned long *result_size)
{
	int bound;
	unsigned char *deflated;
	git_zstream stream;

	git_deflate_init(&stream, zlib_compression_level);
	bound = git_deflate_bound(&stream, size);
	deflated = xmalloc(bound);
	stream.next_out = deflated;
	stream.avail_out = bound;

	stream.next_in = (unsigned char *)data;
	stream.avail_in = size;
	while (git_deflate(&stream, Z_FINISH) == Z_OK)
		; /* nothing */
	git_deflate_end(&stream);
	*result_size = stream.total_out;
	return deflated;
}

static void emit_binary_diff_body(struct diff_options *o,
				  mmfile_t *one, mmfile_t *two)
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
		char *s = xstrfmt("%"PRIuMAX , (uintmax_t)orig_size);
		emit_diff_symbol(o, DIFF_SYMBOL_BINARY_DIFF_HEADER_DELTA,
				 s, strlen(s), 0);
		free(s);
		free(deflated);
		data = delta;
		data_size = delta_size;
	} else {
		char *s = xstrfmt("%lu", two->size);
		emit_diff_symbol(o, DIFF_SYMBOL_BINARY_DIFF_HEADER_LITERAL,
				 s, strlen(s), 0);
		free(s);
		free(delta);
		data = deflated;
		data_size = deflate_size;
	}

	/* emit data encoded in base85 */
	cp = data;
	while (data_size) {
		int len;
		int bytes = (52 < data_size) ? 52 : data_size;
		char line[71];
		data_size -= bytes;
		if (bytes <= 26)
			line[0] = bytes + 'A' - 1;
		else
			line[0] = bytes - 26 + 'a' - 1;
		encode_85(line + 1, cp, bytes);
		cp = (char *) cp + bytes;

		len = strlen(line);
		line[len++] = '\n';
		line[len] = '\0';

		emit_diff_symbol(o, DIFF_SYMBOL_BINARY_DIFF_BODY,
				 line, len, 0);
	}
	emit_diff_symbol(o, DIFF_SYMBOL_BINARY_DIFF_FOOTER, NULL, 0, 0);
	free(data);
}

static void emit_binary_diff(struct diff_options *o,
			     mmfile_t *one, mmfile_t *two)
{
	emit_diff_symbol(o, DIFF_SYMBOL_BINARY_DIFF_HEADER, NULL, 0, 0);
	emit_binary_diff_body(o, one, two);
	emit_binary_diff_body(o, two, one);
}

int diff_filespec_is_binary(struct repository *r,
			    struct diff_filespec *one)
{
	if (one->is_binary == -1) {
		diff_filespec_load_driver(one, r->index);
		if (one->driver->binary != -1)
			one->is_binary = one->driver->binary;
		else {
			if (!one->data && DIFF_FILE_VALID(one))
				diff_populate_filespec(r, one, CHECK_BINARY);
			if (one->is_binary == -1 && one->data)
				one->is_binary = buffer_is_binary(one->data,
						one->size);
			if (one->is_binary == -1)
				one->is_binary = 0;
		}
	}
	return one->is_binary;
}

static const struct userdiff_funcname *
diff_funcname_pattern(struct diff_options *o, struct diff_filespec *one)
{
	diff_filespec_load_driver(one, o->repo->index);
	return one->driver->funcname.pattern ? &one->driver->funcname : NULL;
}

void diff_set_mnemonic_prefix(struct diff_options *options, const char *a, const char *b)
{
	if (!options->a_prefix)
		options->a_prefix = a;
	if (!options->b_prefix)
		options->b_prefix = b;
}

struct userdiff_driver *get_textconv(struct repository *r,
				     struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one))
		return NULL;

	diff_filespec_load_driver(one, r->index);
	return userdiff_get_textconv(r, one->driver);
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
	const char *meta = diff_get_color_opt(o, DIFF_METAINFO);
	const char *reset = diff_get_color_opt(o, DIFF_RESET);
	const char *a_prefix, *b_prefix;
	struct userdiff_driver *textconv_one = NULL;
	struct userdiff_driver *textconv_two = NULL;
	struct strbuf header = STRBUF_INIT;
	const char *line_prefix = diff_line_prefix(o);

	diff_set_mnemonic_prefix(o, "a/", "b/");
	if (o->flags.reverse_diff) {
		a_prefix = o->b_prefix;
		b_prefix = o->a_prefix;
	} else {
		a_prefix = o->a_prefix;
		b_prefix = o->b_prefix;
	}

	if (o->submodule_format == DIFF_SUBMODULE_LOG &&
	    (!one->mode || S_ISGITLINK(one->mode)) &&
	    (!two->mode || S_ISGITLINK(two->mode))) {
		show_submodule_summary(o, one->path ? one->path : two->path,
				&one->oid, &two->oid,
				two->dirty_submodule);
		return;
	} else if (o->submodule_format == DIFF_SUBMODULE_INLINE_DIFF &&
		   (!one->mode || S_ISGITLINK(one->mode)) &&
		   (!two->mode || S_ISGITLINK(two->mode))) {
		show_submodule_inline_diff(o, one->path ? one->path : two->path,
				&one->oid, &two->oid,
				two->dirty_submodule);
		return;
	}

	if (o->flags.allow_textconv) {
		textconv_one = get_textconv(o->repo, one);
		textconv_two = get_textconv(o->repo, two);
	}

	/* Never use a non-valid filename anywhere if at all possible */
	name_a = DIFF_FILE_VALID(one) ? name_a : name_b;
	name_b = DIFF_FILE_VALID(two) ? name_b : name_a;

	a_one = quote_two(a_prefix, name_a + (*name_a == '/'));
	b_two = quote_two(b_prefix, name_b + (*name_b == '/'));
	lbl[0] = DIFF_FILE_VALID(one) ? a_one : "/dev/null";
	lbl[1] = DIFF_FILE_VALID(two) ? b_two : "/dev/null";
	strbuf_addf(&header, "%s%sdiff --git %s %s%s\n", line_prefix, meta, a_one, b_two, reset);
	if (lbl[0][0] == '/') {
		/* /dev/null */
		strbuf_addf(&header, "%s%snew file mode %06o%s\n", line_prefix, meta, two->mode, reset);
		if (xfrm_msg)
			strbuf_addstr(&header, xfrm_msg);
		must_show_header = 1;
	}
	else if (lbl[1][0] == '/') {
		strbuf_addf(&header, "%s%sdeleted file mode %06o%s\n", line_prefix, meta, one->mode, reset);
		if (xfrm_msg)
			strbuf_addstr(&header, xfrm_msg);
		must_show_header = 1;
	}
	else {
		if (one->mode != two->mode) {
			strbuf_addf(&header, "%s%sold mode %06o%s\n", line_prefix, meta, one->mode, reset);
			strbuf_addf(&header, "%s%snew mode %06o%s\n", line_prefix, meta, two->mode, reset);
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
		    (textconv_one || !diff_filespec_is_binary(o->repo, one)) &&
		    (textconv_two || !diff_filespec_is_binary(o->repo, two))) {
			emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
					 header.buf, header.len, 0);
			strbuf_reset(&header);
			emit_rewrite_diff(name_a, name_b, one, two,
					  textconv_one, textconv_two, o);
			o->found_changes = 1;
			goto free_ab_and_return;
		}
	}

	if (o->irreversible_delete && lbl[1][0] == '/') {
		emit_diff_symbol(o, DIFF_SYMBOL_HEADER, header.buf,
				 header.len, 0);
		strbuf_reset(&header);
		goto free_ab_and_return;
	} else if (!o->flags.text &&
		   ( (!textconv_one && diff_filespec_is_binary(o->repo, one)) ||
		     (!textconv_two && diff_filespec_is_binary(o->repo, two)) )) {
		struct strbuf sb = STRBUF_INIT;
		if (!one->data && !two->data &&
		    S_ISREG(one->mode) && S_ISREG(two->mode) &&
		    !o->flags.binary) {
			if (oideq(&one->oid, &two->oid)) {
				if (must_show_header)
					emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
							 header.buf, header.len,
							 0);
				goto free_ab_and_return;
			}
			emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
					 header.buf, header.len, 0);
			strbuf_addf(&sb, "%sBinary files %s and %s differ\n",
				    diff_line_prefix(o), lbl[0], lbl[1]);
			emit_diff_symbol(o, DIFF_SYMBOL_BINARY_FILES,
					 sb.buf, sb.len, 0);
			strbuf_release(&sb);
			goto free_ab_and_return;
		}
		if (fill_mmfile(o->repo, &mf1, one) < 0 ||
		    fill_mmfile(o->repo, &mf2, two) < 0)
			die("unable to read files to diff");
		/* Quite common confusing case */
		if (mf1.size == mf2.size &&
		    !memcmp(mf1.ptr, mf2.ptr, mf1.size)) {
			if (must_show_header)
				emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
						 header.buf, header.len, 0);
			goto free_ab_and_return;
		}
		emit_diff_symbol(o, DIFF_SYMBOL_HEADER, header.buf, header.len, 0);
		strbuf_reset(&header);
		if (o->flags.binary)
			emit_binary_diff(o, &mf1, &mf2);
		else {
			strbuf_addf(&sb, "%sBinary files %s and %s differ\n",
				    diff_line_prefix(o), lbl[0], lbl[1]);
			emit_diff_symbol(o, DIFF_SYMBOL_BINARY_FILES,
					 sb.buf, sb.len, 0);
			strbuf_release(&sb);
		}
		o->found_changes = 1;
	} else {
		/* Crazy xdl interfaces.. */
		const char *diffopts;
		const char *v;
		xpparam_t xpp;
		xdemitconf_t xecfg;
		struct emit_callback ecbdata;
		const struct userdiff_funcname *pe;

		if (must_show_header) {
			emit_diff_symbol(o, DIFF_SYMBOL_HEADER,
					 header.buf, header.len, 0);
			strbuf_reset(&header);
		}

		mf1.size = fill_textconv(o->repo, textconv_one, one, &mf1.ptr);
		mf2.size = fill_textconv(o->repo, textconv_two, two, &mf2.ptr);

		pe = diff_funcname_pattern(o, one);
		if (!pe)
			pe = diff_funcname_pattern(o, two);

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		memset(&ecbdata, 0, sizeof(ecbdata));
		if (o->flags.suppress_diff_headers)
			lbl[0] = NULL;
		ecbdata.label_path = lbl;
		ecbdata.color_diff = want_color(o->use_color);
		ecbdata.ws_rule = whitespace_rule(o->repo->index, name_b);
		if (ecbdata.ws_rule & WS_BLANK_AT_EOF)
			check_blank_at_eof(&mf1, &mf2, &ecbdata);
		ecbdata.opt = o;
		if (header.len && !o->flags.suppress_diff_headers)
			ecbdata.header = &header;
		xpp.flags = o->xdl_opts;
		xpp.anchors = o->anchors;
		xpp.anchors_nr = o->anchors_nr;
		xecfg.ctxlen = o->context;
		xecfg.interhunkctxlen = o->interhunkcontext;
		xecfg.flags = XDL_EMIT_FUNCNAMES;
		if (o->flags.funccontext)
			xecfg.flags |= XDL_EMIT_FUNCCONTEXT;
		if (pe)
			xdiff_set_find_func(&xecfg, pe->pattern, pe->cflags);

		diffopts = getenv("GIT_DIFF_OPTS");
		if (!diffopts)
			;
		else if (skip_prefix(diffopts, "--unified=", &v))
			xecfg.ctxlen = strtoul(v, NULL, 10);
		else if (skip_prefix(diffopts, "-u", &v))
			xecfg.ctxlen = strtoul(v, NULL, 10);

		if (o->word_diff)
			init_diff_words_data(&ecbdata, o, one, two);
		if (xdi_diff_outf(&mf1, &mf2, NULL, fn_out_consume,
				  &ecbdata, &xpp, &xecfg))
			die("unable to generate diff for %s", one->path);
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

static char *get_compact_summary(const struct diff_filepair *p, int is_renamed)
{
	if (!is_renamed) {
		if (p->status == DIFF_STATUS_ADDED) {
			if (S_ISLNK(p->two->mode))
				return "new +l";
			else if ((p->two->mode & 0777) == 0755)
				return "new +x";
			else
				return "new";
		} else if (p->status == DIFF_STATUS_DELETED)
			return "gone";
	}
	if (S_ISLNK(p->one->mode) && !S_ISLNK(p->two->mode))
		return "mode -l";
	else if (!S_ISLNK(p->one->mode) && S_ISLNK(p->two->mode))
		return "mode +l";
	else if ((p->one->mode & 0777) == 0644 &&
		 (p->two->mode & 0777) == 0755)
		return "mode +x";
	else if ((p->one->mode & 0777) == 0755 &&
		 (p->two->mode & 0777) == 0644)
		return "mode -x";
	return NULL;
}

static void builtin_diffstat(const char *name_a, const char *name_b,
			     struct diff_filespec *one,
			     struct diff_filespec *two,
			     struct diffstat_t *diffstat,
			     struct diff_options *o,
			     struct diff_filepair *p)
{
	mmfile_t mf1, mf2;
	struct diffstat_file *data;
	int same_contents;
	int complete_rewrite = 0;

	if (!DIFF_PAIR_UNMERGED(p)) {
		if (p->status == DIFF_STATUS_MODIFIED && p->score)
			complete_rewrite = 1;
	}

	data = diffstat_add(diffstat, name_a, name_b);
	data->is_interesting = p->status != DIFF_STATUS_UNKNOWN;
	if (o->flags.stat_with_summary)
		data->comments = get_compact_summary(p, data->is_renamed);

	if (!one || !two) {
		data->is_unmerged = 1;
		return;
	}

	same_contents = oideq(&one->oid, &two->oid);

	if (diff_filespec_is_binary(o->repo, one) ||
	    diff_filespec_is_binary(o->repo, two)) {
		data->is_binary = 1;
		if (same_contents) {
			data->added = 0;
			data->deleted = 0;
		} else {
			data->added = diff_filespec_size(o->repo, two);
			data->deleted = diff_filespec_size(o->repo, one);
		}
	}

	else if (complete_rewrite) {
		diff_populate_filespec(o->repo, one, 0);
		diff_populate_filespec(o->repo, two, 0);
		data->deleted = count_lines(one->data, one->size);
		data->added = count_lines(two->data, two->size);
	}

	else if (!same_contents) {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;

		if (fill_mmfile(o->repo, &mf1, one) < 0 ||
		    fill_mmfile(o->repo, &mf2, two) < 0)
			die("unable to read files to diff");

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		xpp.flags = o->xdl_opts;
		xpp.anchors = o->anchors;
		xpp.anchors_nr = o->anchors_nr;
		xecfg.ctxlen = o->context;
		xecfg.interhunkctxlen = o->interhunkcontext;
		if (xdi_diff_outf(&mf1, &mf2, discard_hunk_line,
				  diffstat_consume, diffstat, &xpp, &xecfg))
			die("unable to generate diffstat for %s", one->path);
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
	data.ws_rule = whitespace_rule(o->repo->index, attr_path);
	data.conflict_marker_size = ll_merge_marker_size(o->repo->index, attr_path);

	if (fill_mmfile(o->repo, &mf1, one) < 0 ||
	    fill_mmfile(o->repo, &mf2, two) < 0)
		die("unable to read files to diff");

	/*
	 * All the other codepaths check both sides, but not checking
	 * the "old" side here is deliberate.  We are checking the newly
	 * introduced changes, and as long as the "new" side is text, we
	 * can and should check what it introduces.
	 */
	if (diff_filespec_is_binary(o->repo, two))
		goto free_and_return;
	else {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		xecfg.ctxlen = 1; /* at least one context line */
		xpp.flags = 0;
		if (xdi_diff_outf(&mf1, &mf2, checkdiff_consume_hunk,
				  checkdiff_consume, &data,
				  &xpp, &xecfg))
			die("unable to generate checkdiff for %s", one->path);

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
		o->flags.check_failed = 1;
}

struct diff_filespec *alloc_filespec(const char *path)
{
	struct diff_filespec *spec;

	FLEXPTR_ALLOC_STR(spec, path, path);
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

void fill_filespec(struct diff_filespec *spec, const struct object_id *oid,
		   int oid_valid, unsigned short mode)
{
	if (mode) {
		spec->mode = canon_mode(mode);
		oidcpy(&spec->oid, oid);
		spec->oid_valid = oid_valid;
	}
}

/*
 * Given a name and sha1 pair, if the index tells us the file in
 * the work tree has that object contents, return true, so that
 * prepare_temp_file() does not have to inflate and extract.
 */
static int reuse_worktree_file(struct index_state *istate,
			       const char *name,
			       const struct object_id *oid,
			       int want_file)
{
	const struct cache_entry *ce;
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
	if (!istate->cache)
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
	if (!FAST_WORKING_DIRECTORY && !want_file && has_object_pack(oid))
		return 0;

	/*
	 * Similarly, if we'd have to convert the file contents anyway, that
	 * makes the optimization not worthwhile.
	 */
	if (!want_file && would_convert_to_git(istate, name))
		return 0;

	len = strlen(name);
	pos = index_name_pos(istate, name, len);
	if (pos < 0)
		return 0;
	ce = istate->cache[pos];

	/*
	 * This is not the sha1 we are looking for, or
	 * unreusable because it is not a regular file.
	 */
	if (!oideq(oid, &ce->oid) || !S_ISREG(ce->ce_mode))
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
	    (!lstat(name, &st) && !ie_match_stat(istate, ce, &st, 0)))
		return 1;

	return 0;
}

static int diff_populate_gitlink(struct diff_filespec *s, int size_only)
{
	struct strbuf buf = STRBUF_INIT;
	char *dirty = "";

	/* Are we looking at the work tree? */
	if (s->dirty_submodule)
		dirty = "-dirty";

	strbuf_addf(&buf, "Subproject commit %s%s\n",
		    oid_to_hex(&s->oid), dirty);
	s->size = buf.len;
	if (size_only) {
		s->data = NULL;
		strbuf_release(&buf);
	} else {
		s->data = strbuf_detach(&buf, NULL);
		s->should_free = 1;
	}
	return 0;
}

/*
 * While doing rename detection and pickaxe operation, we may need to
 * grab the data for the blob (or file) for our own in-core comparison.
 * diff_filespec has data and size fields for this purpose.
 */
int diff_populate_filespec(struct repository *r,
			   struct diff_filespec *s,
			   unsigned int flags)
{
	int size_only = flags & CHECK_SIZE_ONLY;
	int err = 0;
	int conv_flags = global_conv_flags_eol;
	/*
	 * demote FAIL to WARN to allow inspecting the situation
	 * instead of refusing.
	 */
	if (conv_flags & CONV_EOL_RNDTRP_DIE)
		conv_flags = CONV_EOL_RNDTRP_WARN;

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

	if (!s->oid_valid ||
	    reuse_worktree_file(r->index, s->path, &s->oid, 0)) {
		struct strbuf buf = STRBUF_INIT;
		struct stat st;
		int fd;

		if (lstat(s->path, &st) < 0) {
		err_empty:
			err = -1;
		empty:
			s->data = (char *)"";
			s->size = 0;
			return err;
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

		/*
		 * Even if the caller would be happy with getting
		 * only the size, we cannot return early at this
		 * point if the path requires us to run the content
		 * conversion.
		 */
		if (size_only && !would_convert_to_git(r->index, s->path))
			return 0;

		/*
		 * Note: this check uses xsize_t(st.st_size) that may
		 * not be the true size of the blob after it goes
		 * through convert_to_git().  This may not strictly be
		 * correct, but the whole point of big_file_threshold
		 * and is_binary check being that we want to avoid
		 * opening the file and inspecting the contents, this
		 * is probably fine.
		 */
		if ((flags & CHECK_BINARY) &&
		    s->size > big_file_threshold && s->is_binary == -1) {
			s->is_binary = 1;
			return 0;
		}
		fd = open(s->path, O_RDONLY);
		if (fd < 0)
			goto err_empty;
		s->data = xmmap(NULL, s->size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		s->should_munmap = 1;

		/*
		 * Convert from working tree format to canonical git format
		 */
		if (convert_to_git(r->index, s->path, s->data, s->size, &buf, conv_flags)) {
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
		if (size_only || (flags & CHECK_BINARY)) {
			type = oid_object_info(r, &s->oid, &s->size);
			if (type < 0)
				die("unable to read %s",
				    oid_to_hex(&s->oid));
			if (size_only)
				return 0;
			if (s->size > big_file_threshold && s->is_binary == -1) {
				s->is_binary = 1;
				return 0;
			}
		}
		s->data = repo_read_object_file(r, &s->oid, &type, &s->size);
		if (!s->data)
			die("unable to read %s", oid_to_hex(&s->oid));
		s->should_free = 1;
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
	FREE_AND_NULL(s->cnt_data);
}

static void prep_temp_blob(struct index_state *istate,
			   const char *path, struct diff_tempfile *temp,
			   void *blob,
			   unsigned long size,
			   const struct object_id *oid,
			   int mode)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf tempfile = STRBUF_INIT;
	char *path_dup = xstrdup(path);
	const char *base = basename(path_dup);

	/* Generate "XXXXXX_basename.ext" */
	strbuf_addstr(&tempfile, "XXXXXX_");
	strbuf_addstr(&tempfile, base);

	temp->tempfile = mks_tempfile_ts(tempfile.buf, strlen(base) + 1);
	if (!temp->tempfile)
		die_errno("unable to create temp-file");
	if (convert_to_working_tree(istate, path,
			(const char *)blob, (size_t)size, &buf)) {
		blob = buf.buf;
		size = buf.len;
	}
	if (write_in_full(temp->tempfile->fd, blob, size) < 0 ||
	    close_tempfile_gently(temp->tempfile))
		die_errno("unable to write temp-file");
	temp->name = get_tempfile_path(temp->tempfile);
	oid_to_hex_r(temp->hex, oid);
	xsnprintf(temp->mode, sizeof(temp->mode), "%06o", mode);
	strbuf_release(&buf);
	strbuf_release(&tempfile);
	free(path_dup);
}

static struct diff_tempfile *prepare_temp_file(struct repository *r,
					       const char *name,
					       struct diff_filespec *one)
{
	struct diff_tempfile *temp = claim_diff_tempfile();

	if (!DIFF_FILE_VALID(one)) {
	not_a_valid_file:
		/* A '-' entry produces this for file-2, and
		 * a '+' entry produces this for file-1.
		 */
		temp->name = "/dev/null";
		xsnprintf(temp->hex, sizeof(temp->hex), ".");
		xsnprintf(temp->mode, sizeof(temp->mode), ".");
		return temp;
	}

	if (!S_ISGITLINK(one->mode) &&
	    (!one->oid_valid ||
	     reuse_worktree_file(r->index, name, &one->oid, 1))) {
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
			prep_temp_blob(r->index, name, temp, sb.buf, sb.len,
				       (one->oid_valid ?
					&one->oid : &null_oid),
				       (one->oid_valid ?
					one->mode : S_IFLNK));
			strbuf_release(&sb);
		}
		else {
			/* we can borrow from the file in the work tree */
			temp->name = name;
			if (!one->oid_valid)
				oid_to_hex_r(temp->hex, &null_oid);
			else
				oid_to_hex_r(temp->hex, &one->oid);
			/* Even though we may sometimes borrow the
			 * contents from the work tree, we always want
			 * one->mode.  mode is trustworthy even when
			 * !(one->oid_valid), as long as
			 * DIFF_FILE_VALID(one).
			 */
			xsnprintf(temp->mode, sizeof(temp->mode), "%06o", one->mode);
		}
		return temp;
	}
	else {
		if (diff_populate_filespec(r, one, 0))
			die("cannot read data blob for %s", one->path);
		prep_temp_blob(r->index, name, temp,
			       one->data, one->size,
			       &one->oid, one->mode);
	}
	return temp;
}

static void add_external_diff_name(struct repository *r,
				   struct argv_array *argv,
				   const char *name,
				   struct diff_filespec *df)
{
	struct diff_tempfile *temp = prepare_temp_file(r, name, df);
	argv_array_push(argv, temp->name);
	argv_array_push(argv, temp->hex);
	argv_array_push(argv, temp->mode);
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
			      struct diff_options *o)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	struct argv_array env = ARGV_ARRAY_INIT;
	struct diff_queue_struct *q = &diff_queued_diff;

	argv_array_push(&argv, pgm);
	argv_array_push(&argv, name);

	if (one && two) {
		add_external_diff_name(o->repo, &argv, name, one);
		if (!other)
			add_external_diff_name(o->repo, &argv, name, two);
		else {
			add_external_diff_name(o->repo, &argv, other, two);
			argv_array_push(&argv, other);
			argv_array_push(&argv, xfrm_msg);
		}
	}

	argv_array_pushf(&env, "GIT_DIFF_PATH_COUNTER=%d", ++o->diff_path_counter);
	argv_array_pushf(&env, "GIT_DIFF_PATH_TOTAL=%d", q->nr);

	diff_free_filespec_data(one);
	diff_free_filespec_data(two);
	if (run_command_v_opt_cd_env(argv.argv, RUN_USING_SHELL, NULL, env.argv))
		die(_("external diff died, stopping at %s"), name);

	remove_tempfile();
	argv_array_clear(&argv);
	argv_array_clear(&env);
}

static int similarity_index(struct diff_filepair *p)
{
	return p->score * 100 / MAX_SCORE;
}

static const char *diff_abbrev_oid(const struct object_id *oid, int abbrev)
{
	if (startup_info->have_repository)
		return find_unique_abbrev(oid, abbrev);
	else {
		char *hex = oid_to_hex(oid);
		if (abbrev < 0)
			abbrev = FALLBACK_DEFAULT_ABBREV;
		if (abbrev > the_hash_algo->hexsz)
			BUG("oid abbreviation out of range: %d", abbrev);
		if (abbrev)
			hex[abbrev] = '\0';
		return hex;
	}
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
	const char *line_prefix = diff_line_prefix(o);

	*must_show_header = 1;
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
	if (one && two && !oideq(&one->oid, &two->oid)) {
		const unsigned hexsz = the_hash_algo->hexsz;
		int abbrev = o->flags.full_index ? hexsz : DEFAULT_ABBREV;

		if (o->flags.binary) {
			mmfile_t mf;
			if ((!fill_mmfile(o->repo, &mf, one) &&
			     diff_filespec_is_binary(o->repo, one)) ||
			    (!fill_mmfile(o->repo, &mf, two) &&
			     diff_filespec_is_binary(o->repo, two)))
				abbrev = hexsz;
		}
		strbuf_addf(msg, "%s%sindex %s..%s", line_prefix, set,
			    diff_abbrev_oid(&one->oid, abbrev),
			    diff_abbrev_oid(&two->oid, abbrev));
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


	if (o->flags.allow_external) {
		struct userdiff_driver *drv;

		drv = userdiff_find_by_path(o->repo->index, attr_path);
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
			      want_color(o->use_color) && !pgm);
		xfrm_msg = msg->len ? msg->buf : NULL;
	}

	if (pgm) {
		run_external_diff(pgm, name, other, one, two, xfrm_msg, o);
		return;
	}
	if (one && two)
		builtin_diff(name, other ? other : name,
			     one, two, xfrm_msg, must_show_header,
			     o, complete_rewrite);
	else
		fprintf(o->file, "* Unmerged path %s\n", name);
}

static void diff_fill_oid_info(struct diff_filespec *one, struct index_state *istate)
{
	if (DIFF_FILE_VALID(one)) {
		if (!one->oid_valid) {
			struct stat st;
			if (one->is_stdin) {
				oidclr(&one->oid);
				return;
			}
			if (lstat(one->path, &st) < 0)
				die_errno("stat '%s'", one->path);
			if (index_path(istate, &one->oid, one->path, &st, 0))
				die("cannot hash %s", one->path);
		}
	}
	else
		oidclr(&one->oid);
}

static void strip_prefix(int prefix_length, const char **namep, const char **otherp)
{
	/* Strip the prefix but do not molest /dev/null and absolute paths */
	if (*namep && !is_absolute_path(*namep)) {
		*namep += prefix_length;
		if (**namep == '/')
			++*namep;
	}
	if (*otherp && !is_absolute_path(*otherp)) {
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

	name  = one->path;
	other = (strcmp(name, two->path) ? two->path : NULL);
	attr_path = name;
	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	if (!o->flags.allow_external)
		pgm = NULL;

	if (DIFF_PAIR_UNMERGED(p)) {
		run_diff_cmd(pgm, name, NULL, attr_path,
			     NULL, NULL, NULL, o, p);
		return;
	}

	diff_fill_oid_info(one, o->repo->index);
	diff_fill_oid_info(two, o->repo->index);

	if (!pgm &&
	    DIFF_FILE_VALID(one) && DIFF_FILE_VALID(two) &&
	    (S_IFMT & one->mode) != (S_IFMT & two->mode)) {
		/*
		 * a filepair that changes between file and symlink
		 * needs to be split into deletion and creation.
		 */
		struct diff_filespec *null = alloc_filespec(two->path);
		run_diff_cmd(NULL, name, other, attr_path,
			     one, null, &msg,
			     o, p);
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

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		builtin_diffstat(p->one->path, NULL, NULL, NULL,
				 diffstat, o, p);
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);

	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	diff_fill_oid_info(p->one, o->repo->index);
	diff_fill_oid_info(p->two, o->repo->index);

	builtin_diffstat(name, other, p->one, p->two,
			 diffstat, o, p);
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

	diff_fill_oid_info(p->one, o->repo->index);
	diff_fill_oid_info(p->two, o->repo->index);

	builtin_checkdiff(name, other, attr_path, p->one, p->two, o);
}

static void prep_parse_options(struct diff_options *options);

void repo_diff_setup(struct repository *r, struct diff_options *options)
{
	memcpy(options, &default_diff_options, sizeof(*options));

	options->file = stdout;
	options->repo = r;

	options->output_indicators[OUTPUT_INDICATOR_NEW] = '+';
	options->output_indicators[OUTPUT_INDICATOR_OLD] = '-';
	options->output_indicators[OUTPUT_INDICATOR_CONTEXT] = ' ';
	options->abbrev = DEFAULT_ABBREV;
	options->line_termination = '\n';
	options->break_opt = -1;
	options->rename_limit = -1;
	options->dirstat_permille = diff_dirstat_permille_default;
	options->context = diff_context_default;
	options->interhunkcontext = diff_interhunk_context_default;
	options->ws_error_highlight = ws_error_highlight_default;
	options->flags.rename_empty = 1;
	options->objfind = NULL;

	/* pathchange left =NULL by default */
	options->change = diff_change;
	options->add_remove = diff_addremove;
	options->use_color = diff_use_color_default;
	options->detect_rename = diff_detect_rename_default;
	options->xdl_opts |= diff_algorithm;
	if (diff_indent_heuristic)
		DIFF_XDL_SET(options, INDENT_HEURISTIC);

	options->orderfile = diff_order_file_cfg;

	if (diff_no_prefix) {
		options->a_prefix = options->b_prefix = "";
	} else if (!diff_mnemonic_prefix) {
		options->a_prefix = "a/";
		options->b_prefix = "b/";
	}

	options->color_moved = diff_color_moved_default;
	options->color_moved_ws_handling = diff_color_moved_ws_default;

	prep_parse_options(options);
}

void diff_setup_done(struct diff_options *options)
{
	unsigned check_mask = DIFF_FORMAT_NAME |
			      DIFF_FORMAT_NAME_STATUS |
			      DIFF_FORMAT_CHECKDIFF |
			      DIFF_FORMAT_NO_OUTPUT;
	/*
	 * This must be signed because we're comparing against a potentially
	 * negative value.
	 */
	const int hexsz = the_hash_algo->hexsz;

	if (options->set_default)
		options->set_default(options);

	if (HAS_MULTI_BITS(options->output_format & check_mask))
		die(_("--name-only, --name-status, --check and -s are mutually exclusive"));

	if (HAS_MULTI_BITS(options->pickaxe_opts & DIFF_PICKAXE_KINDS_MASK))
		die(_("-G, -S and --find-object are mutually exclusive"));

	/*
	 * Most of the time we can say "there are changes"
	 * only by checking if there are changed paths, but
	 * --ignore-whitespace* options force us to look
	 * inside contents.
	 */

	if ((options->xdl_opts & XDF_WHITESPACE_FLAGS))
		options->flags.diff_from_contents = 1;
	else
		options->flags.diff_from_contents = 0;

	if (options->flags.find_copies_harder)
		options->detect_rename = DIFF_DETECT_COPY;

	if (!options->flags.relative_name)
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
		options->flags.recursive = 1;
	/*
	 * Also pickaxe would not work very well if you do not say recursive
	 */
	if (options->pickaxe_opts & DIFF_PICKAXE_KINDS_MASK)
		options->flags.recursive = 1;
	/*
	 * When patches are generated, submodules diffed against the work tree
	 * must be checked for dirtiness too so it can be shown in the output
	 */
	if (options->output_format & DIFF_FORMAT_PATCH)
		options->flags.dirty_submodules = 1;

	if (options->detect_rename && options->rename_limit < 0)
		options->rename_limit = diff_rename_limit_default;
	if (hexsz < options->abbrev)
		options->abbrev = hexsz; /* full */

	/*
	 * It does not make sense to show the first hit we happened
	 * to have found.  It does not make sense not to return with
	 * exit code in such a case either.
	 */
	if (options->flags.quick) {
		options->output_format = DIFF_FORMAT_NO_OUTPUT;
		options->flags.exit_with_status = 1;
	}

	options->diff_path_counter = 0;

	if (options->flags.follow_renames && options->pathspec.nr != 1)
		die(_("--follow requires exactly one pathspec"));

	if (!options->use_color || external_diff())
		options->color_moved = 0;

	FREE_AND_NULL(options->parseopts);
}

int parse_long_opt(const char *opt, const char **argv,
		   const char **optarg)
{
	const char *arg = argv[0];
	if (!skip_prefix(arg, "--", &arg))
		return 0;
	if (!skip_prefix(arg, opt, &arg))
		return 0;
	if (*arg == '=') { /* stuck form: --option=value */
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

static int diff_opt_stat(const struct option *opt, const char *value, int unset)
{
	struct diff_options *options = opt->value;
	int width = options->stat_width;
	int name_width = options->stat_name_width;
	int graph_width = options->stat_graph_width;
	int count = options->stat_count;
	char *end;

	BUG_ON_OPT_NEG(unset);

	if (!strcmp(opt->long_name, "stat")) {
		if (value) {
			width = strtoul(value, &end, 10);
			if (*end == ',')
				name_width = strtoul(end+1, &end, 10);
			if (*end == ',')
				count = strtoul(end+1, &end, 10);
			if (*end)
				return error(_("invalid --stat value: %s"), value);
		}
	} else if (!strcmp(opt->long_name, "stat-width")) {
		width = strtoul(value, &end, 10);
		if (*end)
			return error(_("%s expects a numerical value"),
				     opt->long_name);
	} else if (!strcmp(opt->long_name, "stat-name-width")) {
		name_width = strtoul(value, &end, 10);
		if (*end)
			return error(_("%s expects a numerical value"),
				     opt->long_name);
	} else if (!strcmp(opt->long_name, "stat-graph-width")) {
		graph_width = strtoul(value, &end, 10);
		if (*end)
			return error(_("%s expects a numerical value"),
				     opt->long_name);
	} else if (!strcmp(opt->long_name, "stat-count")) {
		count = strtoul(value, &end, 10);
		if (*end)
			return error(_("%s expects a numerical value"),
				     opt->long_name);
	} else
		BUG("%s should not get here", opt->long_name);

	options->output_format |= DIFF_FORMAT_DIFFSTAT;
	options->stat_name_width = name_width;
	options->stat_graph_width = graph_width;
	options->stat_width = width;
	options->stat_count = count;
	return 0;
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

static const char diff_status_letters[] = {
	DIFF_STATUS_ADDED,
	DIFF_STATUS_COPIED,
	DIFF_STATUS_DELETED,
	DIFF_STATUS_MODIFIED,
	DIFF_STATUS_RENAMED,
	DIFF_STATUS_TYPE_CHANGED,
	DIFF_STATUS_UNKNOWN,
	DIFF_STATUS_UNMERGED,
	DIFF_STATUS_FILTER_AON,
	DIFF_STATUS_FILTER_BROKEN,
	'\0',
};

static unsigned int filter_bit['Z' + 1];

static void prepare_filter_bits(void)
{
	int i;

	if (!filter_bit[DIFF_STATUS_ADDED]) {
		for (i = 0; diff_status_letters[i]; i++)
			filter_bit[(int) diff_status_letters[i]] = (1 << i);
	}
}

static unsigned filter_bit_tst(char status, const struct diff_options *opt)
{
	return opt->filter & filter_bit[(int) status];
}

unsigned diff_filter_bit(char status)
{
	prepare_filter_bits();
	return filter_bit[(int) status];
}

static int diff_opt_diff_filter(const struct option *option,
				const char *optarg, int unset)
{
	struct diff_options *opt = option->value;
	int i, optch;

	BUG_ON_OPT_NEG(unset);
	prepare_filter_bits();

	/*
	 * If there is a negation e.g. 'd' in the input, and we haven't
	 * initialized the filter field with another --diff-filter, start
	 * from full set of bits, except for AON.
	 */
	if (!opt->filter) {
		for (i = 0; (optch = optarg[i]) != '\0'; i++) {
			if (optch < 'a' || 'z' < optch)
				continue;
			opt->filter = (1 << (ARRAY_SIZE(diff_status_letters) - 1)) - 1;
			opt->filter &= ~filter_bit[DIFF_STATUS_FILTER_AON];
			break;
		}
	}

	for (i = 0; (optch = optarg[i]) != '\0'; i++) {
		unsigned int bit;
		int negate;

		if ('a' <= optch && optch <= 'z') {
			negate = 1;
			optch = toupper(optch);
		} else {
			negate = 0;
		}

		bit = (0 <= optch && optch <= 'Z') ? filter_bit[optch] : 0;
		if (!bit)
			return error(_("unknown change class '%c' in --diff-filter=%s"),
				     optarg[i], optarg);
		if (negate)
			opt->filter &= ~bit;
		else
			opt->filter |= bit;
	}
	return 0;
}

static void enable_patch_output(int *fmt)
{
	*fmt &= ~DIFF_FORMAT_NO_OUTPUT;
	*fmt |= DIFF_FORMAT_PATCH;
}

static int diff_opt_ws_error_highlight(const struct option *option,
				       const char *arg, int unset)
{
	struct diff_options *opt = option->value;
	int val = parse_ws_error_highlight(arg);

	BUG_ON_OPT_NEG(unset);
	if (val < 0)
		return error(_("unknown value after ws-error-highlight=%.*s"),
			     -1 - val, arg);
	opt->ws_error_highlight = val;
	return 0;
}

static int diff_opt_find_object(const struct option *option,
				const char *arg, int unset)
{
	struct diff_options *opt = option->value;
	struct object_id oid;

	BUG_ON_OPT_NEG(unset);
	if (get_oid(arg, &oid))
		return error(_("unable to resolve '%s'"), arg);

	if (!opt->objfind)
		opt->objfind = xcalloc(1, sizeof(*opt->objfind));

	opt->pickaxe_opts |= DIFF_PICKAXE_KIND_OBJFIND;
	opt->flags.recursive = 1;
	opt->flags.tree_in_recursive = 1;
	oidset_insert(opt->objfind, &oid);
	return 0;
}

static int diff_opt_anchored(const struct option *opt,
			     const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->xdl_opts = DIFF_WITH_ALG(options, PATIENCE_DIFF);
	ALLOC_GROW(options->anchors, options->anchors_nr + 1,
		   options->anchors_alloc);
	options->anchors[options->anchors_nr++] = xstrdup(arg);
	return 0;
}

static int diff_opt_binary(const struct option *opt,
			   const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	enable_patch_output(&options->output_format);
	options->flags.binary = 1;
	return 0;
}

static int diff_opt_break_rewrites(const struct option *opt,
				   const char *arg, int unset)
{
	int *break_opt = opt->value;
	int opt1, opt2;

	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "";
	opt1 = parse_rename_score(&arg);
	if (*arg == 0)
		opt2 = 0;
	else if (*arg != '/')
		return error(_("%s expects <n>/<m> form"), opt->long_name);
	else {
		arg++;
		opt2 = parse_rename_score(&arg);
	}
	if (*arg != 0)
		return error(_("%s expects <n>/<m> form"), opt->long_name);
	*break_opt = opt1 | (opt2 << 16);
	return 0;
}

static int diff_opt_char(const struct option *opt,
			 const char *arg, int unset)
{
	char *value = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (arg[1])
		return error(_("%s expects a character, got '%s'"),
			     opt->long_name, arg);
	*value = arg[0];
	return 0;
}

static int diff_opt_color_moved(const struct option *opt,
				const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	if (unset) {
		options->color_moved = COLOR_MOVED_NO;
	} else if (!arg) {
		if (diff_color_moved_default)
			options->color_moved = diff_color_moved_default;
		if (options->color_moved == COLOR_MOVED_NO)
			options->color_moved = COLOR_MOVED_DEFAULT;
	} else {
		int cm = parse_color_moved(arg);
		if (cm < 0)
			return error(_("bad --color-moved argument: %s"), arg);
		options->color_moved = cm;
	}
	return 0;
}

static int diff_opt_color_moved_ws(const struct option *opt,
				   const char *arg, int unset)
{
	struct diff_options *options = opt->value;
	unsigned cm;

	if (unset) {
		options->color_moved_ws_handling = 0;
		return 0;
	}

	cm = parse_color_moved_ws(arg);
	if (cm & COLOR_MOVED_WS_ERROR)
		return error(_("invalid mode '%s' in --color-moved-ws"), arg);
	options->color_moved_ws_handling = cm;
	return 0;
}

static int diff_opt_color_words(const struct option *opt,
				const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->use_color = 1;
	options->word_diff = DIFF_WORDS_COLOR;
	options->word_regex = arg;
	return 0;
}

static int diff_opt_compact_summary(const struct option *opt,
				    const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_ARG(arg);
	if (unset) {
		options->flags.stat_with_summary = 0;
	} else {
		options->flags.stat_with_summary = 1;
		options->output_format |= DIFF_FORMAT_DIFFSTAT;
	}
	return 0;
}

static int diff_opt_diff_algorithm(const struct option *opt,
				   const char *arg, int unset)
{
	struct diff_options *options = opt->value;
	long value = parse_algorithm_value(arg);

	BUG_ON_OPT_NEG(unset);
	if (value < 0)
		return error(_("option diff-algorithm accepts \"myers\", "
			       "\"minimal\", \"patience\" and \"histogram\""));

	/* clear out previous settings */
	DIFF_XDL_CLR(options, NEED_MINIMAL);
	options->xdl_opts &= ~XDF_DIFF_ALGORITHM_MASK;
	options->xdl_opts |= value;
	return 0;
}

static int diff_opt_dirstat(const struct option *opt,
			    const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (!strcmp(opt->long_name, "cumulative")) {
		if (arg)
			BUG("how come --cumulative take a value?");
		arg = "cumulative";
	} else if (!strcmp(opt->long_name, "dirstat-by-file"))
		parse_dirstat_opt(options, "files");
	parse_dirstat_opt(options, arg ? arg : "");
	return 0;
}

static int diff_opt_find_copies(const struct option *opt,
				const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "";
	options->rename_score = parse_rename_score(&arg);
	if (*arg != 0)
		return error(_("invalid argument to %s"), opt->long_name);

	if (options->detect_rename == DIFF_DETECT_COPY)
		options->flags.find_copies_harder = 1;
	else
		options->detect_rename = DIFF_DETECT_COPY;

	return 0;
}

static int diff_opt_find_renames(const struct option *opt,
				 const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "";
	options->rename_score = parse_rename_score(&arg);
	if (*arg != 0)
		return error(_("invalid argument to %s"), opt->long_name);

	options->detect_rename = DIFF_DETECT_RENAME;
	return 0;
}

static int diff_opt_follow(const struct option *opt,
			   const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_ARG(arg);
	if (unset) {
		options->flags.follow_renames = 0;
		options->flags.default_follow_renames = 0;
	} else {
		options->flags.follow_renames = 1;
	}
	return 0;
}

static int diff_opt_ignore_submodules(const struct option *opt,
				      const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "all";
	options->flags.override_submodule_config = 1;
	handle_ignore_submodules_arg(options, arg);
	return 0;
}

static int diff_opt_line_prefix(const struct option *opt,
				const char *optarg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->line_prefix = optarg;
	options->line_prefix_length = strlen(options->line_prefix);
	graph_setup_line_prefix(options);
	return 0;
}

static int diff_opt_no_prefix(const struct option *opt,
			      const char *optarg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(optarg);
	options->a_prefix = "";
	options->b_prefix = "";
	return 0;
}

static enum parse_opt_result diff_opt_output(struct parse_opt_ctx_t *ctx,
					     const struct option *opt,
					     const char *arg, int unset)
{
	struct diff_options *options = opt->value;
	char *path;

	BUG_ON_OPT_NEG(unset);
	path = prefix_filename(ctx->prefix, arg);
	options->file = xfopen(path, "w");
	options->close_file = 1;
	if (options->use_color != GIT_COLOR_ALWAYS)
		options->use_color = GIT_COLOR_NEVER;
	free(path);
	return 0;
}

static int diff_opt_patience(const struct option *opt,
			     const char *arg, int unset)
{
	struct diff_options *options = opt->value;
	int i;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	options->xdl_opts = DIFF_WITH_ALG(options, PATIENCE_DIFF);
	/*
	 * Both --patience and --anchored use PATIENCE_DIFF
	 * internally, so remove any anchors previously
	 * specified.
	 */
	for (i = 0; i < options->anchors_nr; i++)
		free(options->anchors[i]);
	options->anchors_nr = 0;
	return 0;
}

static int diff_opt_pickaxe_regex(const struct option *opt,
				  const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->pickaxe = arg;
	options->pickaxe_opts |= DIFF_PICKAXE_KIND_G;
	return 0;
}

static int diff_opt_pickaxe_string(const struct option *opt,
				   const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->pickaxe = arg;
	options->pickaxe_opts |= DIFF_PICKAXE_KIND_S;
	return 0;
}

static int diff_opt_relative(const struct option *opt,
			     const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	options->flags.relative_name = 1;
	if (arg)
		options->prefix = arg;
	return 0;
}

static int diff_opt_submodule(const struct option *opt,
			      const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "log";
	if (parse_submodule_params(options, arg))
		return error(_("failed to parse --submodule option parameter: '%s'"),
			     arg);
	return 0;
}

static int diff_opt_textconv(const struct option *opt,
			     const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_ARG(arg);
	if (unset) {
		options->flags.allow_textconv = 0;
	} else {
		options->flags.allow_textconv = 1;
		options->flags.textconv_set_via_cmdline = 1;
	}
	return 0;
}

static int diff_opt_unified(const struct option *opt,
			    const char *arg, int unset)
{
	struct diff_options *options = opt->value;
	char *s;

	BUG_ON_OPT_NEG(unset);

	if (arg) {
		options->context = strtol(arg, &s, 10);
		if (*s)
			return error(_("%s expects a numerical value"), "--unified");
	}
	enable_patch_output(&options->output_format);

	return 0;
}

static int diff_opt_word_diff(const struct option *opt,
			      const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (arg) {
		if (!strcmp(arg, "plain"))
			options->word_diff = DIFF_WORDS_PLAIN;
		else if (!strcmp(arg, "color")) {
			options->use_color = 1;
			options->word_diff = DIFF_WORDS_COLOR;
		}
		else if (!strcmp(arg, "porcelain"))
			options->word_diff = DIFF_WORDS_PORCELAIN;
		else if (!strcmp(arg, "none"))
			options->word_diff = DIFF_WORDS_NONE;
		else
			return error(_("bad --word-diff argument: %s"), arg);
	} else {
		if (options->word_diff == DIFF_WORDS_NONE)
			options->word_diff = DIFF_WORDS_PLAIN;
	}
	return 0;
}

static int diff_opt_word_diff_regex(const struct option *opt,
				    const char *arg, int unset)
{
	struct diff_options *options = opt->value;

	BUG_ON_OPT_NEG(unset);
	if (options->word_diff == DIFF_WORDS_NONE)
		options->word_diff = DIFF_WORDS_PLAIN;
	options->word_regex = arg;
	return 0;
}

static void prep_parse_options(struct diff_options *options)
{
	struct option parseopts[] = {
		OPT_GROUP(N_("Diff output format options")),
		OPT_BITOP('p', "patch", &options->output_format,
			  N_("generate patch"),
			  DIFF_FORMAT_PATCH, DIFF_FORMAT_NO_OUTPUT),
		OPT_BIT_F('s', "no-patch", &options->output_format,
			  N_("suppress diff output"),
			  DIFF_FORMAT_NO_OUTPUT, PARSE_OPT_NONEG),
		OPT_BITOP('u', NULL, &options->output_format,
			  N_("generate patch"),
			  DIFF_FORMAT_PATCH, DIFF_FORMAT_NO_OUTPUT),
		OPT_CALLBACK_F('U', "unified", options, N_("<n>"),
			       N_("generate diffs with <n> lines context"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG, diff_opt_unified),
		OPT_BOOL('W', "function-context", &options->flags.funccontext,
			 N_("generate diffs with <n> lines context")),
		OPT_BIT_F(0, "raw", &options->output_format,
			  N_("generate the diff in raw format"),
			  DIFF_FORMAT_RAW, PARSE_OPT_NONEG),
		OPT_BITOP(0, "patch-with-raw", &options->output_format,
			  N_("synonym for '-p --raw'"),
			  DIFF_FORMAT_PATCH | DIFF_FORMAT_RAW,
			  DIFF_FORMAT_NO_OUTPUT),
		OPT_BITOP(0, "patch-with-stat", &options->output_format,
			  N_("synonym for '-p --stat'"),
			  DIFF_FORMAT_PATCH | DIFF_FORMAT_DIFFSTAT,
			  DIFF_FORMAT_NO_OUTPUT),
		OPT_BIT_F(0, "numstat", &options->output_format,
			  N_("machine friendly --stat"),
			  DIFF_FORMAT_NUMSTAT, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "shortstat", &options->output_format,
			  N_("output only the last line of --stat"),
			  DIFF_FORMAT_SHORTSTAT, PARSE_OPT_NONEG),
		OPT_CALLBACK_F('X', "dirstat", options, N_("<param1,param2>..."),
			       N_("output the distribution of relative amount of changes for each sub-directory"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_dirstat),
		OPT_CALLBACK_F(0, "cumulative", options, NULL,
			       N_("synonym for --dirstat=cumulative"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       diff_opt_dirstat),
		OPT_CALLBACK_F(0, "dirstat-by-file", options, N_("<param1,param2>..."),
			       N_("synonym for --dirstat=files,param1,param2..."),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_dirstat),
		OPT_BIT_F(0, "check", &options->output_format,
			  N_("warn if changes introduce conflict markers or whitespace errors"),
			  DIFF_FORMAT_CHECKDIFF, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "summary", &options->output_format,
			  N_("condensed summary such as creations, renames and mode changes"),
			  DIFF_FORMAT_SUMMARY, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "name-only", &options->output_format,
			  N_("show only names of changed files"),
			  DIFF_FORMAT_NAME, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "name-status", &options->output_format,
			  N_("show only names and status of changed files"),
			  DIFF_FORMAT_NAME_STATUS, PARSE_OPT_NONEG),
		OPT_CALLBACK_F(0, "stat", options, N_("<width>[,<name-width>[,<count>]]"),
			       N_("generate diffstat"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG, diff_opt_stat),
		OPT_CALLBACK_F(0, "stat-width", options, N_("<width>"),
			       N_("generate diffstat with a given width"),
			       PARSE_OPT_NONEG, diff_opt_stat),
		OPT_CALLBACK_F(0, "stat-name-width", options, N_("<width>"),
			       N_("generate diffstat with a given name width"),
			       PARSE_OPT_NONEG, diff_opt_stat),
		OPT_CALLBACK_F(0, "stat-graph-width", options, N_("<width>"),
			       N_("generate diffstat with a given graph width"),
			       PARSE_OPT_NONEG, diff_opt_stat),
		OPT_CALLBACK_F(0, "stat-count", options, N_("<count>"),
			       N_("generate diffstat with limited lines"),
			       PARSE_OPT_NONEG, diff_opt_stat),
		OPT_CALLBACK_F(0, "compact-summary", options, NULL,
			       N_("generate compact summary in diffstat"),
			       PARSE_OPT_NOARG, diff_opt_compact_summary),
		OPT_CALLBACK_F(0, "binary", options, NULL,
			       N_("output a binary diff that can be applied"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG, diff_opt_binary),
		OPT_BOOL(0, "full-index", &options->flags.full_index,
			 N_("show full pre- and post-image object names on the \"index\" lines")),
		OPT_COLOR_FLAG(0, "color", &options->use_color,
			       N_("show colored diff")),
		OPT_CALLBACK_F(0, "ws-error-highlight", options, N_("<kind>"),
			       N_("highlight whitespace errors in the 'context', 'old' or 'new' lines in the diff"),
			       PARSE_OPT_NONEG, diff_opt_ws_error_highlight),
		OPT_SET_INT('z', NULL, &options->line_termination,
			    N_("do not munge pathnames and use NULs as output field terminators in --raw or --numstat"),
			    0),
		OPT__ABBREV(&options->abbrev),
		OPT_STRING_F(0, "src-prefix", &options->a_prefix, N_("<prefix>"),
			     N_("show the given source prefix instead of \"a/\""),
			     PARSE_OPT_NONEG),
		OPT_STRING_F(0, "dst-prefix", &options->b_prefix, N_("<prefix>"),
			     N_("show the given destination prefix instead of \"b/\""),
			     PARSE_OPT_NONEG),
		OPT_CALLBACK_F(0, "line-prefix", options, N_("<prefix>"),
			       N_("prepend an additional prefix to every line of output"),
			       PARSE_OPT_NONEG, diff_opt_line_prefix),
		OPT_CALLBACK_F(0, "no-prefix", options, NULL,
			       N_("do not show any source or destination prefix"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG, diff_opt_no_prefix),
		OPT_INTEGER_F(0, "inter-hunk-context", &options->interhunkcontext,
			      N_("show context between diff hunks up to the specified number of lines"),
			      PARSE_OPT_NONEG),
		OPT_CALLBACK_F(0, "output-indicator-new",
			       &options->output_indicators[OUTPUT_INDICATOR_NEW],
			       N_("<char>"),
			       N_("specify the character to indicate a new line instead of '+'"),
			       PARSE_OPT_NONEG, diff_opt_char),
		OPT_CALLBACK_F(0, "output-indicator-old",
			       &options->output_indicators[OUTPUT_INDICATOR_OLD],
			       N_("<char>"),
			       N_("specify the character to indicate an old line instead of '-'"),
			       PARSE_OPT_NONEG, diff_opt_char),
		OPT_CALLBACK_F(0, "output-indicator-context",
			       &options->output_indicators[OUTPUT_INDICATOR_CONTEXT],
			       N_("<char>"),
			       N_("specify the character to indicate a context instead of ' '"),
			       PARSE_OPT_NONEG, diff_opt_char),

		OPT_GROUP(N_("Diff rename options")),
		OPT_CALLBACK_F('B', "break-rewrites", &options->break_opt, N_("<n>[/<m>]"),
			       N_("break complete rewrite changes into pairs of delete and create"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_break_rewrites),
		OPT_CALLBACK_F('M', "find-renames", options, N_("<n>"),
			       N_("detect renames"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_find_renames),
		OPT_SET_INT_F('D', "irreversible-delete", &options->irreversible_delete,
			      N_("omit the preimage for deletes"),
			      1, PARSE_OPT_NONEG),
		OPT_CALLBACK_F('C', "find-copies", options, N_("<n>"),
			       N_("detect copies"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_find_copies),
		OPT_BOOL(0, "find-copies-harder", &options->flags.find_copies_harder,
			 N_("use unmodified files as source to find copies")),
		OPT_SET_INT_F(0, "no-renames", &options->detect_rename,
			      N_("disable rename detection"),
			      0, PARSE_OPT_NONEG),
		OPT_BOOL(0, "rename-empty", &options->flags.rename_empty,
			 N_("use empty blobs as rename source")),
		OPT_CALLBACK_F(0, "follow", options, NULL,
			       N_("continue listing the history of a file beyond renames"),
			       PARSE_OPT_NOARG, diff_opt_follow),
		OPT_INTEGER('l', NULL, &options->rename_limit,
			    N_("prevent rename/copy detection if the number of rename/copy targets exceeds given limit")),

		OPT_GROUP(N_("Diff algorithm options")),
		OPT_BIT(0, "minimal", &options->xdl_opts,
			N_("produce the smallest possible diff"),
			XDF_NEED_MINIMAL),
		OPT_BIT_F('w', "ignore-all-space", &options->xdl_opts,
			  N_("ignore whitespace when comparing lines"),
			  XDF_IGNORE_WHITESPACE, PARSE_OPT_NONEG),
		OPT_BIT_F('b', "ignore-space-change", &options->xdl_opts,
			  N_("ignore changes in amount of whitespace"),
			  XDF_IGNORE_WHITESPACE_CHANGE, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "ignore-space-at-eol", &options->xdl_opts,
			  N_("ignore changes in whitespace at EOL"),
			  XDF_IGNORE_WHITESPACE_AT_EOL, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "ignore-cr-at-eol", &options->xdl_opts,
			  N_("ignore carrier-return at the end of line"),
			  XDF_IGNORE_CR_AT_EOL, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "ignore-blank-lines", &options->xdl_opts,
			  N_("ignore changes whose lines are all blank"),
			  XDF_IGNORE_BLANK_LINES, PARSE_OPT_NONEG),
		OPT_BIT(0, "indent-heuristic", &options->xdl_opts,
			N_("heuristic to shift diff hunk boundaries for easy reading"),
			XDF_INDENT_HEURISTIC),
		OPT_CALLBACK_F(0, "patience", options, NULL,
			       N_("generate diff using the \"patience diff\" algorithm"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       diff_opt_patience),
		OPT_BITOP(0, "histogram", &options->xdl_opts,
			  N_("generate diff using the \"histogram diff\" algorithm"),
			  XDF_HISTOGRAM_DIFF, XDF_DIFF_ALGORITHM_MASK),
		OPT_CALLBACK_F(0, "diff-algorithm", options, N_("<algorithm>"),
			       N_("choose a diff algorithm"),
			       PARSE_OPT_NONEG, diff_opt_diff_algorithm),
		OPT_CALLBACK_F(0, "anchored", options, N_("<text>"),
			       N_("generate diff using the \"anchored diff\" algorithm"),
			       PARSE_OPT_NONEG, diff_opt_anchored),
		OPT_CALLBACK_F(0, "word-diff", options, N_("<mode>"),
			       N_("show word diff, using <mode> to delimit changed words"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG, diff_opt_word_diff),
		OPT_CALLBACK_F(0, "word-diff-regex", options, N_("<regex>"),
			       N_("use <regex> to decide what a word is"),
			       PARSE_OPT_NONEG, diff_opt_word_diff_regex),
		OPT_CALLBACK_F(0, "color-words", options, N_("<regex>"),
			       N_("equivalent to --word-diff=color --word-diff-regex=<regex>"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG, diff_opt_color_words),
		OPT_CALLBACK_F(0, "color-moved", options, N_("<mode>"),
			       N_("moved lines of code are colored differently"),
			       PARSE_OPT_OPTARG, diff_opt_color_moved),
		OPT_CALLBACK_F(0, "color-moved-ws", options, N_("<mode>"),
			       N_("how white spaces are ignored in --color-moved"),
			       0, diff_opt_color_moved_ws),

		OPT_GROUP(N_("Other diff options")),
		OPT_CALLBACK_F(0, "relative", options, N_("<prefix>"),
			       N_("when run from subdir, exclude changes outside and show relative paths"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_relative),
		OPT_BOOL('a', "text", &options->flags.text,
			 N_("treat all files as text")),
		OPT_BOOL('R', NULL, &options->flags.reverse_diff,
			 N_("swap two inputs, reverse the diff")),
		OPT_BOOL(0, "exit-code", &options->flags.exit_with_status,
			 N_("exit with 1 if there were differences, 0 otherwise")),
		OPT_BOOL(0, "quiet", &options->flags.quick,
			 N_("disable all output of the program")),
		OPT_BOOL(0, "ext-diff", &options->flags.allow_external,
			 N_("allow an external diff helper to be executed")),
		OPT_CALLBACK_F(0, "textconv", options, NULL,
			       N_("run external text conversion filters when comparing binary files"),
			       PARSE_OPT_NOARG, diff_opt_textconv),
		OPT_CALLBACK_F(0, "ignore-submodules", options, N_("<when>"),
			       N_("ignore changes to submodules in the diff generation"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_ignore_submodules),
		OPT_CALLBACK_F(0, "submodule", options, N_("<format>"),
			       N_("specify how differences in submodules are shown"),
			       PARSE_OPT_NONEG | PARSE_OPT_OPTARG,
			       diff_opt_submodule),
		OPT_SET_INT_F(0, "ita-invisible-in-index", &options->ita_invisible_in_index,
			      N_("hide 'git add -N' entries from the index"),
			      1, PARSE_OPT_NONEG),
		OPT_SET_INT_F(0, "ita-visible-in-index", &options->ita_invisible_in_index,
			      N_("treat 'git add -N' entries as real in the index"),
			      0, PARSE_OPT_NONEG),
		OPT_CALLBACK_F('S', NULL, options, N_("<string>"),
			       N_("look for differences that change the number of occurrences of the specified string"),
			       0, diff_opt_pickaxe_string),
		OPT_CALLBACK_F('G', NULL, options, N_("<regex>"),
			       N_("look for differences that change the number of occurrences of the specified regex"),
			       0, diff_opt_pickaxe_regex),
		OPT_BIT_F(0, "pickaxe-all", &options->pickaxe_opts,
			  N_("show all changes in the changeset with -S or -G"),
			  DIFF_PICKAXE_ALL, PARSE_OPT_NONEG),
		OPT_BIT_F(0, "pickaxe-regex", &options->pickaxe_opts,
			  N_("treat <string> in -S as extended POSIX regular expression"),
			  DIFF_PICKAXE_REGEX, PARSE_OPT_NONEG),
		OPT_FILENAME('O', NULL, &options->orderfile,
			     N_("control the order in which files appear in the output")),
		OPT_CALLBACK_F(0, "find-object", options, N_("<object-id>"),
			       N_("look for differences that change the number of occurrences of the specified object"),
			       PARSE_OPT_NONEG, diff_opt_find_object),
		OPT_CALLBACK_F(0, "diff-filter", options, N_("[(A|C|D|M|R|T|U|X|B)...[*]]"),
			       N_("select files by diff type"),
			       PARSE_OPT_NONEG, diff_opt_diff_filter),
		{ OPTION_CALLBACK, 0, "output", options, N_("<file>"),
		  N_("Output to a specific file"),
		  PARSE_OPT_NONEG, NULL, 0, diff_opt_output },

		OPT_END()
	};

	ALLOC_ARRAY(options->parseopts, ARRAY_SIZE(parseopts));
	memcpy(options->parseopts, parseopts, sizeof(parseopts));
}

int diff_opt_parse(struct diff_options *options,
		   const char **av, int ac, const char *prefix)
{
	if (!prefix)
		prefix = "";

	ac = parse_options(ac, av, prefix, options->parseopts, NULL,
			   PARSE_OPT_KEEP_DASHDASH |
			   PARSE_OPT_KEEP_UNKNOWN |
			   PARSE_OPT_NO_INTERNAL_HELP |
			   PARSE_OPT_ONE_SHOT |
			   PARSE_OPT_STOP_AT_NON_OPTION);

	return ac;
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

struct diff_queue_struct diff_queued_diff;

void diff_q(struct diff_queue_struct *queue, struct diff_filepair *dp)
{
	ALLOC_GROW(queue->queue, queue->nr + 1, queue->alloc);
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

const char *diff_aligned_abbrev(const struct object_id *oid, int len)
{
	int abblen;
	const char *abbrev;

	/* Do we want all 40 hex characters? */
	if (len == the_hash_algo->hexsz)
		return oid_to_hex(oid);

	/* An abbreviated value is fine, possibly followed by an ellipsis. */
	abbrev = diff_abbrev_oid(oid, len);

	if (!print_sha1_ellipsis())
		return abbrev;

	abblen = strlen(abbrev);

	/*
	 * In well-behaved cases, where the abbreviated result is the
	 * same as the requested length, append three dots after the
	 * abbreviation (hence the whole logic is limited to the case
	 * where abblen < 37); when the actual abbreviated result is a
	 * bit longer than the requested length, we reduce the number
	 * of dots so that they match the well-behaved ones.  However,
	 * if the actual abbreviation is longer than the requested
	 * length by more than three, we give up on aligning, and add
	 * three dots anyway, to indicate that the output is not the
	 * full object name.  Yes, this may be suboptimal, but this
	 * appears only in "diff --raw --abbrev" output and it is not
	 * worth the effort to change it now.  Note that this would
	 * likely to work fine when the automatic sizing of default
	 * abbreviation length is used--we would be fed -1 in "len" in
	 * that case, and will end up always appending three-dots, but
	 * the automatic sizing is supposed to give abblen that ensures
	 * uniqueness across all objects (statistically speaking).
	 */
	if (abblen < the_hash_algo->hexsz - 3) {
		static char hex[GIT_MAX_HEXSZ + 1];
		if (len < abblen && abblen <= len + 2)
			xsnprintf(hex, sizeof(hex), "%s%.*s", abbrev, len+3-abblen, "..");
		else
			xsnprintf(hex, sizeof(hex), "%s...", abbrev);
		return hex;
	}

	return oid_to_hex(oid);
}

static void diff_flush_raw(struct diff_filepair *p, struct diff_options *opt)
{
	int line_termination = opt->line_termination;
	int inter_name_termination = line_termination ? '\t' : '\0';

	fprintf(opt->file, "%s", diff_line_prefix(opt));
	if (!(opt->output_format & DIFF_FORMAT_NAME_STATUS)) {
		fprintf(opt->file, ":%06o %06o %s ", p->one->mode, p->two->mode,
			diff_aligned_abbrev(&p->one->oid, opt->abbrev));
		fprintf(opt->file, "%s ",
			diff_aligned_abbrev(&p->two->oid, opt->abbrev));
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
	if (one->oid_valid && two->oid_valid &&
	    oideq(&one->oid, &two->oid) &&
	    !one->dirty_submodule && !two->dirty_submodule)
		return 1; /* no change */
	if (!one->oid_valid && !two->oid_valid)
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
		s->oid_valid ? oid_to_hex(&s->oid) : "");
	fprintf(stderr, "queue[%d] %s size %lu\n",
		x, one ? one : "",
		s->size);
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
		else if (!oideq(&p->one->oid, &p->two->oid) ||
			 p->one->mode != p->two->mode ||
			 p->one->dirty_submodule ||
			 p->two->dirty_submodule ||
			 is_null_oid(&p->one->oid))
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
		fprintf(opt->file, "%s", diff_line_prefix(opt));
		write_name_quoted(name_a, opt->file, opt->line_termination);
	}
}

static void show_file_mode_name(struct diff_options *opt, const char *newdelete, struct diff_filespec *fs)
{
	struct strbuf sb = STRBUF_INIT;
	if (fs->mode)
		strbuf_addf(&sb, " %s mode %06o ", newdelete, fs->mode);
	else
		strbuf_addf(&sb, " %s ", newdelete);

	quote_c_style(fs->path, &sb, NULL, 0);
	strbuf_addch(&sb, '\n');
	emit_diff_symbol(opt, DIFF_SYMBOL_SUMMARY,
			 sb.buf, sb.len, 0);
	strbuf_release(&sb);
}

static void show_mode_change(struct diff_options *opt, struct diff_filepair *p,
		int show_name)
{
	if (p->one->mode && p->two->mode && p->one->mode != p->two->mode) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addf(&sb, " mode change %06o => %06o",
			    p->one->mode, p->two->mode);
		if (show_name) {
			strbuf_addch(&sb, ' ');
			quote_c_style(p->two->path, &sb, NULL, 0);
		}
		strbuf_addch(&sb, '\n');
		emit_diff_symbol(opt, DIFF_SYMBOL_SUMMARY,
				 sb.buf, sb.len, 0);
		strbuf_release(&sb);
	}
}

static void show_rename_copy(struct diff_options *opt, const char *renamecopy,
		struct diff_filepair *p)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf names = STRBUF_INIT;

	pprint_rename(&names, p->one->path, p->two->path);
	strbuf_addf(&sb, " %s %s (%d%%)\n",
		    renamecopy, names.buf, similarity_index(p));
	strbuf_release(&names);
	emit_diff_symbol(opt, DIFF_SYMBOL_SUMMARY,
				 sb.buf, sb.len, 0);
	show_mode_change(opt, p, 0);
	strbuf_release(&sb);
}

static void diff_summary(struct diff_options *opt, struct diff_filepair *p)
{
	switch(p->status) {
	case DIFF_STATUS_DELETED:
		show_file_mode_name(opt, "delete", p->one);
		break;
	case DIFF_STATUS_ADDED:
		show_file_mode_name(opt, "create", p->two);
		break;
	case DIFF_STATUS_COPIED:
		show_rename_copy(opt, "copy", p);
		break;
	case DIFF_STATUS_RENAMED:
		show_rename_copy(opt, "rename", p);
		break;
	default:
		if (p->score) {
			struct strbuf sb = STRBUF_INIT;
			strbuf_addstr(&sb, " rewrite ");
			quote_c_style(p->two->path, &sb, NULL, 0);
			strbuf_addf(&sb, " (%d%%)\n", similarity_index(p));
			emit_diff_symbol(opt, DIFF_SYMBOL_SUMMARY,
					 sb.buf, sb.len, 0);
			strbuf_release(&sb);
		}
		show_mode_change(opt, p, !p->score);
		break;
	}
}

struct patch_id_t {
	git_hash_ctx *ctx;
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

void flush_one_hunk(struct object_id *result, git_hash_ctx *ctx)
{
	unsigned char hash[GIT_MAX_RAWSZ];
	unsigned short carry = 0;
	int i;

	the_hash_algo->final_fn(hash, ctx);
	the_hash_algo->init_fn(ctx);
	/* 20-byte sum, with carry */
	for (i = 0; i < the_hash_algo->rawsz; ++i) {
		carry += result->hash[i] + hash[i];
		result->hash[i] = carry;
		carry >>= 8;
	}
}

static void patch_id_consume(void *priv, char *line, unsigned long len)
{
	struct patch_id_t *data = priv;
	int new_len;

	new_len = remove_space(line, len);

	the_hash_algo->update_fn(data->ctx, line, new_len);
	data->patchlen += new_len;
}

static void patch_id_add_string(git_hash_ctx *ctx, const char *str)
{
	the_hash_algo->update_fn(ctx, str, strlen(str));
}

static void patch_id_add_mode(git_hash_ctx *ctx, unsigned mode)
{
	/* large enough for 2^32 in octal */
	char buf[12];
	int len = xsnprintf(buf, sizeof(buf), "%06o", mode);
	the_hash_algo->update_fn(ctx, buf, len);
}

/* returns 0 upon success, and writes result into oid */
static int diff_get_patch_id(struct diff_options *options, struct object_id *oid, int diff_header_only, int stable)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	git_hash_ctx ctx;
	struct patch_id_t data;

	the_hash_algo->init_fn(&ctx);
	memset(&data, 0, sizeof(struct patch_id_t));
	data.ctx = &ctx;
	oidclr(oid);

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

		diff_fill_oid_info(p->one, options->repo->index);
		diff_fill_oid_info(p->two, options->repo->index);

		len1 = remove_space(p->one->path, strlen(p->one->path));
		len2 = remove_space(p->two->path, strlen(p->two->path));
		patch_id_add_string(&ctx, "diff--git");
		patch_id_add_string(&ctx, "a/");
		the_hash_algo->update_fn(&ctx, p->one->path, len1);
		patch_id_add_string(&ctx, "b/");
		the_hash_algo->update_fn(&ctx, p->two->path, len2);

		if (p->one->mode == 0) {
			patch_id_add_string(&ctx, "newfilemode");
			patch_id_add_mode(&ctx, p->two->mode);
			patch_id_add_string(&ctx, "---/dev/null");
			patch_id_add_string(&ctx, "+++b/");
			the_hash_algo->update_fn(&ctx, p->two->path, len2);
		} else if (p->two->mode == 0) {
			patch_id_add_string(&ctx, "deletedfilemode");
			patch_id_add_mode(&ctx, p->one->mode);
			patch_id_add_string(&ctx, "---a/");
			the_hash_algo->update_fn(&ctx, p->one->path, len1);
			patch_id_add_string(&ctx, "+++/dev/null");
		} else {
			patch_id_add_string(&ctx, "---a/");
			the_hash_algo->update_fn(&ctx, p->one->path, len1);
			patch_id_add_string(&ctx, "+++b/");
			the_hash_algo->update_fn(&ctx, p->two->path, len2);
		}

		if (diff_header_only)
			continue;

		if (fill_mmfile(options->repo, &mf1, p->one) < 0 ||
		    fill_mmfile(options->repo, &mf2, p->two) < 0)
			return error("unable to read files to diff");

		if (diff_filespec_is_binary(options->repo, p->one) ||
		    diff_filespec_is_binary(options->repo, p->two)) {
			the_hash_algo->update_fn(&ctx, oid_to_hex(&p->one->oid),
					the_hash_algo->hexsz);
			the_hash_algo->update_fn(&ctx, oid_to_hex(&p->two->oid),
					the_hash_algo->hexsz);
			continue;
		}

		xpp.flags = 0;
		xecfg.ctxlen = 3;
		xecfg.flags = 0;
		if (xdi_diff_outf(&mf1, &mf2, discard_hunk_line,
				  patch_id_consume, &data, &xpp, &xecfg))
			return error("unable to generate patch-id diff for %s",
				     p->one->path);

		if (stable)
			flush_one_hunk(oid, &ctx);
	}

	if (!stable)
		the_hash_algo->final_fn(oid->hash, &ctx);

	return 0;
}

int diff_flush_patch_id(struct diff_options *options, struct object_id *oid, int diff_header_only, int stable)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	int result = diff_get_patch_id(options, oid, diff_header_only, stable);

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
N_("inexact rename detection was skipped due to too many files.");

static const char degrade_cc_to_c_warning[] =
N_("only found copies from modified paths due to too many files.");

static const char rename_limit_advice[] =
N_("you may want to set your %s variable to at least "
   "%d and retry the command.");

void diff_warn_rename_limit(const char *varname, int needed, int degraded_cc)
{
	fflush(stdout);
	if (degraded_cc)
		warning(_(degrade_cc_to_c_warning));
	else if (needed)
		warning(_(rename_limit_warning));
	else
		return;
	if (0 < needed)
		warning(_(rename_limit_advice), varname, needed);
}

static void diff_flush_patch_all_file_pairs(struct diff_options *o)
{
	int i;
	static struct emitted_diff_symbols esm = EMITTED_DIFF_SYMBOLS_INIT;
	struct diff_queue_struct *q = &diff_queued_diff;

	if (WSEH_NEW & WS_RULE_MASK)
		BUG("WS rules bit mask overlaps with diff symbol flags");

	if (o->color_moved)
		o->emitted_symbols = &esm;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (check_pair_status(p))
			diff_flush_patch(p, o);
	}

	if (o->emitted_symbols) {
		if (o->color_moved) {
			struct hashmap add_lines, del_lines;

			if (o->color_moved_ws_handling &
			    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE)
				o->color_moved_ws_handling |= XDF_IGNORE_WHITESPACE;

			hashmap_init(&del_lines, moved_entry_cmp, o, 0);
			hashmap_init(&add_lines, moved_entry_cmp, o, 0);

			add_lines_to_move_detection(o, &add_lines, &del_lines);
			mark_color_as_moved(o, &add_lines, &del_lines);
			if (o->color_moved == COLOR_MOVED_ZEBRA_DIM)
				dim_moved_lines(o);

			hashmap_free_entries(&add_lines, struct moved_entry,
						ent);
			hashmap_free_entries(&del_lines, struct moved_entry,
						ent);
		}

		for (i = 0; i < esm.nr; i++)
			emit_diff_symbol_from_struct(o, &esm.buf[i]);

		for (i = 0; i < esm.nr; i++)
			free((void *)esm.buf[i].line);
		esm.nr = 0;

		o->emitted_symbols = NULL;
	}
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

	if (output_format & DIFF_FORMAT_DIRSTAT && options->flags.dirstat_by_line)
		dirstat_by_line = 1;

	if (output_format & (DIFF_FORMAT_DIFFSTAT|DIFF_FORMAT_SHORTSTAT|DIFF_FORMAT_NUMSTAT) ||
	    dirstat_by_line) {
		struct diffstat_t diffstat;

		compute_diffstat(options, &diffstat, q);
		if (output_format & DIFF_FORMAT_NUMSTAT)
			show_numstat(&diffstat, options);
		if (output_format & DIFF_FORMAT_DIFFSTAT)
			show_stats(&diffstat, options);
		if (output_format & DIFF_FORMAT_SHORTSTAT)
			show_shortstats(&diffstat, options);
		if (output_format & DIFF_FORMAT_DIRSTAT && dirstat_by_line)
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
	    options->flags.exit_with_status &&
	    options->flags.diff_from_contents) {
		/*
		 * run diff_flush_patch for the exit status. setting
		 * options->file to /dev/null should be safe, because we
		 * aren't supposed to produce any output anyway.
		 */
		if (options->close_file)
			fclose(options->file);
		options->file = xfopen("/dev/null", "w");
		options->close_file = 1;
		options->color_moved = 0;
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
			emit_diff_symbol(options, DIFF_SYMBOL_SEPARATOR, NULL, 0, 0);
			if (options->stat_sep)
				/* attach patch instead of inline */
				emit_diff_symbol(options, DIFF_SYMBOL_STAT_SEP,
						 NULL, 0, 0);
		}

		diff_flush_patch_all_file_pairs(options);
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
	if (options->flags.diff_from_contents) {
		if (options->found_changes)
			options->flags.has_changes = 1;
		else
			options->flags.has_changes = 0;
	}
}

static int match_filter(const struct diff_options *options, const struct diff_filepair *p)
{
	return (((p->status == DIFF_STATUS_MODIFIED) &&
		 ((p->score &&
		   filter_bit_tst(DIFF_STATUS_FILTER_BROKEN, options)) ||
		  (!p->score &&
		   filter_bit_tst(DIFF_STATUS_MODIFIED, options)))) ||
		((p->status != DIFF_STATUS_MODIFIED) &&
		 filter_bit_tst(p->status, options)));
}

static void diffcore_apply_filter(struct diff_options *options)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;

	DIFF_QUEUE_CLEAR(&outq);

	if (!options->filter)
		return;

	if (filter_bit_tst(DIFF_STATUS_FILTER_AON, options)) {
		int found;
		for (i = found = 0; !found && i < q->nr; i++) {
			if (match_filter(options, q->queue[i]))
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
			if (match_filter(options, p))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}
	free(q->queue);
	*q = outq;
}

/* Check whether two filespecs with the same mode and size are identical */
static int diff_filespec_is_identical(struct repository *r,
				      struct diff_filespec *one,
				      struct diff_filespec *two)
{
	if (S_ISGITLINK(one->mode))
		return 0;
	if (diff_populate_filespec(r, one, 0))
		return 0;
	if (diff_populate_filespec(r, two, 0))
		return 0;
	return !memcmp(one->data, two->data, one->size);
}

static int diff_filespec_check_stat_unmatch(struct repository *r,
					    struct diff_filepair *p)
{
	if (p->done_skip_stat_unmatch)
		return p->skip_stat_unmatch_result;

	p->done_skip_stat_unmatch = 1;
	p->skip_stat_unmatch_result = 0;
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
	    (p->one->oid_valid && p->two->oid_valid) ||
	    (p->one->mode != p->two->mode) ||
	    diff_populate_filespec(r, p->one, CHECK_SIZE_ONLY) ||
	    diff_populate_filespec(r, p->two, CHECK_SIZE_ONLY) ||
	    (p->one->size != p->two->size) ||
	    !diff_filespec_is_identical(r, p->one, p->two)) /* (2) */
		p->skip_stat_unmatch_result = 1;
	return p->skip_stat_unmatch_result;
}

static void diffcore_skip_stat_unmatch(struct diff_options *diffopt)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	DIFF_QUEUE_CLEAR(&outq);

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];

		if (diff_filespec_check_stat_unmatch(diffopt->repo, p))
			diff_q(&outq, p);
		else {
			/*
			 * The caller can subtract 1 from skip_stat_unmatch
			 * to determine how many paths were dirty only
			 * due to stat info mismatch.
			 */
			if (!diffopt->flags.no_index)
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

void diffcore_fix_diff_index(void)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	QSORT(q->queue, q->nr, diffnamecmp);
}

static void add_if_missing(struct repository *r,
			   struct oid_array *to_fetch,
			   const struct diff_filespec *filespec)
{
	if (filespec && filespec->oid_valid &&
	    !S_ISGITLINK(filespec->mode) &&
	    oid_object_info_extended(r, &filespec->oid, NULL,
				     OBJECT_INFO_FOR_PREFETCH))
		oid_array_append(to_fetch, &filespec->oid);
}

void diffcore_std(struct diff_options *options)
{
	if (options->repo == the_repository && has_promisor_remote()) {
		/*
		 * Prefetch the diff pairs that are about to be flushed.
		 */
		int i;
		struct diff_queue_struct *q = &diff_queued_diff;
		struct oid_array to_fetch = OID_ARRAY_INIT;

		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			add_if_missing(options->repo, &to_fetch, p->one);
			add_if_missing(options->repo, &to_fetch, p->two);
		}
		if (to_fetch.nr)
			/*
			 * NEEDSWORK: Consider deduplicating the OIDs sent.
			 */
			promisor_remote_get_direct(options->repo,
						   to_fetch.oid, to_fetch.nr);
		oid_array_clear(&to_fetch);
	}

	/* NOTE please keep the following in sync with diff_tree_combined() */
	if (options->skip_stat_unmatch)
		diffcore_skip_stat_unmatch(options);
	if (!options->found_follow) {
		/* See try_to_follow_renames() in tree-diff.c */
		if (options->break_opt != -1)
			diffcore_break(options->repo,
				       options->break_opt);
		if (options->detect_rename)
			diffcore_rename(options);
		if (options->break_opt != -1)
			diffcore_merge_broken();
	}
	if (options->pickaxe_opts & DIFF_PICKAXE_KINDS_MASK)
		diffcore_pickaxe(options);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	if (!options->found_follow)
		/* See try_to_follow_renames() in tree-diff.c */
		diff_resolve_rename_copy();
	diffcore_apply_filter(options);

	if (diff_queued_diff.nr && !options->flags.diff_from_contents)
		options->flags.has_changes = 1;
	else
		options->flags.has_changes = 0;

	options->found_follow = 0;
}

int diff_result_code(struct diff_options *opt, int status)
{
	int result = 0;

	diff_warn_rename_limit("diff.renameLimit",
			       opt->needed_rename_limit,
			       opt->degraded_cc_to_c);
	if (!opt->flags.exit_with_status &&
	    !(opt->output_format & DIFF_FORMAT_CHECKDIFF))
		return status;
	if (opt->flags.exit_with_status &&
	    opt->flags.has_changes)
		result |= 01;
	if ((opt->output_format & DIFF_FORMAT_CHECKDIFF) &&
	    opt->flags.check_failed)
		result |= 02;
	return result;
}

int diff_can_quit_early(struct diff_options *opt)
{
	return (opt->flags.quick &&
		!opt->filter &&
		opt->flags.has_changes);
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
	struct diff_flags orig_flags = options->flags;
	if (!options->flags.override_submodule_config)
		set_diffopt_flags_from_submodule_config(options, path);
	if (options->flags.ignore_submodules)
		ignored = 1;
	options->flags = orig_flags;
	return ignored;
}

void compute_diffstat(struct diff_options *options,
		      struct diffstat_t *diffstat,
		      struct diff_queue_struct *q)
{
	int i;

	memset(diffstat, 0, sizeof(struct diffstat_t));
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (check_pair_status(p))
			diff_flush_stat(p, options, diffstat);
	}
}

void diff_addremove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const struct object_id *oid,
		    int oid_valid,
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
	if (options->flags.reverse_diff)
		addremove = (addremove == '+' ? '-' :
			     addremove == '-' ? '+' : addremove);

	if (options->prefix &&
	    strncmp(concatpath, options->prefix, options->prefix_length))
		return;

	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);

	if (addremove != '+')
		fill_filespec(one, oid, oid_valid, mode);
	if (addremove != '-') {
		fill_filespec(two, oid, oid_valid, mode);
		two->dirty_submodule = dirty_submodule;
	}

	diff_queue(&diff_queued_diff, one, two);
	if (!options->flags.diff_from_contents)
		options->flags.has_changes = 1;
}

void diff_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const struct object_id *old_oid,
		 const struct object_id *new_oid,
		 int old_oid_valid, int new_oid_valid,
		 const char *concatpath,
		 unsigned old_dirty_submodule, unsigned new_dirty_submodule)
{
	struct diff_filespec *one, *two;
	struct diff_filepair *p;

	if (S_ISGITLINK(old_mode) && S_ISGITLINK(new_mode) &&
	    is_submodule_ignored(concatpath, options))
		return;

	if (options->flags.reverse_diff) {
		SWAP(old_mode, new_mode);
		SWAP(old_oid, new_oid);
		SWAP(old_oid_valid, new_oid_valid);
		SWAP(old_dirty_submodule, new_dirty_submodule);
	}

	if (options->prefix &&
	    strncmp(concatpath, options->prefix, options->prefix_length))
		return;

	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);
	fill_filespec(one, old_oid, old_oid_valid, old_mode);
	fill_filespec(two, new_oid, new_oid_valid, new_mode);
	one->dirty_submodule = old_dirty_submodule;
	two->dirty_submodule = new_dirty_submodule;
	p = diff_queue(&diff_queued_diff, one, two);

	if (options->flags.diff_from_contents)
		return;

	if (options->flags.quick && options->skip_stat_unmatch &&
	    !diff_filespec_check_stat_unmatch(options->repo, p))
		return;

	options->flags.has_changes = 1;
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

static char *run_textconv(struct repository *r,
			  const char *pgm,
			  struct diff_filespec *spec,
			  size_t *outsize)
{
	struct diff_tempfile *temp;
	const char *argv[3];
	const char **arg = argv;
	struct child_process child = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	int err = 0;

	temp = prepare_temp_file(r, spec->path, spec);
	*arg++ = pgm;
	*arg++ = temp->name;
	*arg = NULL;

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

size_t fill_textconv(struct repository *r,
		     struct userdiff_driver *driver,
		     struct diff_filespec *df,
		     char **outbuf)
{
	size_t size;

	if (!driver) {
		if (!DIFF_FILE_VALID(df)) {
			*outbuf = "";
			return 0;
		}
		if (diff_populate_filespec(r, df, 0))
			die("unable to read files to diff");
		*outbuf = df->data;
		return df->size;
	}

	if (!driver->textconv)
		BUG("fill_textconv called with non-textconv driver");

	if (driver->textconv_cache && df->oid_valid) {
		*outbuf = notes_cache_get(driver->textconv_cache,
					  &df->oid,
					  &size);
		if (*outbuf)
			return size;
	}

	*outbuf = run_textconv(r, driver->textconv, df, &size);
	if (!*outbuf)
		die("unable to read files to diff");

	if (driver->textconv_cache && df->oid_valid) {
		/* ignore errors, as we might be in a readonly repository */
		notes_cache_put(driver->textconv_cache, &df->oid, *outbuf,
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

int textconv_object(struct repository *r,
		    const char *path,
		    unsigned mode,
		    const struct object_id *oid,
		    int oid_valid,
		    char **buf,
		    unsigned long *buf_size)
{
	struct diff_filespec *df;
	struct userdiff_driver *textconv;

	df = alloc_filespec(path);
	fill_filespec(df, oid, oid_valid, mode);
	textconv = get_textconv(r, df);
	if (!textconv) {
		free_filespec(df);
		return 0;
	}

	*buf_size = fill_textconv(r, textconv, df, buf);
	free_filespec(df);
	return 1;
}

void setup_diff_pager(struct diff_options *opt)
{
	/*
	 * If the user asked for our exit code, then either they want --quiet
	 * or --exit-code. We should definitely not bother with a pager in the
	 * former case, as we will generate no output. Since we still properly
	 * report our exit code even when a pager is run, we _could_ run a
	 * pager with --exit-code. But since we have not done so historically,
	 * and because it is easy to find people oneline advising "git diff
	 * --exit-code" in hooks and other scripts, we do not do so.
	 */
	if (!opt->flags.exit_with_status &&
	    check_pager_config("diff") != 0)
		setup_pager();
}
