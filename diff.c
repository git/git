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

#ifdef NO_FAST_WORKING_DIRECTORY
#define FAST_WORKING_DIRECTORY 0
#else
#define FAST_WORKING_DIRECTORY 1
#endif

static int use_size_cache;

static int diff_detect_rename_default;
static int diff_rename_limit_default = -1;
static int diff_use_color_default;

static char diff_colors[][COLOR_MAXLEN] = {
	"\033[m",	/* reset */
	"",		/* PLAIN (normal) */
	"\033[1m",	/* METAINFO (bold) */
	"\033[36m",	/* FRAGINFO (cyan) */
	"\033[31m",	/* OLD (red) */
	"\033[32m",	/* NEW (green) */
	"\033[33m",	/* COMMIT (yellow) */
	"\033[41m",	/* WHITESPACE (red background) */
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
	die("bad config variable '%s'", var);
}

/*
 * These are to give UI layer defaults.
 * The core-level commands such as git-diff-files should
 * never be affected by the setting of diff.renames
 * the user happens to have in the configuration file.
 */
int git_diff_ui_config(const char *var, const char *value)
{
	if (!strcmp(var, "diff.renamelimit")) {
		diff_rename_limit_default = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff")) {
		diff_use_color_default = git_config_colorbool(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.renames")) {
		if (!value)
			diff_detect_rename_default = DIFF_DETECT_RENAME;
		else if (!strcasecmp(value, "copies") ||
			 !strcasecmp(value, "copy"))
			diff_detect_rename_default = DIFF_DETECT_COPY;
		else if (git_config_bool(var,value))
			diff_detect_rename_default = DIFF_DETECT_RENAME;
		return 0;
	}
	if (!prefixcmp(var, "diff.color.") || !prefixcmp(var, "color.diff.")) {
		int slot = parse_diff_color_slot(var, 11);
		color_parse(value, var, diff_colors[slot]);
		return 0;
	}
	return git_default_config(var, value);
}

static char *quote_one(const char *str)
{
	int needlen;
	char *xp;

	if (!str)
		return NULL;
	needlen = quote_c_style(str, NULL, NULL, 0);
	if (!needlen)
		return xstrdup(str);
	xp = xmalloc(needlen + 1);
	quote_c_style(str, xp, NULL, 0);
	return xp;
}

static char *quote_two(const char *one, const char *two)
{
	int need_one = quote_c_style(one, NULL, NULL, 1);
	int need_two = quote_c_style(two, NULL, NULL, 1);
	char *xp;

	if (need_one + need_two) {
		if (!need_one) need_one = strlen(one);
		if (!need_two) need_one = strlen(two);

		xp = xmalloc(need_one + need_two + 3);
		xp[0] = '"';
		quote_c_style(one, xp + 1, NULL, 1);
		quote_c_style(two, xp + need_one + 1, NULL, 1);
		strcpy(xp + need_one + need_two + 1, "\"");
		return xp;
	}
	need_one = strlen(one);
	need_two = strlen(two);
	xp = xmalloc(need_one + need_two + 1);
	strcpy(xp, one);
	strcpy(xp + need_one, two);
	return xp;
}

static const char *external_diff(void)
{
	static const char *external_diff_cmd = NULL;
	static int done_preparing = 0;

	if (done_preparing)
		return external_diff_cmd;
	external_diff_cmd = getenv("GIT_EXTERNAL_DIFF");
	done_preparing = 1;
	return external_diff_cmd;
}

#define TEMPFILE_PATH_LEN		50

static struct diff_tempfile {
	const char *name; /* filename external diff should read from */
	char hex[41];
	char mode[10];
	char tmp_path[TEMPFILE_PATH_LEN];
} diff_temp[2];

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

static void print_line_count(int count)
{
	switch (count) {
	case 0:
		printf("0,0");
		break;
	case 1:
		printf("1");
		break;
	default:
		printf("1,%d", count);
		break;
	}
}

static void copy_file(int prefix, const char *data, int size,
		const char *set, const char *reset)
{
	int ch, nl_just_seen = 1;
	while (0 < size--) {
		ch = *data++;
		if (nl_just_seen) {
			fputs(set, stdout);
			putchar(prefix);
		}
		if (ch == '\n') {
			nl_just_seen = 1;
			fputs(reset, stdout);
		} else
			nl_just_seen = 0;
		putchar(ch);
	}
	if (!nl_just_seen)
		printf("%s\n\\ No newline at end of file\n", reset);
}

static void emit_rewrite_diff(const char *name_a,
			      const char *name_b,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      int color_diff)
{
	int lc_a, lc_b;
	const char *name_a_tab, *name_b_tab;
	const char *metainfo = diff_get_color(color_diff, DIFF_METAINFO);
	const char *fraginfo = diff_get_color(color_diff, DIFF_FRAGINFO);
	const char *old = diff_get_color(color_diff, DIFF_FILE_OLD);
	const char *new = diff_get_color(color_diff, DIFF_FILE_NEW);
	const char *reset = diff_get_color(color_diff, DIFF_RESET);

	name_a_tab = strchr(name_a, ' ') ? "\t" : "";
	name_b_tab = strchr(name_b, ' ') ? "\t" : "";

	diff_populate_filespec(one, 0);
	diff_populate_filespec(two, 0);
	lc_a = count_lines(one->data, one->size);
	lc_b = count_lines(two->data, two->size);
	printf("%s--- a/%s%s%s\n%s+++ b/%s%s%s\n%s@@ -",
	       metainfo, name_a, name_a_tab, reset,
	       metainfo, name_b, name_b_tab, reset, fraginfo);
	print_line_count(lc_a);
	printf(" +");
	print_line_count(lc_b);
	printf(" @@%s\n", reset);
	if (lc_a)
		copy_file('-', one->data, one->size, old, reset);
	if (lc_b)
		copy_file('+', two->data, two->size, new, reset);
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

struct diff_words_buffer {
	mmfile_t text;
	long alloc;
	long current; /* output pointer */
	int suppressed_newline;
};

static void diff_words_append(char *line, unsigned long len,
		struct diff_words_buffer *buffer)
{
	if (buffer->text.size + len > buffer->alloc) {
		buffer->alloc = (buffer->text.size + len) * 3 / 2;
		buffer->text.ptr = xrealloc(buffer->text.ptr, buffer->alloc);
	}
	line++;
	len--;
	memcpy(buffer->text.ptr + buffer->text.size, line, len);
	buffer->text.size += len;
}

struct diff_words_data {
	struct xdiff_emit_state xm;
	struct diff_words_buffer minus, plus;
};

static void print_word(struct diff_words_buffer *buffer, int len, int color,
		int suppress_newline)
{
	const char *ptr;
	int eol = 0;

	if (len == 0)
		return;

	ptr  = buffer->text.ptr + buffer->current;
	buffer->current += len;

	if (ptr[len - 1] == '\n') {
		eol = 1;
		len--;
	}

	fputs(diff_get_color(1, color), stdout);
	fwrite(ptr, len, 1, stdout);
	fputs(diff_get_color(1, DIFF_RESET), stdout);

	if (eol) {
		if (suppress_newline)
			buffer->suppressed_newline = 1;
		else
			putchar('\n');
	}
}

static void fn_out_diff_words_aux(void *priv, char *line, unsigned long len)
{
	struct diff_words_data *diff_words = priv;

	if (diff_words->minus.suppressed_newline) {
		if (line[0] != '+')
			putchar('\n');
		diff_words->minus.suppressed_newline = 0;
	}

	len--;
	switch (line[0]) {
		case '-':
			print_word(&diff_words->minus, len, DIFF_FILE_OLD, 1);
			break;
		case '+':
			print_word(&diff_words->plus, len, DIFF_FILE_NEW, 0);
			break;
		case ' ':
			print_word(&diff_words->plus, len, DIFF_PLAIN, 0);
			diff_words->minus.current += len;
			break;
	}
}

/* this executes the word diff on the accumulated buffers */
static void diff_words_show(struct diff_words_data *diff_words)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;
	mmfile_t minus, plus;
	int i;

	minus.size = diff_words->minus.text.size;
	minus.ptr = xmalloc(minus.size);
	memcpy(minus.ptr, diff_words->minus.text.ptr, minus.size);
	for (i = 0; i < minus.size; i++)
		if (isspace(minus.ptr[i]))
			minus.ptr[i] = '\n';
	diff_words->minus.current = 0;

	plus.size = diff_words->plus.text.size;
	plus.ptr = xmalloc(plus.size);
	memcpy(plus.ptr, diff_words->plus.text.ptr, plus.size);
	for (i = 0; i < plus.size; i++)
		if (isspace(plus.ptr[i]))
			plus.ptr[i] = '\n';
	diff_words->plus.current = 0;

	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = diff_words->minus.alloc + diff_words->plus.alloc;
	xecfg.flags = 0;
	ecb.outf = xdiff_outf;
	ecb.priv = diff_words;
	diff_words->xm.consume = fn_out_diff_words_aux;
	xdl_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	diff_words->minus.text.size = diff_words->plus.text.size = 0;

	if (diff_words->minus.suppressed_newline) {
		putchar('\n');
		diff_words->minus.suppressed_newline = 0;
	}
}

struct emit_callback {
	struct xdiff_emit_state xm;
	int nparents, color_diff;
	const char **label_path;
	struct diff_words_data *diff_words;
};

static void free_diff_words_data(struct emit_callback *ecbdata)
{
	if (ecbdata->diff_words) {
		/* flush buffers */
		if (ecbdata->diff_words->minus.text.size ||
				ecbdata->diff_words->plus.text.size)
			diff_words_show(ecbdata->diff_words);

		if (ecbdata->diff_words->minus.text.ptr)
			free (ecbdata->diff_words->minus.text.ptr);
		if (ecbdata->diff_words->plus.text.ptr)
			free (ecbdata->diff_words->plus.text.ptr);
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

static void emit_line(const char *set, const char *reset, const char *line, int len)
{
	if (len > 0 && line[len-1] == '\n')
		len--;
	fputs(set, stdout);
	fwrite(line, len, 1, stdout);
	puts(reset);
}

static void emit_line_with_ws(int nparents,
		const char *set, const char *reset, const char *ws,
		const char *line, int len)
{
	int col0 = nparents;
	int last_tab_in_indent = -1;
	int last_space_in_indent = -1;
	int i;
	int tail = len;
	int need_highlight_leading_space = 0;
	/* The line is a newly added line.  Does it have funny leading
	 * whitespaces?  In indent, SP should never precede a TAB.
	 */
	for (i = col0; i < len; i++) {
		if (line[i] == '\t') {
			last_tab_in_indent = i;
			if (0 <= last_space_in_indent)
				need_highlight_leading_space = 1;
		}
		else if (line[i] == ' ')
			last_space_in_indent = i;
		else
			break;
	}
	fputs(set, stdout);
	fwrite(line, col0, 1, stdout);
	fputs(reset, stdout);
	if (((i == len) || line[i] == '\n') && i != col0) {
		/* The whole line was indent */
		emit_line(ws, reset, line + col0, len - col0);
		return;
	}
	i = col0;
	if (need_highlight_leading_space) {
		while (i < last_tab_in_indent) {
			if (line[i] == ' ') {
				fputs(ws, stdout);
				putchar(' ');
				fputs(reset, stdout);
			}
			else
				putchar(line[i]);
			i++;
		}
	}
	tail = len - 1;
	if (line[tail] == '\n' && i < tail)
		tail--;
	while (i < tail) {
		if (!isspace(line[tail]))
			break;
		tail--;
	}
	if ((i < tail && line[tail + 1] != '\n')) {
		/* This has whitespace between tail+1..len */
		fputs(set, stdout);
		fwrite(line + i, tail - i + 1, 1, stdout);
		fputs(reset, stdout);
		emit_line(ws, reset, line + tail + 1, len - tail - 1);
	}
	else
		emit_line(set, reset, line + i, len - i);
}

static void emit_add_line(const char *reset, struct emit_callback *ecbdata, const char *line, int len)
{
	const char *ws = diff_get_color(ecbdata->color_diff, DIFF_WHITESPACE);
	const char *set = diff_get_color(ecbdata->color_diff, DIFF_FILE_NEW);

	if (!*ws)
		emit_line(set, reset, line, len);
	else
		emit_line_with_ws(ecbdata->nparents, set, reset, ws,
				line, len);
}

static void fn_out_consume(void *priv, char *line, unsigned long len)
{
	int i;
	int color;
	struct emit_callback *ecbdata = priv;
	const char *set = diff_get_color(ecbdata->color_diff, DIFF_METAINFO);
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);

	if (ecbdata->label_path[0]) {
		const char *name_a_tab, *name_b_tab;

		name_a_tab = strchr(ecbdata->label_path[0], ' ') ? "\t" : "";
		name_b_tab = strchr(ecbdata->label_path[1], ' ') ? "\t" : "";

		printf("%s--- %s%s%s\n",
		       set, ecbdata->label_path[0], reset, name_a_tab);
		printf("%s+++ %s%s%s\n",
		       set, ecbdata->label_path[1], reset, name_b_tab);
		ecbdata->label_path[0] = ecbdata->label_path[1] = NULL;
	}

	/* This is not really necessary for now because
	 * this codepath only deals with two-way diffs.
	 */
	for (i = 0; i < len && line[i] == '@'; i++)
		;
	if (2 <= i && i < len && line[i] == ' ') {
		ecbdata->nparents = i - 1;
		emit_line(diff_get_color(ecbdata->color_diff, DIFF_FRAGINFO),
			  reset, line, len);
		return;
	}

	if (len < ecbdata->nparents) {
		set = reset;
		emit_line(reset, reset, line, len);
		return;
	}

	color = DIFF_PLAIN;
	if (ecbdata->diff_words && ecbdata->nparents != 1)
		/* fall back to normal diff */
		free_diff_words_data(ecbdata);
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
		if (ecbdata->diff_words->minus.text.size ||
		    ecbdata->diff_words->plus.text.size)
			diff_words_show(ecbdata->diff_words);
		line++;
		len--;
		emit_line(set, reset, line, len);
		return;
	}
	for (i = 0; i < ecbdata->nparents && len; i++) {
		if (line[i] == '-')
			color = DIFF_FILE_OLD;
		else if (line[i] == '+')
			color = DIFF_FILE_NEW;
	}

	if (color != DIFF_FILE_NEW) {
		emit_line(diff_get_color(ecbdata->color_diff, color),
			  reset, line, len);
		return;
	}
	emit_add_line(reset, ecbdata, line, len);
}

static char *pprint_rename(const char *a, const char *b)
{
	const char *old = a;
	const char *new = b;
	char *name = NULL;
	int pfx_length, sfx_length;
	int len_a = strlen(a);
	int len_b = strlen(b);
	int qlen_a = quote_c_style(a, NULL, NULL, 0);
	int qlen_b = quote_c_style(b, NULL, NULL, 0);

	if (qlen_a || qlen_b) {
		if (qlen_a) len_a = qlen_a;
		if (qlen_b) len_b = qlen_b;
		name = xmalloc( len_a + len_b + 5 );
		if (qlen_a)
			quote_c_style(a, name, NULL, 0);
		else
			memcpy(name, a, len_a);
		memcpy(name + len_a, " => ", 4);
		if (qlen_b)
			quote_c_style(b, name + len_a + 4, NULL, 0);
		else
			memcpy(name + len_a + 4, b, len_b + 1);
		return name;
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
	if (pfx_length + sfx_length) {
		int a_midlen = len_a - pfx_length - sfx_length;
		int b_midlen = len_b - pfx_length - sfx_length;
		if (a_midlen < 0) a_midlen = 0;
		if (b_midlen < 0) b_midlen = 0;

		name = xmalloc(pfx_length + a_midlen + b_midlen + sfx_length + 7);
		sprintf(name, "%.*s{%.*s => %.*s}%s",
			pfx_length, a,
			a_midlen, a + pfx_length,
			b_midlen, b + pfx_length,
			a + len_a - sfx_length);
	}
	else {
		name = xmalloc(len_a + len_b + 5);
		sprintf(name, "%s => %s", a, b);
	}
	return name;
}

struct diffstat_t {
	struct xdiff_emit_state xm;

	int nr;
	int alloc;
	struct diffstat_file {
		char *name;
		unsigned is_unmerged:1;
		unsigned is_binary:1;
		unsigned is_renamed:1;
		unsigned int added, deleted;
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
		x->name = pprint_rename(name_a, name_b);
		x->is_renamed = 1;
	}
	else
		x->name = xstrdup(name_a);
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

static void show_name(const char *prefix, const char *name, int len,
		      const char *reset, const char *set)
{
	printf(" %s%s%-*s%s |", set, prefix, len, name, reset);
}

static void show_graph(char ch, int cnt, const char *set, const char *reset)
{
	if (cnt <= 0)
		return;
	printf("%s", set);
	while (cnt--)
		putchar(ch);
	printf("%s", reset);
}

static void show_stats(struct diffstat_t* data, struct diff_options *options)
{
	int i, len, add, del, total, adds = 0, dels = 0;
	int max_change = 0, max_len = 0;
	int total_files = data->nr;
	int width, name_width;
	const char *reset, *set, *add_c, *del_c;

	if (data->nr == 0)
		return;

	width = options->stat_width ? options->stat_width : 80;
	name_width = options->stat_name_width ? options->stat_name_width : 50;

	/* Sanity: give at least 5 columns to the graph,
	 * but leave at least 10 columns for the name.
	 */
	if (width < name_width + 15) {
		if (name_width <= 25)
			width = name_width + 15;
		else
			name_width = width - 15;
	}

	/* Find the longest filename and max number of changes */
	reset = diff_get_color(options->color_diff, DIFF_RESET);
	set = diff_get_color(options->color_diff, DIFF_PLAIN);
	add_c = diff_get_color(options->color_diff, DIFF_FILE_NEW);
	del_c = diff_get_color(options->color_diff, DIFF_FILE_OLD);

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];
		int change = file->added + file->deleted;

		if (!file->is_renamed) {  /* renames are already quoted by pprint_rename */
			len = quote_c_style(file->name, NULL, NULL, 0);
			if (len) {
				char *qname = xmalloc(len + 1);
				quote_c_style(file->name, qname, NULL, 0);
				free(file->name);
				file->name = qname;
			}
		}

		len = strlen(file->name);
		if (max_len < len)
			max_len = len;

		if (file->is_binary || file->is_unmerged)
			continue;
		if (max_change < change)
			max_change = change;
	}

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

	for (i = 0; i < data->nr; i++) {
		const char *prefix = "";
		char *name = data->files[i]->name;
		int added = data->files[i]->added;
		int deleted = data->files[i]->deleted;
		int name_len;

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
			show_name(prefix, name, len, reset, set);
			printf("  Bin\n");
			goto free_diffstat_file;
		}
		else if (data->files[i]->is_unmerged) {
			show_name(prefix, name, len, reset, set);
			printf("  Unmerged\n");
			goto free_diffstat_file;
		}
		else if (!data->files[i]->is_renamed &&
			 (added + deleted == 0)) {
			total_files--;
			goto free_diffstat_file;
		}

		/*
		 * scale the add/delete
		 */
		add = added;
		del = deleted;
		total = add + del;
		adds += add;
		dels += del;

		if (width <= max_change) {
			add = scale_linear(add, width, max_change);
			del = scale_linear(del, width, max_change);
			total = add + del;
		}
		show_name(prefix, name, len, reset, set);
		printf("%5d ", added + deleted);
		show_graph('+', add, add_c, reset);
		show_graph('-', del, del_c, reset);
		putchar('\n');
	free_diffstat_file:
		free(data->files[i]->name);
		free(data->files[i]);
	}
	free(data->files);
	printf("%s %d files changed, %d insertions(+), %d deletions(-)%s\n",
	       set, total_files, adds, dels, reset);
}

static void show_shortstats(struct diffstat_t* data)
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
		free(data->files[i]->name);
		free(data->files[i]);
	}
	free(data->files);

	printf(" %d files changed, %d insertions(+), %d deletions(-)\n",
	       total_files, adds, dels);
}

static void show_numstat(struct diffstat_t* data, struct diff_options *options)
{
	int i;

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];

		if (file->is_binary)
			printf("-\t-\t");
		else
			printf("%d\t%d\t", file->added, file->deleted);
		if (options->line_termination && !file->is_renamed &&
		    quote_c_style(file->name, NULL, NULL, 0))
			quote_c_style(file->name, NULL, stdout, 0);
		else
			fputs(file->name, stdout);
		putchar(options->line_termination);
	}
}

struct checkdiff_t {
	struct xdiff_emit_state xm;
	const char *filename;
	int lineno, color_diff;
};

static void checkdiff_consume(void *priv, char *line, unsigned long len)
{
	struct checkdiff_t *data = priv;
	const char *ws = diff_get_color(data->color_diff, DIFF_WHITESPACE);
	const char *reset = diff_get_color(data->color_diff, DIFF_RESET);
	const char *set = diff_get_color(data->color_diff, DIFF_FILE_NEW);

	if (line[0] == '+') {
		int i, spaces = 0, space_before_tab = 0, white_space_at_end = 0;

		/* check space before tab */
		for (i = 1; i < len && (line[i] == ' ' || line[i] == '\t'); i++)
			if (line[i] == ' ')
				spaces++;
		if (line[i - 1] == '\t' && spaces)
			space_before_tab = 1;

		/* check white space at line end */
		if (line[len - 1] == '\n')
			len--;
		if (isspace(line[len - 1]))
			white_space_at_end = 1;

		if (space_before_tab || white_space_at_end) {
			printf("%s:%d: %s", data->filename, data->lineno, ws);
			if (space_before_tab) {
				printf("space before tab");
				if (white_space_at_end)
					putchar(',');
			}
			if (white_space_at_end)
				printf("white space at end");
			printf(":%s ", reset);
			emit_line_with_ws(1, set, reset, ws, line, len);
		}

		data->lineno++;
	} else if (line[0] == ' ')
		data->lineno++;
	else if (line[0] == '@') {
		char *plus = strchr(line, '+');
		if (plus)
			data->lineno = strtol(plus, NULL, 10);
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

static void emit_binary_diff_body(mmfile_t *one, mmfile_t *two)
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
		printf("delta %lu\n", orig_size);
		free(deflated);
		data = delta;
		data_size = delta_size;
	}
	else {
		printf("literal %lu\n", two->size);
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
		puts(line);
	}
	printf("\n");
	free(data);
}

static void emit_binary_diff(mmfile_t *one, mmfile_t *two)
{
	printf("GIT binary patch\n");
	emit_binary_diff_body(one, two);
	emit_binary_diff_body(two, one);
}

#define FIRST_FEW_BYTES 8000
static int mmfile_is_binary(mmfile_t *mf)
{
	long sz = mf->size;
	if (FIRST_FEW_BYTES < sz)
		sz = FIRST_FEW_BYTES;
	return !!memchr(mf->ptr, 0, sz);
}

static void builtin_diff(const char *name_a,
			 const char *name_b,
			 struct diff_filespec *one,
			 struct diff_filespec *two,
			 const char *xfrm_msg,
			 struct diff_options *o,
			 int complete_rewrite)
{
	mmfile_t mf1, mf2;
	const char *lbl[2];
	char *a_one, *b_two;
	const char *set = diff_get_color(o->color_diff, DIFF_METAINFO);
	const char *reset = diff_get_color(o->color_diff, DIFF_RESET);

	a_one = quote_two("a/", name_a);
	b_two = quote_two("b/", name_b);
	lbl[0] = DIFF_FILE_VALID(one) ? a_one : "/dev/null";
	lbl[1] = DIFF_FILE_VALID(two) ? b_two : "/dev/null";
	printf("%sdiff --git %s %s%s\n", set, a_one, b_two, reset);
	if (lbl[0][0] == '/') {
		/* /dev/null */
		printf("%snew file mode %06o%s\n", set, two->mode, reset);
		if (xfrm_msg && xfrm_msg[0])
			printf("%s%s%s\n", set, xfrm_msg, reset);
	}
	else if (lbl[1][0] == '/') {
		printf("%sdeleted file mode %06o%s\n", set, one->mode, reset);
		if (xfrm_msg && xfrm_msg[0])
			printf("%s%s%s\n", set, xfrm_msg, reset);
	}
	else {
		if (one->mode != two->mode) {
			printf("%sold mode %06o%s\n", set, one->mode, reset);
			printf("%snew mode %06o%s\n", set, two->mode, reset);
		}
		if (xfrm_msg && xfrm_msg[0])
			printf("%s%s%s\n", set, xfrm_msg, reset);
		/*
		 * we do not run diff between different kind
		 * of objects.
		 */
		if ((one->mode ^ two->mode) & S_IFMT)
			goto free_ab_and_return;
		if (complete_rewrite) {
			emit_rewrite_diff(name_a, name_b, one, two,
					o->color_diff);
			goto free_ab_and_return;
		}
	}

	if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
		die("unable to read files to diff");

	if (!o->text && (mmfile_is_binary(&mf1) || mmfile_is_binary(&mf2))) {
		/* Quite common confusing case */
		if (mf1.size == mf2.size &&
		    !memcmp(mf1.ptr, mf2.ptr, mf1.size))
			goto free_ab_and_return;
		if (o->binary)
			emit_binary_diff(&mf1, &mf2);
		else
			printf("Binary files %s and %s differ\n",
			       lbl[0], lbl[1]);
	}
	else {
		/* Crazy xdl interfaces.. */
		const char *diffopts = getenv("GIT_DIFF_OPTS");
		xpparam_t xpp;
		xdemitconf_t xecfg;
		xdemitcb_t ecb;
		struct emit_callback ecbdata;

		memset(&ecbdata, 0, sizeof(ecbdata));
		ecbdata.label_path = lbl;
		ecbdata.color_diff = o->color_diff;
		xpp.flags = XDF_NEED_MINIMAL | o->xdl_opts;
		xecfg.ctxlen = o->context;
		xecfg.flags = XDL_EMIT_FUNCNAMES;
		if (!diffopts)
			;
		else if (!prefixcmp(diffopts, "--unified="))
			xecfg.ctxlen = strtoul(diffopts + 10, NULL, 10);
		else if (!prefixcmp(diffopts, "-u"))
			xecfg.ctxlen = strtoul(diffopts + 2, NULL, 10);
		ecb.outf = xdiff_outf;
		ecb.priv = &ecbdata;
		ecbdata.xm.consume = fn_out_consume;
		if (o->color_diff_words)
			ecbdata.diff_words =
				xcalloc(1, sizeof(struct diff_words_data));
		xdl_diff(&mf1, &mf2, &xpp, &xecfg, &ecb);
		if (o->color_diff_words)
			free_diff_words_data(&ecbdata);
	}

 free_ab_and_return:
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
	if (complete_rewrite) {
		diff_populate_filespec(one, 0);
		diff_populate_filespec(two, 0);
		data->deleted = count_lines(one->data, one->size);
		data->added = count_lines(two->data, two->size);
		return;
	}
	if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
		die("unable to read files to diff");

	if (mmfile_is_binary(&mf1) || mmfile_is_binary(&mf2))
		data->is_binary = 1;
	else {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;
		xdemitcb_t ecb;

		xpp.flags = XDF_NEED_MINIMAL | o->xdl_opts;
		xecfg.ctxlen = 0;
		xecfg.flags = 0;
		ecb.outf = xdiff_outf;
		ecb.priv = diffstat;
		xdl_diff(&mf1, &mf2, &xpp, &xecfg, &ecb);
	}
}

static void builtin_checkdiff(const char *name_a, const char *name_b,
			     struct diff_filespec *one,
			     struct diff_filespec *two, struct diff_options *o)
{
	mmfile_t mf1, mf2;
	struct checkdiff_t data;

	if (!two)
		return;

	memset(&data, 0, sizeof(data));
	data.xm.consume = checkdiff_consume;
	data.filename = name_b ? name_b : name_a;
	data.lineno = 0;
	data.color_diff = o->color_diff;

	if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
		die("unable to read files to diff");

	if (mmfile_is_binary(&mf2))
		return;
	else {
		/* Crazy xdl interfaces.. */
		xpparam_t xpp;
		xdemitconf_t xecfg;
		xdemitcb_t ecb;

		xpp.flags = XDF_NEED_MINIMAL;
		xecfg.ctxlen = 0;
		xecfg.flags = 0;
		ecb.outf = xdiff_outf;
		ecb.priv = &data;
		xdl_diff(&mf1, &mf2, &xpp, &xecfg, &ecb);
	}
}

struct diff_filespec *alloc_filespec(const char *path)
{
	int namelen = strlen(path);
	struct diff_filespec *spec = xmalloc(sizeof(*spec) + namelen + 1);

	memset(spec, 0, sizeof(*spec));
	spec->path = (char *)(spec + 1);
	memcpy(spec->path, path, namelen+1);
	return spec;
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
 * Given a name and sha1 pair, if the dircache tells us the file in
 * the work tree has that object contents, return true, so that
 * prepare_temp_file() does not have to inflate and extract.
 */
static int reuse_worktree_file(const char *name, const unsigned char *sha1, int want_file)
{
	struct cache_entry *ce;
	struct stat st;
	int pos, len;

	/* We do not read the cache ourselves here, because the
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
	if (!FAST_WORKING_DIRECTORY && !want_file && has_sha1_pack(sha1, NULL))
		return 0;

	len = strlen(name);
	pos = cache_name_pos(name, len);
	if (pos < 0)
		return 0;
	ce = active_cache[pos];
	if ((lstat(name, &st) < 0) ||
	    !S_ISREG(st.st_mode) || /* careful! */
	    ce_match_stat(ce, &st, 0) ||
	    hashcmp(sha1, ce->sha1))
		return 0;
	/* we return 1 only when we can stat, it is a regular file,
	 * stat information matches, and sha1 recorded in the cache
	 * matches.  I.e. we know the file in the work tree really is
	 * the same as the <name, sha1> pair.
	 */
	return 1;
}

static struct sha1_size_cache {
	unsigned char sha1[20];
	unsigned long size;
} **sha1_size_cache;
static int sha1_size_cache_nr, sha1_size_cache_alloc;

static struct sha1_size_cache *locate_size_cache(unsigned char *sha1,
						 int find_only,
						 unsigned long size)
{
	int first, last;
	struct sha1_size_cache *e;

	first = 0;
	last = sha1_size_cache_nr;
	while (last > first) {
		int cmp, next = (last + first) >> 1;
		e = sha1_size_cache[next];
		cmp = hashcmp(e->sha1, sha1);
		if (!cmp)
			return e;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	/* not found */
	if (find_only)
		return NULL;
	/* insert to make it at "first" */
	if (sha1_size_cache_alloc <= sha1_size_cache_nr) {
		sha1_size_cache_alloc = alloc_nr(sha1_size_cache_alloc);
		sha1_size_cache = xrealloc(sha1_size_cache,
					   sha1_size_cache_alloc *
					   sizeof(*sha1_size_cache));
	}
	sha1_size_cache_nr++;
	if (first < sha1_size_cache_nr)
		memmove(sha1_size_cache + first + 1, sha1_size_cache + first,
			(sha1_size_cache_nr - first - 1) *
			sizeof(*sha1_size_cache));
	e = xmalloc(sizeof(struct sha1_size_cache));
	sha1_size_cache[first] = e;
	hashcpy(e->sha1, sha1);
	e->size = size;
	return e;
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

	if (!use_size_cache)
		size_only = 0;

	if (s->data)
		return err;
	if (!s->sha1_valid ||
	    reuse_worktree_file(s->path, s->sha1, 0)) {
		struct stat st;
		int fd;
		char *buf;
		unsigned long size;

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
		s->size = st.st_size;
		if (!s->size)
			goto empty;
		if (size_only)
			return 0;
		if (S_ISLNK(st.st_mode)) {
			int ret;
			s->data = xmalloc(s->size);
			s->should_free = 1;
			ret = readlink(s->path, s->data, s->size);
			if (ret < 0) {
				free(s->data);
				goto err_empty;
			}
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
		buf = s->data;
		size = s->size;
		if (convert_to_git(s->path, &buf, &size)) {
			munmap(s->data, s->size);
			s->should_munmap = 0;
			s->data = buf;
			s->size = size;
			s->should_free = 1;
		}
	}
	else {
		char type[20];
		struct sha1_size_cache *e;

		if (size_only) {
			e = locate_size_cache(s->sha1, 1, 0);
			if (e) {
				s->size = e->size;
				return 0;
			}
			if (!sha1_object_info(s->sha1, type, &s->size))
				locate_size_cache(s->sha1, 0, s->size);
		}
		else {
			s->data = read_sha1_file(s->sha1, type, &s->size);
			s->should_free = 1;
		}
	}
	return 0;
}

void diff_free_filespec_data(struct diff_filespec *s)
{
	if (s->should_free)
		free(s->data);
	else if (s->should_munmap)
		munmap(s->data, s->size);
	s->should_free = s->should_munmap = 0;
	s->data = NULL;
	free(s->cnt_data);
	s->cnt_data = NULL;
}

static void prep_temp_blob(struct diff_tempfile *temp,
			   void *blob,
			   unsigned long size,
			   const unsigned char *sha1,
			   int mode)
{
	int fd;

	fd = git_mkstemp(temp->tmp_path, TEMPFILE_PATH_LEN, ".diff_XXXXXX");
	if (fd < 0)
		die("unable to create temp-file");
	if (write_in_full(fd, blob, size) != size)
		die("unable to write temp-file");
	close(fd);
	temp->name = temp->tmp_path;
	strcpy(temp->hex, sha1_to_hex(sha1));
	temp->hex[40] = 0;
	sprintf(temp->mode, "%06o", mode);
}

static void prepare_temp_file(const char *name,
			      struct diff_tempfile *temp,
			      struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one)) {
	not_a_valid_file:
		/* A '-' entry produces this for file-2, and
		 * a '+' entry produces this for file-1.
		 */
		temp->name = "/dev/null";
		strcpy(temp->hex, ".");
		strcpy(temp->mode, ".");
		return;
	}

	if (!one->sha1_valid ||
	    reuse_worktree_file(name, one->sha1, 1)) {
		struct stat st;
		if (lstat(name, &st) < 0) {
			if (errno == ENOENT)
				goto not_a_valid_file;
			die("stat(%s): %s", name, strerror(errno));
		}
		if (S_ISLNK(st.st_mode)) {
			int ret;
			char buf[PATH_MAX + 1]; /* ought to be SYMLINK_MAX */
			if (sizeof(buf) <= st.st_size)
				die("symlink too long: %s", name);
			ret = readlink(name, buf, st.st_size);
			if (ret < 0)
				die("readlink(%s)", name);
			prep_temp_blob(temp, buf, st.st_size,
				       (one->sha1_valid ?
					one->sha1 : null_sha1),
				       (one->sha1_valid ?
					one->mode : S_IFLNK));
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
		return;
	}
	else {
		if (diff_populate_filespec(one, 0))
			die("cannot read data blob for %s", one->path);
		prep_temp_blob(temp, one->data, one->size,
			       one->sha1, one->mode);
	}
}

static void remove_tempfile(void)
{
	int i;

	for (i = 0; i < 2; i++)
		if (diff_temp[i].name == diff_temp[i].tmp_path) {
			unlink(diff_temp[i].name);
			diff_temp[i].name = NULL;
		}
}

static void remove_tempfile_on_signal(int signo)
{
	remove_tempfile();
	signal(SIGINT, SIG_DFL);
	raise(signo);
}

static int spawn_prog(const char *pgm, const char **arg)
{
	pid_t pid;
	int status;

	fflush(NULL);
	pid = fork();
	if (pid < 0)
		die("unable to fork");
	if (!pid) {
		execvp(pgm, (char *const*) arg);
		exit(255);
	}

	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		return -1;
	}

	/* Earlier we did not check the exit status because
	 * diff exits non-zero if files are different, and
	 * we are not interested in knowing that.  It was a
	 * mistake which made it harder to quit a diff-*
	 * session that uses the git-apply-patch-script as
	 * the GIT_EXTERNAL_DIFF.  A custom GIT_EXTERNAL_DIFF
	 * should also exit non-zero only when it wants to
	 * abort the entire diff-* session.
	 */
	if (WIFEXITED(status) && !WEXITSTATUS(status))
		return 0;
	return -1;
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
	struct diff_tempfile *temp = diff_temp;
	int retval;
	static int atexit_asked = 0;
	const char *othername;
	const char **arg = &spawn_arg[0];

	othername = (other? other : name);
	if (one && two) {
		prepare_temp_file(name, &temp[0], one);
		prepare_temp_file(othername, &temp[1], two);
		if (! atexit_asked &&
		    (temp[0].name == temp[0].tmp_path ||
		     temp[1].name == temp[1].tmp_path)) {
			atexit_asked = 1;
			atexit(remove_tempfile);
		}
		signal(SIGINT, remove_tempfile_on_signal);
	}

	if (one && two) {
		*arg++ = pgm;
		*arg++ = name;
		*arg++ = temp[0].name;
		*arg++ = temp[0].hex;
		*arg++ = temp[0].mode;
		*arg++ = temp[1].name;
		*arg++ = temp[1].hex;
		*arg++ = temp[1].mode;
		if (other) {
			*arg++ = other;
			*arg++ = xfrm_msg;
		}
	} else {
		*arg++ = pgm;
		*arg++ = name;
	}
	*arg = NULL;
	retval = spawn_prog(pgm, spawn_arg);
	remove_tempfile();
	if (retval) {
		fprintf(stderr, "external diff died, stopping at %s.\n", name);
		exit(1);
	}
}

static void run_diff_cmd(const char *pgm,
			 const char *name,
			 const char *other,
			 struct diff_filespec *one,
			 struct diff_filespec *two,
			 const char *xfrm_msg,
			 struct diff_options *o,
			 int complete_rewrite)
{
	if (pgm) {
		run_external_diff(pgm, name, other, one, two, xfrm_msg,
				  complete_rewrite);
		return;
	}
	if (one && two)
		builtin_diff(name, other ? other : name,
			     one, two, xfrm_msg, o, complete_rewrite);
	else
		printf("* Unmerged path %s\n", name);
}

static void diff_fill_sha1_info(struct diff_filespec *one)
{
	if (DIFF_FILE_VALID(one)) {
		if (!one->sha1_valid) {
			struct stat st;
			if (lstat(one->path, &st) < 0)
				die("stat %s", one->path);
			if (index_path(one->sha1, one->path, &st, 0))
				die("cannot hash %s\n", one->path);
		}
	}
	else
		hashclr(one->sha1);
}

static void run_diff(struct diff_filepair *p, struct diff_options *o)
{
	const char *pgm = external_diff();
	char msg[PATH_MAX*2+300], *xfrm_msg;
	struct diff_filespec *one;
	struct diff_filespec *two;
	const char *name;
	const char *other;
	char *name_munged, *other_munged;
	int complete_rewrite = 0;
	int len;

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		run_diff_cmd(pgm, p->one->path, NULL, NULL, NULL, NULL, o, 0);
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);
	name_munged = quote_one(name);
	other_munged = quote_one(other);
	one = p->one; two = p->two;

	diff_fill_sha1_info(one);
	diff_fill_sha1_info(two);

	len = 0;
	switch (p->status) {
	case DIFF_STATUS_COPIED:
		len += snprintf(msg + len, sizeof(msg) - len,
				"similarity index %d%%\n"
				"copy from %s\n"
				"copy to %s\n",
				(int)(0.5 + p->score * 100.0/MAX_SCORE),
				name_munged, other_munged);
		break;
	case DIFF_STATUS_RENAMED:
		len += snprintf(msg + len, sizeof(msg) - len,
				"similarity index %d%%\n"
				"rename from %s\n"
				"rename to %s\n",
				(int)(0.5 + p->score * 100.0/MAX_SCORE),
				name_munged, other_munged);
		break;
	case DIFF_STATUS_MODIFIED:
		if (p->score) {
			len += snprintf(msg + len, sizeof(msg) - len,
					"dissimilarity index %d%%\n",
					(int)(0.5 + p->score *
					      100.0/MAX_SCORE));
			complete_rewrite = 1;
			break;
		}
		/* fallthru */
	default:
		/* nothing */
		;
	}

	if (hashcmp(one->sha1, two->sha1)) {
		int abbrev = o->full_index ? 40 : DEFAULT_ABBREV;

		if (o->binary) {
			mmfile_t mf;
			if ((!fill_mmfile(&mf, one) && mmfile_is_binary(&mf)) ||
			    (!fill_mmfile(&mf, two) && mmfile_is_binary(&mf)))
				abbrev = 40;
		}
		len += snprintf(msg + len, sizeof(msg) - len,
				"index %.*s..%.*s",
				abbrev, sha1_to_hex(one->sha1),
				abbrev, sha1_to_hex(two->sha1));
		if (one->mode == two->mode)
			len += snprintf(msg + len, sizeof(msg) - len,
					" %06o", one->mode);
		len += snprintf(msg + len, sizeof(msg) - len, "\n");
	}

	if (len)
		msg[--len] = 0;
	xfrm_msg = len ? msg : NULL;

	if (!pgm &&
	    DIFF_FILE_VALID(one) && DIFF_FILE_VALID(two) &&
	    (S_IFMT & one->mode) != (S_IFMT & two->mode)) {
		/* a filepair that changes between file and symlink
		 * needs to be split into deletion and creation.
		 */
		struct diff_filespec *null = alloc_filespec(two->path);
		run_diff_cmd(NULL, name, other, one, null, xfrm_msg, o, 0);
		free(null);
		null = alloc_filespec(one->path);
		run_diff_cmd(NULL, name, other, null, two, xfrm_msg, o, 0);
		free(null);
	}
	else
		run_diff_cmd(pgm, name, other, one, two, xfrm_msg, o,
			     complete_rewrite);

	free(name_munged);
	free(other_munged);
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

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);

	diff_fill_sha1_info(p->one);
	diff_fill_sha1_info(p->two);

	builtin_checkdiff(name, other, p->one, p->two, o);
}

void diff_setup(struct diff_options *options)
{
	memset(options, 0, sizeof(*options));
	options->line_termination = '\n';
	options->break_opt = -1;
	options->rename_limit = -1;
	options->context = 3;
	options->msg_sep = "";

	options->change = diff_change;
	options->add_remove = diff_addremove;
	options->color_diff = diff_use_color_default;
	options->detect_rename = diff_detect_rename_default;
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

	if (options->find_copies_harder)
		options->detect_rename = DIFF_DETECT_COPY;

	if (options->output_format & (DIFF_FORMAT_NAME |
				      DIFF_FORMAT_NAME_STATUS |
				      DIFF_FORMAT_CHECKDIFF |
				      DIFF_FORMAT_NO_OUTPUT))
		options->output_format &= ~(DIFF_FORMAT_RAW |
					    DIFF_FORMAT_NUMSTAT |
					    DIFF_FORMAT_DIFFSTAT |
					    DIFF_FORMAT_SHORTSTAT |
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
				      DIFF_FORMAT_SUMMARY |
				      DIFF_FORMAT_CHECKDIFF))
		options->recursive = 1;
	/*
	 * Also pickaxe would not work very well if you do not say recursive
	 */
	if (options->pickaxe)
		options->recursive = 1;

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
	if (options->setup & DIFF_SETUP_USE_SIZE_CACHE)
		use_size_cache = 1;
	if (options->abbrev <= 0 || 40 < options->abbrev)
		options->abbrev = 40; /* full */

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

int diff_opt_parse(struct diff_options *options, const char **av, int ac)
{
	const char *arg = av[0];
	if (!strcmp(arg, "-p") || !strcmp(arg, "-u"))
		options->output_format |= DIFF_FORMAT_PATCH;
	else if (opt_arg(arg, 'U', "unified", &options->context))
		options->output_format |= DIFF_FORMAT_PATCH;
	else if (!strcmp(arg, "--raw"))
		options->output_format |= DIFF_FORMAT_RAW;
	else if (!strcmp(arg, "--patch-with-raw")) {
		options->output_format |= DIFF_FORMAT_PATCH | DIFF_FORMAT_RAW;
	}
	else if (!strcmp(arg, "--numstat")) {
		options->output_format |= DIFF_FORMAT_NUMSTAT;
	}
	else if (!strcmp(arg, "--shortstat")) {
		options->output_format |= DIFF_FORMAT_SHORTSTAT;
	}
	else if (!prefixcmp(arg, "--stat")) {
		char *end;
		int width = options->stat_width;
		int name_width = options->stat_name_width;
		arg += 6;
		end = (char *)arg;

		switch (*arg) {
		case '-':
			if (!prefixcmp(arg, "-width="))
				width = strtoul(arg + 7, &end, 10);
			else if (!prefixcmp(arg, "-name-width="))
				name_width = strtoul(arg + 12, &end, 10);
			break;
		case '=':
			width = strtoul(arg+1, &end, 10);
			if (*end == ',')
				name_width = strtoul(end+1, &end, 10);
		}

		/* Important! This checks all the error cases! */
		if (*end)
			return 0;
		options->output_format |= DIFF_FORMAT_DIFFSTAT;
		options->stat_name_width = name_width;
		options->stat_width = width;
	}
	else if (!strcmp(arg, "--check"))
		options->output_format |= DIFF_FORMAT_CHECKDIFF;
	else if (!strcmp(arg, "--summary"))
		options->output_format |= DIFF_FORMAT_SUMMARY;
	else if (!strcmp(arg, "--patch-with-stat")) {
		options->output_format |= DIFF_FORMAT_PATCH | DIFF_FORMAT_DIFFSTAT;
	}
	else if (!strcmp(arg, "-z"))
		options->line_termination = 0;
	else if (!prefixcmp(arg, "-l"))
		options->rename_limit = strtoul(arg+2, NULL, 10);
	else if (!strcmp(arg, "--full-index"))
		options->full_index = 1;
	else if (!strcmp(arg, "--binary")) {
		options->output_format |= DIFF_FORMAT_PATCH;
		options->binary = 1;
	}
	else if (!strcmp(arg, "-a") || !strcmp(arg, "--text")) {
		options->text = 1;
	}
	else if (!strcmp(arg, "--name-only"))
		options->output_format |= DIFF_FORMAT_NAME;
	else if (!strcmp(arg, "--name-status"))
		options->output_format |= DIFF_FORMAT_NAME_STATUS;
	else if (!strcmp(arg, "-R"))
		options->reverse_diff = 1;
	else if (!prefixcmp(arg, "-S"))
		options->pickaxe = arg + 2;
	else if (!strcmp(arg, "-s")) {
		options->output_format |= DIFF_FORMAT_NO_OUTPUT;
	}
	else if (!prefixcmp(arg, "-O"))
		options->orderfile = arg + 2;
	else if (!prefixcmp(arg, "--diff-filter="))
		options->filter = arg + 14;
	else if (!strcmp(arg, "--pickaxe-all"))
		options->pickaxe_opts = DIFF_PICKAXE_ALL;
	else if (!strcmp(arg, "--pickaxe-regex"))
		options->pickaxe_opts = DIFF_PICKAXE_REGEX;
	else if (!prefixcmp(arg, "-B")) {
		if ((options->break_opt =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
	}
	else if (!prefixcmp(arg, "-M")) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_RENAME;
	}
	else if (!prefixcmp(arg, "-C")) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_COPY;
	}
	else if (!strcmp(arg, "--find-copies-harder"))
		options->find_copies_harder = 1;
	else if (!strcmp(arg, "--abbrev"))
		options->abbrev = DEFAULT_ABBREV;
	else if (!prefixcmp(arg, "--abbrev=")) {
		options->abbrev = strtoul(arg + 9, NULL, 10);
		if (options->abbrev < MINIMUM_ABBREV)
			options->abbrev = MINIMUM_ABBREV;
		else if (40 < options->abbrev)
			options->abbrev = 40;
	}
	else if (!strcmp(arg, "--color"))
		options->color_diff = 1;
	else if (!strcmp(arg, "--no-color"))
		options->color_diff = 0;
	else if (!strcmp(arg, "-w") || !strcmp(arg, "--ignore-all-space"))
		options->xdl_opts |= XDF_IGNORE_WHITESPACE;
	else if (!strcmp(arg, "-b") || !strcmp(arg, "--ignore-space-change"))
		options->xdl_opts |= XDF_IGNORE_WHITESPACE_CHANGE;
	else if (!strcmp(arg, "--ignore-space-at-eol"))
		options->xdl_opts |= XDF_IGNORE_WHITESPACE_AT_EOL;
	else if (!strcmp(arg, "--color-words"))
		options->color_diff = options->color_diff_words = 1;
	else if (!strcmp(arg, "--no-renames"))
		options->detect_rename = 0;
	else
		return 0;
	return 1;
}

static int parse_num(const char **cp_p)
{
	unsigned long num, scale;
	int ch, dot;
	const char *cp = *cp_p;

	num = 0;
	scale = 1;
	dot = 0;
	for(;;) {
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
	return (num >= scale) ? MAX_SCORE : (MAX_SCORE * num / scale);
}

int diff_scoreopt_parse(const char *opt)
{
	int opt1, opt2, cmd;

	if (*opt++ != '-')
		return -1;
	cmd = *opt++;
	if (cmd != 'M' && cmd != 'C' && cmd != 'B')
		return -1; /* that is not a -M, -C nor -B option */

	opt1 = parse_num(&opt);
	if (cmd != 'B')
		opt2 = 0;
	else {
		if (*opt == 0)
			opt2 = 0;
		else if (*opt != '/')
			return -1; /* we expect -B80/99 or -B80 */
		else {
			opt++;
			opt2 = parse_num(&opt);
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
	diff_free_filespec_data(p->one);
	diff_free_filespec_data(p->two);
	free(p->one);
	free(p->two);
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
	if (!abbrev)
		return sha1_to_hex(sha1);
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

static void diff_flush_raw(struct diff_filepair *p,
			   struct diff_options *options)
{
	int two_paths;
	char status[10];
	int abbrev = options->abbrev;
	const char *path_one, *path_two;
	int inter_name_termination = '\t';
	int line_termination = options->line_termination;

	if (!line_termination)
		inter_name_termination = 0;

	path_one = p->one->path;
	path_two = p->two->path;
	if (line_termination) {
		path_one = quote_one(path_one);
		path_two = quote_one(path_two);
	}

	if (p->score)
		sprintf(status, "%c%03d", p->status,
			(int)(0.5 + p->score * 100.0/MAX_SCORE));
	else {
		status[0] = p->status;
		status[1] = 0;
	}
	switch (p->status) {
	case DIFF_STATUS_COPIED:
	case DIFF_STATUS_RENAMED:
		two_paths = 1;
		break;
	case DIFF_STATUS_ADDED:
	case DIFF_STATUS_DELETED:
		two_paths = 0;
		break;
	default:
		two_paths = 0;
		break;
	}
	if (!(options->output_format & DIFF_FORMAT_NAME_STATUS)) {
		printf(":%06o %06o %s ",
		       p->one->mode, p->two->mode,
		       diff_unique_abbrev(p->one->sha1, abbrev));
		printf("%s ",
		       diff_unique_abbrev(p->two->sha1, abbrev));
	}
	printf("%s%c%s", status, inter_name_termination, path_one);
	if (two_paths)
		printf("%c%s", inter_name_termination, path_two);
	putchar(line_termination);
	if (path_one != p->one->path)
		free((void*)path_one);
	if (path_two != p->two->path)
		free((void*)path_two);
}

static void diff_flush_name(struct diff_filepair *p, struct diff_options *opt)
{
	char *path = p->two->path;

	if (opt->line_termination)
		path = quote_one(p->two->path);
	printf("%s%c", path, opt->line_termination);
	if (p->two->path != path)
		free(path);
}

int diff_unmodified_pair(struct diff_filepair *p)
{
	/* This function is written stricter than necessary to support
	 * the currently implemented transformers, but the idea is to
	 * let transformers to produce diff_filepairs any way they want,
	 * and filter and clean them up here before producing the output.
	 */
	struct diff_filespec *one, *two;

	if (DIFF_PAIR_UNMERGED(p))
		return 0; /* unmerged is interesting */

	one = p->one;
	two = p->two;

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
	    !hashcmp(one->sha1, two->sha1))
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
		return; /* no tree diffs in patch format */

	run_diffstat(p, o, diffstat);
}

static void diff_flush_checkdiff(struct diff_filepair *p,
		struct diff_options *o)
{
	if (diff_unmodified_pair(p))
		return;

	if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
	    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
		return; /* no tree diffs in patch format */

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
	fprintf(stderr, "score %d, status %c stays %d broken %d\n",
		p->score, p->status ? p->status : '?',
		p->source_stays, p->broken_pair);
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
	int i, j;
	struct diff_filepair *p, *pp;
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
			if (p->source_stays) {
				p->status = DIFF_STATUS_COPIED;
				continue;
			}
			/* See if there is some other filepair that
			 * copies from the same source as us.  If so
			 * we are a copy.  Otherwise we are either a
			 * copy if the path stays, or a rename if it
			 * does not, but we already handled "stays" case.
			 */
			for (j = i + 1; j < q->nr; j++) {
				pp = q->queue[j];
				if (strcmp(pp->one->path, p->one->path))
					continue; /* not us */
				if (!DIFF_PAIR_RENAME(pp))
					continue; /* not a rename/copy */
				/* pp is a rename/copy from the same source */
				p->status = DIFF_STATUS_COPIED;
				break;
			}
			if (!p->status)
				p->status = DIFF_STATUS_RENAMED;
		}
		else if (hashcmp(p->one->sha1, p->two->sha1) ||
			 p->one->mode != p->two->mode)
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
	else if (fmt & DIFF_FORMAT_NAME)
		diff_flush_name(p, opt);
}

static void show_file_mode_name(const char *newdelete, struct diff_filespec *fs)
{
	char *name = quote_one(fs->path);
	if (fs->mode)
		printf(" %s mode %06o %s\n", newdelete, fs->mode, name);
	else
		printf(" %s %s\n", newdelete, name);
	free(name);
}


static void show_mode_change(struct diff_filepair *p, int show_name)
{
	if (p->one->mode && p->two->mode && p->one->mode != p->two->mode) {
		if (show_name) {
			char *name = quote_one(p->two->path);
			printf(" mode change %06o => %06o %s\n",
			       p->one->mode, p->two->mode, name);
			free(name);
		}
		else
			printf(" mode change %06o => %06o\n",
			       p->one->mode, p->two->mode);
	}
}

static void show_rename_copy(const char *renamecopy, struct diff_filepair *p)
{
	char *names = pprint_rename(p->one->path, p->two->path);

	printf(" %s %s (%d%%)\n", renamecopy, names,
	       (int)(0.5 + p->score * 100.0/MAX_SCORE));
	free(names);
	show_mode_change(p, 0);
}

static void diff_summary(struct diff_filepair *p)
{
	switch(p->status) {
	case DIFF_STATUS_DELETED:
		show_file_mode_name("delete", p->one);
		break;
	case DIFF_STATUS_ADDED:
		show_file_mode_name("create", p->two);
		break;
	case DIFF_STATUS_COPIED:
		show_rename_copy("copy", p);
		break;
	case DIFF_STATUS_RENAMED:
		show_rename_copy("rename", p);
		break;
	default:
		if (p->score) {
			char *name = quote_one(p->two->path);
			printf(" rewrite %s (%d%%)\n", name,
				(int)(0.5 + p->score * 100.0/MAX_SCORE));
			free(name);
			show_mode_change(p, 0);
		} else	show_mode_change(p, 1);
		break;
	}
}

struct patch_id_t {
	struct xdiff_emit_state xm;
	SHA_CTX *ctx;
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

	SHA1_Update(data->ctx, line, new_len);
	data->patchlen += new_len;
}

/* returns 0 upon success, and writes result into sha1 */
static int diff_get_patch_id(struct diff_options *options, unsigned char *sha1)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	SHA_CTX ctx;
	struct patch_id_t data;
	char buffer[PATH_MAX * 4 + 20];

	SHA1_Init(&ctx);
	memset(&data, 0, sizeof(struct patch_id_t));
	data.ctx = &ctx;
	data.xm.consume = patch_id_consume;

	for (i = 0; i < q->nr; i++) {
		xpparam_t xpp;
		xdemitconf_t xecfg;
		xdemitcb_t ecb;
		mmfile_t mf1, mf2;
		struct diff_filepair *p = q->queue[i];
		int len1, len2;

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

		/* Maybe hash p->two? into the patch id? */
		if (mmfile_is_binary(&mf2))
			continue;

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
		SHA1_Update(&ctx, buffer, len1);

		xpp.flags = XDF_NEED_MINIMAL;
		xecfg.ctxlen = 3;
		xecfg.flags = XDL_EMIT_FUNCNAMES;
		ecb.outf = xdiff_outf;
		ecb.priv = &data;
		xdl_diff(&mf1, &mf2, &xpp, &xecfg, &ecb);
	}

	SHA1_Final(sha1, &ctx);
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
	q->queue = NULL;
	q->nr = q->alloc = 0;

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

void diff_flush(struct diff_options *options)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i, output_format = options->output_format;
	int separator = 0;

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

	if (output_format & (DIFF_FORMAT_DIFFSTAT|DIFF_FORMAT_SHORTSTAT|DIFF_FORMAT_NUMSTAT)) {
		struct diffstat_t diffstat;

		memset(&diffstat, 0, sizeof(struct diffstat_t));
		diffstat.xm.consume = diffstat_consume;
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				diff_flush_stat(p, options, &diffstat);
		}
		if (output_format & DIFF_FORMAT_NUMSTAT)
			show_numstat(&diffstat, options);
		if (output_format & DIFF_FORMAT_DIFFSTAT)
			show_stats(&diffstat, options);
		else if (output_format & DIFF_FORMAT_SHORTSTAT)
			show_shortstats(&diffstat);
		separator++;
	}

	if (output_format & DIFF_FORMAT_SUMMARY && !is_summary_empty(q)) {
		for (i = 0; i < q->nr; i++)
			diff_summary(q->queue[i]);
		separator++;
	}

	if (output_format & DIFF_FORMAT_PATCH) {
		if (separator) {
			if (options->stat_sep) {
				/* attach patch instead of inline */
				fputs(options->stat_sep, stdout);
			} else {
				putchar(options->line_termination);
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
	q->queue = NULL;
	q->nr = q->alloc = 0;
}

static void diffcore_apply_filter(const char *filter)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

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

void diffcore_std(struct diff_options *options)
{
	if (options->break_opt != -1)
		diffcore_break(options->break_opt);
	if (options->detect_rename)
		diffcore_rename(options);
	if (options->break_opt != -1)
		diffcore_merge_broken();
	if (options->pickaxe)
		diffcore_pickaxe(options->pickaxe, options->pickaxe_opts);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	diff_resolve_rename_copy();
	diffcore_apply_filter(options->filter);
}


void diffcore_std_no_resolve(struct diff_options *options)
{
	if (options->pickaxe)
		diffcore_pickaxe(options->pickaxe, options->pickaxe_opts);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	diffcore_apply_filter(options->filter);
}

void diff_addremove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
{
	char concatpath[PATH_MAX];
	struct diff_filespec *one, *two;

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
	if (options->reverse_diff)
		addremove = (addremove == '+' ? '-' :
			     addremove == '-' ? '+' : addremove);

	if (!path) path = "";
	sprintf(concatpath, "%s%s", base, path);
	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);

	if (addremove != '+')
		fill_filespec(one, sha1, mode);
	if (addremove != '-')
		fill_filespec(two, sha1, mode);

	diff_queue(&diff_queued_diff, one, two);
}

void diff_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path) 
{
	char concatpath[PATH_MAX];
	struct diff_filespec *one, *two;

	if (options->reverse_diff) {
		unsigned tmp;
		const unsigned char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_c = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_c;
	}
	if (!path) path = "";
	sprintf(concatpath, "%s%s", base, path);
	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);
	fill_filespec(one, old_sha1, old_mode);
	fill_filespec(two, new_sha1, new_mode);

	diff_queue(&diff_queued_diff, one, two);
}

void diff_unmerge(struct diff_options *options,
		  const char *path,
		  unsigned mode, const unsigned char *sha1)
{
	struct diff_filespec *one, *two;
	one = alloc_filespec(path);
	two = alloc_filespec(path);
	fill_filespec(one, sha1, mode);
	diff_queue(&diff_queued_diff, one, two)->is_unmerged = 1;
}
