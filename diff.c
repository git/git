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
#include "help.h"

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

static NORETURN void die_want_option(const char *option_name)
{
	die(_("option '%s' requires a value"), option_name);
}

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

static int parse_color_moved_ws(const char *arg)
{
	int ret = 0;
	struct string_list l = STRING_LIST_INIT_DUP;
	struct string_list_item *i;

	string_list_split(&l, arg, ',', -1);

	for_each_string_list_item(i, &l) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addstr(&sb, i->string);
		strbuf_trim(&sb);

		if (!strcmp(sb.buf, "ignore-space-change"))
			ret |= XDF_IGNORE_WHITESPACE_CHANGE;
		else if (!strcmp(sb.buf, "ignore-space-at-eol"))
			ret |= XDF_IGNORE_WHITESPACE_AT_EOL;
		else if (!strcmp(sb.buf, "ignore-all-space"))
			ret |= XDF_IGNORE_WHITESPACE;
		else if (!strcmp(sb.buf, "allow-indentation-change"))
			ret |= COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE;
		else
			error(_("ignoring unknown color-moved-ws mode '%s'"), sb.buf);

		strbuf_release(&sb);
	}

	if ((ret & COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE) &&
	    (ret & XDF_WHITESPACE_FLAGS))
		die(_("color-moved-ws: allow-indentation-change cannot be combined with other white space modes"));

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
		int cm = parse_color_moved_ws(value);
		if (cm < 0)
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

	if (!strcmp(var, "diff.wserrorhighlight")) {
		int val = parse_ws_error_highlight(value);
		if (val < 0)
			return -1;
		ws_error_highlight_default = val;
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
	external_diff_cmd = getenv("GIT_EXTERNAL_DIFF");
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
	diff_populate_filespec(one, CHECK_SIZE_ONLY);
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
	struct ws_delta *wsd;
};

/**
 * The struct ws_delta holds white space differences between moved lines, i.e.
 * between '+' and '-' lines that have been detected to be a move.
 * The string contains the difference in leading white spaces, before the
 * rest of the line is compared using the white space config for move
 * coloring. The current_longer indicates if the first string in the
 * comparision is longer than the second.
 */
struct ws_delta {
	char *string;
	unsigned int current_longer : 1;
};
#define WS_DELTA_INIT { NULL, 0 }

static int compute_ws_delta(const struct emitted_diff_symbol *a,
			     const struct emitted_diff_symbol *b,
			     struct ws_delta *out)
{
	const struct emitted_diff_symbol *longer =  a->len > b->len ? a : b;
	const struct emitted_diff_symbol *shorter = a->len > b->len ? b : a;
	int d = longer->len - shorter->len;

	out->string = xmemdupz(longer->line, d);
	out->current_longer = (a == longer);

	return !strncmp(longer->line + d, shorter->line, shorter->len);
}

static int cmp_in_block_with_wsd(const struct diff_options *o,
				 const struct moved_entry *cur,
				 const struct moved_entry *match,
				 struct moved_entry *pmb,
				 int n)
{
	struct emitted_diff_symbol *l = &o->emitted_symbols->buf[n];
	int al = cur->es->len, cl = l->len;
	const char *a = cur->es->line,
		   *b = match->es->line,
		   *c = l->line;

	int wslen;

	/*
	 * We need to check if 'cur' is equal to 'match'.
	 * As those are from the same (+/-) side, we do not need to adjust for
	 * indent changes. However these were found using fuzzy matching
	 * so we do have to check if they are equal.
	 */
	if (strcmp(a, b))
		return 1;

	if (!pmb->wsd)
		/*
		 * No white space delta was carried forward? This can happen
		 * when we exit early in this function and do not carry
		 * forward ws.
		 */
		return 1;

	/*
	 * The indent changes of the block are known and carried forward in
	 * pmb->wsd; however we need to check if the indent changes of the
	 * current line are still the same as before.
	 *
	 * To do so we need to compare 'l' to 'cur', adjusting the
	 * one of them for the white spaces, depending which was longer.
	 */

	wslen = strlen(pmb->wsd->string);
	if (pmb->wsd->current_longer) {
		c += wslen;
		cl -= wslen;
	} else {
		a += wslen;
		al -= wslen;
	}

	if (strcmp(a, c))
		return 1;

	return 0;
}

static int moved_entry_cmp(const void *hashmap_cmp_fn_data,
			   const void *entry,
			   const void *entry_or_key,
			   const void *keydata)
{
	const struct diff_options *diffopt = hashmap_cmp_fn_data;
	const struct moved_entry *a = entry;
	const struct moved_entry *b = entry_or_key;
	unsigned flags = diffopt->color_moved_ws_handling
			 & XDF_WHITESPACE_FLAGS;

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

	ret->ent.hash = xdiff_hash_string(l->line, l->len, flags);
	ret->es = l;
	ret->next_line = NULL;
	ret->wsd = NULL;

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

		key = prepare_entry(o, n);
		if (prev_line && prev_line->es->s == o->emitted_symbols->buf[n].s)
			prev_line->next_line = key;

		hashmap_add(hm, key);
		prev_line = key;
	}
}

static void pmb_advance_or_null(struct diff_options *o,
				struct moved_entry *match,
				struct hashmap *hm,
				struct moved_entry **pmb,
				int pmb_nr)
{
	int i;
	for (i = 0; i < pmb_nr; i++) {
		struct moved_entry *prev = pmb[i];
		struct moved_entry *cur = (prev && prev->next_line) ?
				prev->next_line : NULL;
		if (cur && !hm->cmpfn(o, cur, match, NULL)) {
			pmb[i] = cur;
		} else {
			pmb[i] = NULL;
		}
	}
}

static void pmb_advance_or_null_multi_match(struct diff_options *o,
					    struct moved_entry *match,
					    struct hashmap *hm,
					    struct moved_entry **pmb,
					    int pmb_nr, int n)
{
	int i;
	char *got_match = xcalloc(1, pmb_nr);

	for (; match; match = hashmap_get_next(hm, match)) {
		for (i = 0; i < pmb_nr; i++) {
			struct moved_entry *prev = pmb[i];
			struct moved_entry *cur = (prev && prev->next_line) ?
					prev->next_line : NULL;
			if (!cur)
				continue;
			if (!cmp_in_block_with_wsd(o, cur, match, pmb[i], n))
				got_match[i] |= 1;
		}
	}

	for (i = 0; i < pmb_nr; i++) {
		if (got_match[i]) {
			/* Carry the white space delta forward */
			pmb[i]->next_line->wsd = pmb[i]->wsd;
			pmb[i] = pmb[i]->next_line;
		} else
			pmb[i] = NULL;
	}
}

static int shrink_potential_moved_blocks(struct moved_entry **pmb,
					 int pmb_nr)
{
	int lp, rp;

	/* Shrink the set of potential block to the remaining running */
	for (lp = 0, rp = pmb_nr - 1; lp <= rp;) {
		while (lp < pmb_nr && pmb[lp])
			lp++;
		/* lp points at the first NULL now */

		while (rp > -1 && !pmb[rp])
			rp--;
		/* rp points at the last non-NULL */

		if (lp < pmb_nr && rp > -1 && lp < rp) {
			pmb[lp] = pmb[rp];
			if (pmb[rp]->wsd) {
				free(pmb[rp]->wsd->string);
				FREE_AND_NULL(pmb[rp]->wsd);
			}
			pmb[rp] = NULL;
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
 * NEEDSWORK: This uses the same heuristic as blame_entry_score() in blame.c.
 * Think of a way to unify them.
 */
static void adjust_last_block(struct diff_options *o, int n, int block_length)
{
	int i, alnum_count = 0;
	if (o->color_moved == COLOR_MOVED_PLAIN)
		return;
	for (i = 1; i < block_length + 1; i++) {
		const char *c = o->emitted_symbols->buf[n - i].line;
		for (; *c; c++) {
			if (!isalnum(*c))
				continue;
			alnum_count++;
			if (alnum_count >= COLOR_MOVED_MIN_ALNUM_COUNT)
				return;
		}
	}
	for (i = 1; i < block_length + 1; i++)
		o->emitted_symbols->buf[n - i].flags &= ~DIFF_SYMBOL_MOVED_LINE;
}

/* Find blocks of moved code, delegate actual coloring decision to helper */
static void mark_color_as_moved(struct diff_options *o,
				struct hashmap *add_lines,
				struct hashmap *del_lines)
{
	struct moved_entry **pmb = NULL; /* potentially moved blocks */
	int pmb_nr = 0, pmb_alloc = 0;
	int n, flipped_block = 1, block_length = 0;


	for (n = 0; n < o->emitted_symbols->nr; n++) {
		struct hashmap *hm = NULL;
		struct moved_entry *key;
		struct moved_entry *match = NULL;
		struct emitted_diff_symbol *l = &o->emitted_symbols->buf[n];

		switch (l->s) {
		case DIFF_SYMBOL_PLUS:
			hm = del_lines;
			key = prepare_entry(o, n);
			match = hashmap_get(hm, key, NULL);
			free(key);
			break;
		case DIFF_SYMBOL_MINUS:
			hm = add_lines;
			key = prepare_entry(o, n);
			match = hashmap_get(hm, key, NULL);
			free(key);
			break;
		default:
			flipped_block = 1;
		}

		if (!match) {
			adjust_last_block(o, n, block_length);
			pmb_nr = 0;
			block_length = 0;
			continue;
		}

		l->flags |= DIFF_SYMBOL_MOVED_LINE;

		if (o->color_moved == COLOR_MOVED_PLAIN)
			continue;

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
			for (; match; match = hashmap_get_next(hm, match)) {
				ALLOC_GROW(pmb, pmb_nr + 1, pmb_alloc);
				if (o->color_moved_ws_handling &
				    COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE) {
					struct ws_delta *wsd = xmalloc(sizeof(*match->wsd));
					if (compute_ws_delta(l, match->es, wsd)) {
						match->wsd = wsd;
						pmb[pmb_nr++] = match;
					} else
						free(wsd);
				} else {
					pmb[pmb_nr++] = match;
				}
			}

			flipped_block = (flipped_block + 1) % 2;

			adjust_last_block(o, n, block_length);
			block_length = 0;
		}

		block_length++;

		if (flipped_block && o->color_moved != COLOR_MOVED_BLOCKS)
			l->flags |= DIFF_SYMBOL_MOVED_LINE_ALT;
	}
	adjust_last_block(o, n, block_length);

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
				char sign, const char *line, int len,
				unsigned ws_rule, int blank_at_eof)
{
	const char *ws = NULL;

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
				    o->output_indicators[OUTPUT_INDICATOR_CONTEXT],
				    line, len,
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
				    o->output_indicators[OUTPUT_INDICATOR_NEW],
				    line, len,
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
				    o->output_indicators[OUTPUT_INDICATOR_OLD],
				    line, len,
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
	struct emitted_diff_symbol e = {line, len, flags, s};

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

static void emit_add_line(const char *reset,
			  struct emit_callback *ecbdata,
			  const char *line, int len)
{
	unsigned flags = WSEH_NEW | ecbdata->ws_rule;
	if (new_blank_line_at_eof(ecbdata, line, len))
		flags |= DIFF_SYMBOL_CONTENT_BLANK_LINE_EOF;

	emit_diff_symbol(ecbdata->opt, DIFF_SYMBOL_PLUS, line, len, flags);
}

static void emit_del_line(const char *reset,
			  struct emit_callback *ecbdata,
			  const char *line, int len)
{
	unsigned flags = WSEH_OLD | ecbdata->ws_rule;
	emit_diff_symbol(ecbdata->opt, DIFF_SYMBOL_MINUS, line, len, flags);
}

static void emit_context_line(const char *reset,
			      struct emit_callback *ecbdata,
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

static struct diff_tempfile *claim_diff_tempfile(void) {
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
	const char *reset = diff_get_color(ecb->color_diff, DIFF_RESET);

	while (0 < size) {
		int len;

		endp = memchr(data, '\n', size);
		len = endp ? (endp - data + 1) : size;
		if (prefix != '+') {
			ecb->lno_in_preimage++;
			emit_del_line(reset, ecb, data, len);
		} else {
			ecb->lno_in_postimage++;
			emit_add_line(reset, ecb, data, len);
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

	size_one = fill_textconv(textconv_one, one, &data_one);
	size_two = fill_textconv(textconv_two, two, &data_two);

	memset(&ecbdata, 0, sizeof(ecbdata));
	ecbdata.color_diff = want_color(o->use_color);
	ecbdata.ws_rule = whitespace_rule(name_b);
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

static void fn_out_diff_words_aux(void *priv, char *line, unsigned long len)
{
	struct diff_words_data *diff_words = priv;
	struct diff_words_style *style = diff_words->style;
	int minus_first, minus_len, plus_first, plus_len;
	const char *minus_begin, *minus_end, *plus_begin, *plus_end;
	struct diff_options *opt = diff_words->opt;
	const char *line_prefix;

	if (line[0] != '@' || parse_hunk_header(line, len,
			&minus_first, &minus_len, &plus_first, &plus_len))
		return;

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
	if (xdi_diff_outf(&minus, &plus, fn_out_diff_words_aux, diff_words,
			  &xpp, &xecfg))
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

static const char *userdiff_word_regex(struct diff_filespec *one)
{
	diff_filespec_load_driver(one);
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
		o->word_regex = userdiff_word_regex(one);
	if (!o->word_regex)
		o->word_regex = userdiff_word_regex(two);
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

static unsigned long sane_truncate_line(struct emit_callback *ecb, char *line, unsigned long len)
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
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);
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
		len = sane_truncate_line(ecbdata, line, len);
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
		emit_add_line(reset, ecbdata, line + 1, len - 1);
		break;
	case '-':
		ecbdata->lno_in_preimage++;
		emit_del_line(reset, ecbdata, line + 1, len - 1);
		break;
	case ' ':
		ecbdata->lno_in_postimage++;
		ecbdata->lno_in_preimage++;
		emit_context_line(reset, ecbdata, line + 1, len - 1);
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

struct diffstat_t {
	int nr;
	int alloc;
	struct diffstat_file {
		char *from_name;
		char *name;
		char *print_name;
		const char *comments;
		unsigned is_unmerged:1;
		unsigned is_binary:1;
		unsigned is_renamed:1;
		unsigned is_interesting:1;
		uintmax_t added, deleted;
	} **files;
};

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
	 * scale linearly as if the alloted width is one column shorter
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
			diff_populate_filespec(p->one, 0);
			diff_populate_filespec(p->two, 0);
			diffcore_count_changes(p->one, p->two, NULL, NULL,
					       &copied, &added);
			diff_free_filespec_data(p->one);
			diff_free_filespec_data(p->two);
		} else if (DIFF_FILE_VALID(p->one)) {
			diff_populate_filespec(p->one, CHECK_SIZE_ONLY);
			copied = added = 0;
			diff_free_filespec_data(p->one);
		} else if (DIFF_FILE_VALID(p->two)) {
			diff_populate_filespec(p->two, CHECK_SIZE_ONLY);
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

static void free_diffstat_info(struct diffstat_t *diffstat)
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
	/* line[1] thru line[marker_size-1] are same as firstchar */
	if (len < marker_size + 1 || !isspace(line[marker_size]))
		return 0;
	return 1;
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
		char *s = xstrfmt("%lu", orig_size);
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

int diff_filespec_is_binary(struct diff_filespec *one)
{
	if (one->is_binary == -1) {
		diff_filespec_load_driver(one);
		if (one->driver->binary != -1)
			one->is_binary = one->driver->binary;
		else {
			if (!one->data && DIFF_FILE_VALID(one))
				diff_populate_filespec(one, CHECK_BINARY);
			if (one->is_binary == -1 && one->data)
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
		textconv_one = get_textconv(one);
		textconv_two = get_textconv(two);
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
		    (textconv_one || !diff_filespec_is_binary(one)) &&
		    (textconv_two || !diff_filespec_is_binary(two))) {
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
	    ( (!textconv_one && diff_filespec_is_binary(one)) ||
	      (!textconv_two && diff_filespec_is_binary(two)) )) {
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
		if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
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
		const char *diffopts = getenv("GIT_DIFF_OPTS");
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

		mf1.size = fill_textconv(textconv_one, one, &mf1.ptr);
		mf2.size = fill_textconv(textconv_two, two, &mf2.ptr);

		pe = diff_funcname_pattern(one);
		if (!pe)
			pe = diff_funcname_pattern(two);

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		memset(&ecbdata, 0, sizeof(ecbdata));
		if (o->flags.suppress_diff_headers)
			lbl[0] = NULL;
		ecbdata.label_path = lbl;
		ecbdata.color_diff = want_color(o->use_color);
		ecbdata.ws_rule = whitespace_rule(name_b);
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
		if (!diffopts)
			;
		else if (skip_prefix(diffopts, "--unified=", &v))
			xecfg.ctxlen = strtoul(v, NULL, 10);
		else if (skip_prefix(diffopts, "-u", &v))
			xecfg.ctxlen = strtoul(v, NULL, 10);
		if (o->word_diff)
			init_diff_words_data(&ecbdata, o, one, two);
		if (xdi_diff_outf(&mf1, &mf2, fn_out_consume, &ecbdata,
				  &xpp, &xecfg))
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

	if (diff_filespec_is_binary(one) || diff_filespec_is_binary(two)) {
		data->is_binary = 1;
		if (same_contents) {
			data->added = 0;
			data->deleted = 0;
		} else {
			data->added = diff_filespec_size(two);
			data->deleted = diff_filespec_size(one);
		}
	}

	else if (complete_rewrite) {
		diff_populate_filespec(one, 0);
		diff_populate_filespec(two, 0);
		data->deleted = count_lines(one->data, one->size);
		data->added = count_lines(two->data, two->size);
	}

	else if (!same_contents) {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;

		if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
			die("unable to read files to diff");

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		xpp.flags = o->xdl_opts;
		xpp.anchors = o->anchors;
		xpp.anchors_nr = o->anchors_nr;
		xecfg.ctxlen = o->context;
		xecfg.interhunkctxlen = o->interhunkcontext;
		if (xdi_diff_outf(&mf1, &mf2, diffstat_consume, diffstat,
				  &xpp, &xecfg))
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
		if (xdi_diff_outf(&mf1, &mf2, checkdiff_consume, &data,
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
static int reuse_worktree_file(const char *name, const struct object_id *oid, int want_file)
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
	if (!FAST_WORKING_DIRECTORY && !want_file && has_object_pack(oid))
		return 0;

	/*
	 * Similarly, if we'd have to convert the file contents anyway, that
	 * makes the optimization not worthwhile.
	 */
	if (!want_file && would_convert_to_git(&the_index, name))
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
	    (!lstat(name, &st) && !ce_match_stat(ce, &st, 0)))
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
int diff_populate_filespec(struct diff_filespec *s, unsigned int flags)
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
	    reuse_worktree_file(s->path, &s->oid, 0)) {
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
		if (size_only && !would_convert_to_git(&the_index, s->path))
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
		if (convert_to_git(&the_index, s->path, s->data, s->size, &buf, conv_flags)) {
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
			type = oid_object_info(the_repository, &s->oid,
					       &s->size);
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
		s->data = read_object_file(&s->oid, &type, &s->size);
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

static void prep_temp_blob(const char *path, struct diff_tempfile *temp,
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
	if (convert_to_working_tree(&the_index, path,
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
		xsnprintf(temp->hex, sizeof(temp->hex), ".");
		xsnprintf(temp->mode, sizeof(temp->mode), ".");
		return temp;
	}

	if (!S_ISGITLINK(one->mode) &&
	    (!one->oid_valid ||
	     reuse_worktree_file(name, &one->oid, 1))) {
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
		if (diff_populate_filespec(one, 0))
			die("cannot read data blob for %s", one->path);
		prep_temp_blob(name, temp, one->data, one->size,
			       &one->oid, one->mode);
	}
	return temp;
}

static void add_external_diff_name(struct argv_array *argv,
				   const char *name,
				   struct diff_filespec *df)
{
	struct diff_tempfile *temp = prepare_temp_file(name, df);
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
			      int complete_rewrite,
			      struct diff_options *o)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	struct argv_array env = ARGV_ARRAY_INIT;
	struct diff_queue_struct *q = &diff_queued_diff;

	argv_array_push(&argv, pgm);
	argv_array_push(&argv, name);

	if (one && two) {
		add_external_diff_name(&argv, name, one);
		if (!other)
			add_external_diff_name(&argv, name, two);
		else {
			add_external_diff_name(&argv, other, two);
			argv_array_push(&argv, other);
			argv_array_push(&argv, xfrm_msg);
		}
	}

	argv_array_pushf(&env, "GIT_DIFF_PATH_COUNTER=%d", ++o->diff_path_counter);
	argv_array_pushf(&env, "GIT_DIFF_PATH_TOTAL=%d", q->nr);

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
			if ((!fill_mmfile(&mf, one) && diff_filespec_is_binary(one)) ||
			    (!fill_mmfile(&mf, two) && diff_filespec_is_binary(two)))
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
			      want_color(o->use_color) && !pgm);
		xfrm_msg = msg->len ? msg->buf : NULL;
	}

	if (pgm) {
		run_external_diff(pgm, name, other, one, two, xfrm_msg,
				  complete_rewrite, o);
		return;
	}
	if (one && two)
		builtin_diff(name, other ? other : name,
			     one, two, xfrm_msg, must_show_header,
			     o, complete_rewrite);
	else
		fprintf(o->file, "* Unmerged path %s\n", name);
}

static void diff_fill_oid_info(struct diff_filespec *one)
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
			if (index_path(&one->oid, one->path, &st, 0))
				die("cannot hash %s", one->path);
		}
	}
	else
		oidclr(&one->oid);
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

	diff_fill_oid_info(one);
	diff_fill_oid_info(two);

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

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		builtin_diffstat(p->one->path, NULL, NULL, NULL, diffstat, o, p);
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);

	if (o->prefix_length)
		strip_prefix(o->prefix_length, &name, &other);

	diff_fill_oid_info(p->one);
	diff_fill_oid_info(p->two);

	builtin_diffstat(name, other, p->one, p->two, diffstat, o, p);
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

	diff_fill_oid_info(p->one);
	diff_fill_oid_info(p->two);

	builtin_checkdiff(name, other, attr_path, p->one, p->two, o);
}

void diff_setup(struct diff_options *options)
{
	memcpy(options, &default_diff_options, sizeof(*options));

	options->file = stdout;

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
	eq = strchrnul(arg, '=');
	len = eq - arg;
	if (!len || strncmp(arg, arg_long, len))
		return 0;
	if (*eq) {
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

static int stat_opt(struct diff_options *options, const char **av)
{
	const char *arg = av[0];
	char *end;
	int width = options->stat_width;
	int name_width = options->stat_name_width;
	int graph_width = options->stat_graph_width;
	int count = options->stat_count;
	int argcount = 1;

	if (!skip_prefix(arg, "--stat", &arg))
		BUG("stat option does not begin with --stat: %s", arg);
	end = (char *)arg;

	switch (*arg) {
	case '-':
		if (skip_prefix(arg, "-width", &arg)) {
			if (*arg == '=')
				width = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die_want_option("--stat-width");
			else if (!*arg) {
				width = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		} else if (skip_prefix(arg, "-name-width", &arg)) {
			if (*arg == '=')
				name_width = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die_want_option("--stat-name-width");
			else if (!*arg) {
				name_width = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		} else if (skip_prefix(arg, "-graph-width", &arg)) {
			if (*arg == '=')
				graph_width = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die_want_option("--stat-graph-width");
			else if (!*arg) {
				graph_width = strtoul(av[1], &end, 10);
				argcount = 2;
			}
		} else if (skip_prefix(arg, "-count", &arg)) {
			if (*arg == '=')
				count = strtoul(arg + 1, &end, 10);
			else if (!*arg && !av[1])
				die_want_option("--stat-count");
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
	options->stat_graph_width = graph_width;
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

static int parse_submodule_opt(struct diff_options *options, const char *value)
{
	if (parse_submodule_params(options, value))
		die(_("Failed to parse --submodule option parameter: '%s'"),
			value);
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

static int parse_diff_filter_opt(const char *optarg, struct diff_options *opt)
{
	int i, optch;

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
			return optarg[i];
		if (negate)
			opt->filter &= ~bit;
		else
			opt->filter |= bit;
	}
	return 0;
}

static void enable_patch_output(int *fmt) {
	*fmt &= ~DIFF_FORMAT_NO_OUTPUT;
	*fmt |= DIFF_FORMAT_PATCH;
}

static int parse_ws_error_highlight_opt(struct diff_options *opt, const char *arg)
{
	int val = parse_ws_error_highlight(arg);

	if (val < 0) {
		error("unknown value after ws-error-highlight=%.*s",
		      -1 - val, arg);
		return 0;
	}
	opt->ws_error_highlight = val;
	return 1;
}

static int parse_objfind_opt(struct diff_options *opt, const char *arg)
{
	struct object_id oid;

	if (get_oid(arg, &oid))
		return error("unable to resolve '%s'", arg);

	if (!opt->objfind)
		opt->objfind = xcalloc(1, sizeof(*opt->objfind));

	opt->pickaxe_opts |= DIFF_PICKAXE_KIND_OBJFIND;
	opt->flags.recursive = 1;
	opt->flags.tree_in_recursive = 1;
	oidset_insert(opt->objfind, &oid);
	return 1;
}

int diff_opt_parse(struct diff_options *options,
		   const char **av, int ac, const char *prefix)
{
	const char *arg = av[0];
	const char *optarg;
	int argcount;

	if (!prefix)
		prefix = "";

	/* Output format options */
	if (!strcmp(arg, "-p") || !strcmp(arg, "-u") || !strcmp(arg, "--patch")
	    || opt_arg(arg, 'U', "unified", &options->context))
		enable_patch_output(&options->output_format);
	else if (!strcmp(arg, "--raw"))
		options->output_format |= DIFF_FORMAT_RAW;
	else if (!strcmp(arg, "--patch-with-raw")) {
		enable_patch_output(&options->output_format);
		options->output_format |= DIFF_FORMAT_RAW;
	} else if (!strcmp(arg, "--numstat"))
		options->output_format |= DIFF_FORMAT_NUMSTAT;
	else if (!strcmp(arg, "--shortstat"))
		options->output_format |= DIFF_FORMAT_SHORTSTAT;
	else if (skip_prefix(arg, "-X", &arg) ||
		 skip_to_optional_arg(arg, "--dirstat", &arg))
		return parse_dirstat_opt(options, arg);
	else if (!strcmp(arg, "--cumulative"))
		return parse_dirstat_opt(options, "cumulative");
	else if (skip_to_optional_arg(arg, "--dirstat-by-file", &arg)) {
		parse_dirstat_opt(options, "files");
		return parse_dirstat_opt(options, arg);
	}
	else if (!strcmp(arg, "--check"))
		options->output_format |= DIFF_FORMAT_CHECKDIFF;
	else if (!strcmp(arg, "--summary"))
		options->output_format |= DIFF_FORMAT_SUMMARY;
	else if (!strcmp(arg, "--patch-with-stat")) {
		enable_patch_output(&options->output_format);
		options->output_format |= DIFF_FORMAT_DIFFSTAT;
	} else if (!strcmp(arg, "--name-only"))
		options->output_format |= DIFF_FORMAT_NAME;
	else if (!strcmp(arg, "--name-status"))
		options->output_format |= DIFF_FORMAT_NAME_STATUS;
	else if (!strcmp(arg, "-s") || !strcmp(arg, "--no-patch"))
		options->output_format |= DIFF_FORMAT_NO_OUTPUT;
	else if (starts_with(arg, "--stat"))
		/* --stat, --stat-width, --stat-name-width, or --stat-count */
		return stat_opt(options, av);
	else if (!strcmp(arg, "--compact-summary")) {
		 options->flags.stat_with_summary = 1;
		 options->output_format |= DIFF_FORMAT_DIFFSTAT;
	} else if (!strcmp(arg, "--no-compact-summary"))
		 options->flags.stat_with_summary = 0;
	else if (skip_prefix(arg, "--output-indicator-new=", &arg))
		options->output_indicators[OUTPUT_INDICATOR_NEW] = arg[0];
	else if (skip_prefix(arg, "--output-indicator-old=", &arg))
		options->output_indicators[OUTPUT_INDICATOR_OLD] = arg[0];
	else if (skip_prefix(arg, "--output-indicator-context=", &arg))
		options->output_indicators[OUTPUT_INDICATOR_CONTEXT] = arg[0];

	/* renames options */
	else if (starts_with(arg, "-B") ||
		 skip_to_optional_arg(arg, "--break-rewrites", NULL)) {
		if ((options->break_opt = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -B: %s", arg+2);
	}
	else if (starts_with(arg, "-M") ||
		 skip_to_optional_arg(arg, "--find-renames", NULL)) {
		if ((options->rename_score = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -M: %s", arg+2);
		options->detect_rename = DIFF_DETECT_RENAME;
	}
	else if (!strcmp(arg, "-D") || !strcmp(arg, "--irreversible-delete")) {
		options->irreversible_delete = 1;
	}
	else if (starts_with(arg, "-C") ||
		 skip_to_optional_arg(arg, "--find-copies", NULL)) {
		if (options->detect_rename == DIFF_DETECT_COPY)
			options->flags.find_copies_harder = 1;
		if ((options->rename_score = diff_scoreopt_parse(arg)) == -1)
			return error("invalid argument to -C: %s", arg+2);
		options->detect_rename = DIFF_DETECT_COPY;
	}
	else if (!strcmp(arg, "--no-renames"))
		options->detect_rename = 0;
	else if (!strcmp(arg, "--rename-empty"))
		options->flags.rename_empty = 1;
	else if (!strcmp(arg, "--no-rename-empty"))
		options->flags.rename_empty = 0;
	else if (skip_to_optional_arg_default(arg, "--relative", &arg, NULL)) {
		options->flags.relative_name = 1;
		if (arg)
			options->prefix = arg;
	}

	/* xdiff options */
	else if (!strcmp(arg, "--minimal"))
		DIFF_XDL_SET(options, NEED_MINIMAL);
	else if (!strcmp(arg, "--no-minimal"))
		DIFF_XDL_CLR(options, NEED_MINIMAL);
	else if (!strcmp(arg, "-w") || !strcmp(arg, "--ignore-all-space"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE);
	else if (!strcmp(arg, "-b") || !strcmp(arg, "--ignore-space-change"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE_CHANGE);
	else if (!strcmp(arg, "--ignore-space-at-eol"))
		DIFF_XDL_SET(options, IGNORE_WHITESPACE_AT_EOL);
	else if (!strcmp(arg, "--ignore-cr-at-eol"))
		DIFF_XDL_SET(options, IGNORE_CR_AT_EOL);
	else if (!strcmp(arg, "--ignore-blank-lines"))
		DIFF_XDL_SET(options, IGNORE_BLANK_LINES);
	else if (!strcmp(arg, "--indent-heuristic"))
		DIFF_XDL_SET(options, INDENT_HEURISTIC);
	else if (!strcmp(arg, "--no-indent-heuristic"))
		DIFF_XDL_CLR(options, INDENT_HEURISTIC);
	else if (!strcmp(arg, "--patience")) {
		int i;
		options->xdl_opts = DIFF_WITH_ALG(options, PATIENCE_DIFF);
		/*
		 * Both --patience and --anchored use PATIENCE_DIFF
		 * internally, so remove any anchors previously
		 * specified.
		 */
		for (i = 0; i < options->anchors_nr; i++)
			free(options->anchors[i]);
		options->anchors_nr = 0;
	} else if (!strcmp(arg, "--histogram"))
		options->xdl_opts = DIFF_WITH_ALG(options, HISTOGRAM_DIFF);
	else if ((argcount = parse_long_opt("diff-algorithm", av, &optarg))) {
		long value = parse_algorithm_value(optarg);
		if (value < 0)
			return error("option diff-algorithm accepts \"myers\", "
				     "\"minimal\", \"patience\" and \"histogram\"");
		/* clear out previous settings */
		DIFF_XDL_CLR(options, NEED_MINIMAL);
		options->xdl_opts &= ~XDF_DIFF_ALGORITHM_MASK;
		options->xdl_opts |= value;
		return argcount;
	} else if (skip_prefix(arg, "--anchored=", &arg)) {
		options->xdl_opts = DIFF_WITH_ALG(options, PATIENCE_DIFF);
		ALLOC_GROW(options->anchors, options->anchors_nr + 1,
			   options->anchors_alloc);
		options->anchors[options->anchors_nr++] = xstrdup(arg);
	}

	/* flags options */
	else if (!strcmp(arg, "--binary")) {
		enable_patch_output(&options->output_format);
		options->flags.binary = 1;
	}
	else if (!strcmp(arg, "--full-index"))
		options->flags.full_index = 1;
	else if (!strcmp(arg, "-a") || !strcmp(arg, "--text"))
		options->flags.text = 1;
	else if (!strcmp(arg, "-R"))
		options->flags.reverse_diff = 1;
	else if (!strcmp(arg, "--find-copies-harder"))
		options->flags.find_copies_harder = 1;
	else if (!strcmp(arg, "--follow"))
		options->flags.follow_renames = 1;
	else if (!strcmp(arg, "--no-follow")) {
		options->flags.follow_renames = 0;
		options->flags.default_follow_renames = 0;
	} else if (skip_to_optional_arg_default(arg, "--color", &arg, "always")) {
		int value = git_config_colorbool(NULL, arg);
		if (value < 0)
			return error("option `color' expects \"always\", \"auto\", or \"never\"");
		options->use_color = value;
	}
	else if (!strcmp(arg, "--no-color"))
		options->use_color = 0;
	else if (!strcmp(arg, "--color-moved")) {
		if (diff_color_moved_default)
			options->color_moved = diff_color_moved_default;
		if (options->color_moved == COLOR_MOVED_NO)
			options->color_moved = COLOR_MOVED_DEFAULT;
	} else if (!strcmp(arg, "--no-color-moved"))
		options->color_moved = COLOR_MOVED_NO;
	else if (skip_prefix(arg, "--color-moved=", &arg)) {
		int cm = parse_color_moved(arg);
		if (cm < 0)
			die("bad --color-moved argument: %s", arg);
		options->color_moved = cm;
	} else if (skip_prefix(arg, "--color-moved-ws=", &arg)) {
		options->color_moved_ws_handling = parse_color_moved_ws(arg);
	} else if (skip_to_optional_arg_default(arg, "--color-words", &options->word_regex, NULL)) {
		options->use_color = 1;
		options->word_diff = DIFF_WORDS_COLOR;
	}
	else if (!strcmp(arg, "--word-diff")) {
		if (options->word_diff == DIFF_WORDS_NONE)
			options->word_diff = DIFF_WORDS_PLAIN;
	}
	else if (skip_prefix(arg, "--word-diff=", &arg)) {
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
			die("bad --word-diff argument: %s", arg);
	}
	else if ((argcount = parse_long_opt("word-diff-regex", av, &optarg))) {
		if (options->word_diff == DIFF_WORDS_NONE)
			options->word_diff = DIFF_WORDS_PLAIN;
		options->word_regex = optarg;
		return argcount;
	}
	else if (!strcmp(arg, "--exit-code"))
		options->flags.exit_with_status = 1;
	else if (!strcmp(arg, "--quiet"))
		options->flags.quick = 1;
	else if (!strcmp(arg, "--ext-diff"))
		options->flags.allow_external = 1;
	else if (!strcmp(arg, "--no-ext-diff"))
		options->flags.allow_external = 0;
	else if (!strcmp(arg, "--textconv")) {
		options->flags.allow_textconv = 1;
		options->flags.textconv_set_via_cmdline = 1;
	} else if (!strcmp(arg, "--no-textconv"))
		options->flags.allow_textconv = 0;
	else if (skip_to_optional_arg_default(arg, "--ignore-submodules", &arg, "all")) {
		options->flags.override_submodule_config = 1;
		handle_ignore_submodules_arg(options, arg);
	} else if (skip_to_optional_arg_default(arg, "--submodule", &arg, "log"))
		return parse_submodule_opt(options, arg);
	else if (skip_prefix(arg, "--ws-error-highlight=", &arg))
		return parse_ws_error_highlight_opt(options, arg);
	else if (!strcmp(arg, "--ita-invisible-in-index"))
		options->ita_invisible_in_index = 1;
	else if (!strcmp(arg, "--ita-visible-in-index"))
		options->ita_invisible_in_index = 0;

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
		options->orderfile = prefix_filename(prefix, optarg);
		return argcount;
	} else if (skip_prefix(arg, "--find-object=", &arg))
		return parse_objfind_opt(options, arg);
	else if ((argcount = parse_long_opt("diff-filter", av, &optarg))) {
		int offending = parse_diff_filter_opt(optarg, options);
		if (offending)
			die("unknown change class '%c' in --diff-filter=%s",
			    offending, optarg);
		return argcount;
	}
	else if (!strcmp(arg, "--no-abbrev"))
		options->abbrev = 0;
	else if (!strcmp(arg, "--abbrev"))
		options->abbrev = DEFAULT_ABBREV;
	else if (skip_prefix(arg, "--abbrev=", &arg)) {
		options->abbrev = strtoul(arg, NULL, 10);
		if (options->abbrev < MINIMUM_ABBREV)
			options->abbrev = MINIMUM_ABBREV;
		else if (the_hash_algo->hexsz < options->abbrev)
			options->abbrev = the_hash_algo->hexsz;
	}
	else if ((argcount = parse_long_opt("src-prefix", av, &optarg))) {
		options->a_prefix = optarg;
		return argcount;
	}
	else if ((argcount = parse_long_opt("line-prefix", av, &optarg))) {
		options->line_prefix = optarg;
		options->line_prefix_length = strlen(options->line_prefix);
		graph_setup_line_prefix(options);
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
	else if (!strcmp(arg, "-W"))
		options->flags.funccontext = 1;
	else if (!strcmp(arg, "--function-context"))
		options->flags.funccontext = 1;
	else if (!strcmp(arg, "--no-function-context"))
		options->flags.funccontext = 0;
	else if ((argcount = parse_long_opt("output", av, &optarg))) {
		char *path = prefix_filename(prefix, optarg);
		options->file = xfopen(path, "w");
		options->close_file = 1;
		if (options->use_color != GIT_COLOR_ALWAYS)
			options->use_color = GIT_COLOR_NEVER;
		free(path);
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
		if (skip_prefix(opt, "break-rewrites", &opt)) {
			if (*opt == 0 || *opt++ == '=')
				cmd = 'B';
		} else if (skip_prefix(opt, "find-copies", &opt)) {
			if (*opt == 0 || *opt++ == '=')
				cmd = 'C';
		} else if (skip_prefix(opt, "find-renames", &opt)) {
			if (*opt == 0 || *opt++ == '=')
				cmd = 'M';
		}
	}
	if (cmd != 'M' && cmd != 'C' && cmd != 'B')
		return -1; /* that is not a -M, -C, or -B option */

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
	if (starts_with(line, "@@ -"))
		return;

	new_len = remove_space(line, len);

	git_SHA1_Update(data->ctx, line, new_len);
	data->patchlen += new_len;
}

static void patch_id_add_string(git_SHA_CTX *ctx, const char *str)
{
	git_SHA1_Update(ctx, str, strlen(str));
}

static void patch_id_add_mode(git_SHA_CTX *ctx, unsigned mode)
{
	/* large enough for 2^32 in octal */
	char buf[12];
	int len = xsnprintf(buf, sizeof(buf), "%06o", mode);
	git_SHA1_Update(ctx, buf, len);
}

/* returns 0 upon success, and writes result into sha1 */
static int diff_get_patch_id(struct diff_options *options, struct object_id *oid, int diff_header_only)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	git_SHA_CTX ctx;
	struct patch_id_t data;

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

		diff_fill_oid_info(p->one);
		diff_fill_oid_info(p->two);

		len1 = remove_space(p->one->path, strlen(p->one->path));
		len2 = remove_space(p->two->path, strlen(p->two->path));
		patch_id_add_string(&ctx, "diff--git");
		patch_id_add_string(&ctx, "a/");
		git_SHA1_Update(&ctx, p->one->path, len1);
		patch_id_add_string(&ctx, "b/");
		git_SHA1_Update(&ctx, p->two->path, len2);

		if (p->one->mode == 0) {
			patch_id_add_string(&ctx, "newfilemode");
			patch_id_add_mode(&ctx, p->two->mode);
			patch_id_add_string(&ctx, "---/dev/null");
			patch_id_add_string(&ctx, "+++b/");
			git_SHA1_Update(&ctx, p->two->path, len2);
		} else if (p->two->mode == 0) {
			patch_id_add_string(&ctx, "deletedfilemode");
			patch_id_add_mode(&ctx, p->one->mode);
			patch_id_add_string(&ctx, "---a/");
			git_SHA1_Update(&ctx, p->one->path, len1);
			patch_id_add_string(&ctx, "+++/dev/null");
		} else {
			patch_id_add_string(&ctx, "---a/");
			git_SHA1_Update(&ctx, p->one->path, len1);
			patch_id_add_string(&ctx, "+++b/");
			git_SHA1_Update(&ctx, p->two->path, len2);
		}

		if (diff_header_only)
			continue;

		if (fill_mmfile(&mf1, p->one) < 0 ||
		    fill_mmfile(&mf2, p->two) < 0)
			return error("unable to read files to diff");

		if (diff_filespec_is_binary(p->one) ||
		    diff_filespec_is_binary(p->two)) {
			git_SHA1_Update(&ctx, oid_to_hex(&p->one->oid),
					GIT_SHA1_HEXSZ);
			git_SHA1_Update(&ctx, oid_to_hex(&p->two->oid),
					GIT_SHA1_HEXSZ);
			continue;
		}

		xpp.flags = 0;
		xecfg.ctxlen = 3;
		xecfg.flags = 0;
		if (xdi_diff_outf(&mf1, &mf2, patch_id_consume, &data,
				  &xpp, &xecfg))
			return error("unable to generate patch-id diff for %s",
				     p->one->path);
	}

	git_SHA1_Final(oid->hash, &ctx);
	return 0;
}

int diff_flush_patch_id(struct diff_options *options, struct object_id *oid, int diff_header_only)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	int result = diff_get_patch_id(options, oid, diff_header_only);

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

			hashmap_free(&add_lines, 0);
			hashmap_free(&del_lines, 0);
		}

		for (i = 0; i < esm.nr; i++)
			emit_diff_symbol_from_struct(o, &esm.buf[i]);

		for (i = 0; i < esm.nr; i++)
			free((void *)esm.buf[i].line);
	}
	esm.nr = 0;
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

static int diff_filespec_check_stat_unmatch(struct diff_filepair *p)
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
	    diff_populate_filespec(p->one, CHECK_SIZE_ONLY) ||
	    diff_populate_filespec(p->two, CHECK_SIZE_ONLY) ||
	    (p->one->size != p->two->size) ||
	    !diff_filespec_is_identical(p->one, p->two)) /* (2) */
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

		if (diff_filespec_check_stat_unmatch(p))
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

void diffcore_fix_diff_index(struct diff_options *options)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	QSORT(q->queue, q->nr, diffnamecmp);
}

void diffcore_std(struct diff_options *options)
{
	/* NOTE please keep the following in sync with diff_tree_combined() */
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
	    !diff_filespec_check_stat_unmatch(p))
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

static char *run_textconv(const char *pgm, struct diff_filespec *spec,
		size_t *outsize)
{
	struct diff_tempfile *temp;
	const char *argv[3];
	const char **arg = argv;
	struct child_process child = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	int err = 0;

	temp = prepare_temp_file(spec->path, spec);
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

size_t fill_textconv(struct userdiff_driver *driver,
		     struct diff_filespec *df,
		     char **outbuf)
{
	size_t size;

	if (!driver) {
		if (!DIFF_FILE_VALID(df)) {
			*outbuf = "";
			return 0;
		}
		if (diff_populate_filespec(df, 0))
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

	*outbuf = run_textconv(driver->textconv, df, &size);
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

int textconv_object(const char *path,
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
	textconv = get_textconv(df);
	if (!textconv) {
		free_filespec(df);
		return 0;
	}

	*buf_size = fill_textconv(textconv, df, buf);
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
