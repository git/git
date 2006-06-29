/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "cache.h"
#include "quote.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "xdiff-interface.h"

static int use_size_cache;

static int diff_rename_limit_default = -1;
static int diff_use_color_default = 0;

enum color_diff {
	DIFF_RESET = 0,
	DIFF_PLAIN = 1,
	DIFF_METAINFO = 2,
	DIFF_FRAGINFO = 3,
	DIFF_FILE_OLD = 4,
	DIFF_FILE_NEW = 5,
};

#define COLOR_NORMAL  ""
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"
#define COLOR_UL      "\033[4m"
#define COLOR_BLINK   "\033[5m"
#define COLOR_REVERSE "\033[7m"
#define COLOR_RESET   "\033[m"

#define COLOR_BLACK   "\033[30m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"

static const char *diff_colors[] = {
	[DIFF_RESET]    = COLOR_RESET,
	[DIFF_PLAIN]    = COLOR_NORMAL,
	[DIFF_METAINFO] = COLOR_BOLD,
	[DIFF_FRAGINFO] = COLOR_CYAN,
	[DIFF_FILE_OLD] = COLOR_RED,
	[DIFF_FILE_NEW] = COLOR_GREEN,
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
	die("bad config variable '%s'", var);
}

static const char *parse_diff_color_value(const char *value, const char *var)
{
	if (!strcasecmp(value, "normal"))
		return COLOR_NORMAL;
	if (!strcasecmp(value, "bold"))
		return COLOR_BOLD;
	if (!strcasecmp(value, "dim"))
		return COLOR_DIM;
	if (!strcasecmp(value, "ul"))
		return COLOR_UL;
	if (!strcasecmp(value, "blink"))
		return COLOR_BLINK;
	if (!strcasecmp(value, "reverse"))
		return COLOR_REVERSE;
	if (!strcasecmp(value, "reset"))
		return COLOR_RESET;
	if (!strcasecmp(value, "black"))
		return COLOR_BLACK;
	if (!strcasecmp(value, "red"))
		return COLOR_RED;
	if (!strcasecmp(value, "green"))
		return COLOR_GREEN;
	if (!strcasecmp(value, "yellow"))
		return COLOR_YELLOW;
	if (!strcasecmp(value, "blue"))
		return COLOR_BLUE;
	if (!strcasecmp(value, "magenta"))
		return COLOR_MAGENTA;
	if (!strcasecmp(value, "cyan"))
		return COLOR_CYAN;
	if (!strcasecmp(value, "white"))
		return COLOR_WHITE;
	die("bad config value '%s' for variable '%s'", value, var);
}

int git_diff_config(const char *var, const char *value)
{
	if (!strcmp(var, "diff.renamelimit")) {
		diff_rename_limit_default = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "diff.color")) {
		if (!value)
			diff_use_color_default = 1; /* bool */
		else if (!strcasecmp(value, "auto"))
			diff_use_color_default = isatty(1);
		else if (!strcasecmp(value, "never"))
			diff_use_color_default = 0;
		else if (!strcasecmp(value, "always"))
			diff_use_color_default = 1;
		else
			diff_use_color_default = git_config_bool(var, value);
		return 0;
	}
	if (!strncmp(var, "diff.color.", 11)) {
		int slot = parse_diff_color_slot(var, 11);
		diff_colors[slot] = parse_diff_color_value(value, var);
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
		return strdup(str);
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

static void copy_file(int prefix, const char *data, int size)
{
	int ch, nl_just_seen = 1;
	while (0 < size--) {
		ch = *data++;
		if (nl_just_seen)
			putchar(prefix);
		putchar(ch);
		if (ch == '\n')
			nl_just_seen = 1;
		else
			nl_just_seen = 0;
	}
	if (!nl_just_seen)
		printf("\n\\ No newline at end of file\n");
}

static void emit_rewrite_diff(const char *name_a,
			      const char *name_b,
			      struct diff_filespec *one,
			      struct diff_filespec *two)
{
	int lc_a, lc_b;
	diff_populate_filespec(one, 0);
	diff_populate_filespec(two, 0);
	lc_a = count_lines(one->data, one->size);
	lc_b = count_lines(two->data, two->size);
	printf("--- %s\n+++ %s\n@@ -", name_a, name_b);
	print_line_count(lc_a);
	printf(" +");
	print_line_count(lc_b);
	printf(" @@\n");
	if (lc_a)
		copy_file('-', one->data, one->size);
	if (lc_b)
		copy_file('+', two->data, two->size);
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

struct emit_callback {
	struct xdiff_emit_state xm;
	int nparents, color_diff;
	const char **label_path;
};

static inline const char *get_color(int diff_use_color, enum color_diff ix)
{
	if (diff_use_color)
		return diff_colors[ix];
	return "";
}

static void fn_out_consume(void *priv, char *line, unsigned long len)
{
	int i;
	struct emit_callback *ecbdata = priv;
	const char *set = get_color(ecbdata->color_diff, DIFF_METAINFO);
	const char *reset = get_color(ecbdata->color_diff, DIFF_RESET);

	if (ecbdata->label_path[0]) {
		printf("%s--- %s%s\n", set, ecbdata->label_path[0], reset);
		printf("%s+++ %s%s\n", set, ecbdata->label_path[1], reset);
		ecbdata->label_path[0] = ecbdata->label_path[1] = NULL;
	}

	/* This is not really necessary for now because
	 * this codepath only deals with two-way diffs.
	 */
	for (i = 0; i < len && line[i] == '@'; i++)
		;
	if (2 <= i && i < len && line[i] == ' ') {
		ecbdata->nparents = i - 1;
		set = get_color(ecbdata->color_diff, DIFF_FRAGINFO);
	}
	else if (len < ecbdata->nparents)
		set = reset;
	else {
		int nparents = ecbdata->nparents;
		int color = DIFF_PLAIN;
		for (i = 0; i < nparents && len; i++) {
			if (line[i] == '-')
				color = DIFF_FILE_OLD;
			else if (line[i] == '+')
				color = DIFF_FILE_NEW;
		}
		set = get_color(ecbdata->color_diff, color);
	}
	if (len > 0 && line[len-1] == '\n')
		len--;
	printf("%s%.*s%s\n", set, (int) len, line, reset);
}

static char *pprint_rename(const char *a, const char *b)
{
	const char *old = a;
	const char *new = b;
	char *name = NULL;
	int pfx_length, sfx_length;
	int len_a = strlen(a);
	int len_b = strlen(b);

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
		x->name = strdup(name_a);
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

static const char pluses[] = "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
static const char minuses[]= "----------------------------------------------------------------------";
const char mime_boundary_leader[] = "------------";

static void show_stats(struct diffstat_t* data)
{
	int i, len, add, del, total, adds = 0, dels = 0;
	int max, max_change = 0, max_len = 0;
	int total_files = data->nr;

	if (data->nr == 0)
		return;

	for (i = 0; i < data->nr; i++) {
		struct diffstat_file *file = data->files[i];

		len = strlen(file->name);
		if (max_len < len)
			max_len = len;

		if (file->is_binary || file->is_unmerged)
			continue;
		if (max_change < file->added + file->deleted)
			max_change = file->added + file->deleted;
	}

	for (i = 0; i < data->nr; i++) {
		const char *prefix = "";
		char *name = data->files[i]->name;
		int added = data->files[i]->added;
		int deleted = data->files[i]->deleted;

		if (0 < (len = quote_c_style(name, NULL, NULL, 0))) {
			char *qname = xmalloc(len + 1);
			quote_c_style(name, qname, NULL, 0);
			free(name);
			data->files[i]->name = name = qname;
		}

		/*
		 * "scale" the filename
		 */
		len = strlen(name);
		max = max_len;
		if (max > 50)
			max = 50;
		if (len > max) {
			char *slash;
			prefix = "...";
			max -= 3;
			name += len - max;
			slash = strchr(name, '/');
			if (slash)
				name = slash;
		}
		len = max;

		/*
		 * scale the add/delete
		 */
		max = max_change;
		if (max + len > 70)
			max = 70 - len;

		if (data->files[i]->is_binary) {
			printf(" %s%-*s |  Bin\n", prefix, len, name);
			goto free_diffstat_file;
		}
		else if (data->files[i]->is_unmerged) {
			printf(" %s%-*s |  Unmerged\n", prefix, len, name);
			goto free_diffstat_file;
		}
		else if (!data->files[i]->is_renamed &&
			 (added + deleted == 0)) {
			total_files--;
			goto free_diffstat_file;
		}

		add = added;
		del = deleted;
		total = add + del;
		adds += add;
		dels += del;

		if (max_change > 0) {
			total = (total * max + max_change / 2) / max_change;
			add = (add * max + max_change / 2) / max_change;
			del = total - add;
		}
		printf(" %s%-*s |%5d %.*s%.*s\n", prefix,
				len, name, added + deleted,
				add, pluses, del, minuses);
	free_diffstat_file:
		free(data->files[i]->name);
		free(data->files[i]);
	}
	free(data->files);
	printf(" %d files changed, %d insertions(+), %d deletions(-)\n",
			total_files, adds, dels);
}

struct checkdiff_t {
	struct xdiff_emit_state xm;
	const char *filename;
	int lineno;
};

static void checkdiff_consume(void *priv, char *line, unsigned long len)
{
	struct checkdiff_t *data = priv;

	if (line[0] == '+') {
		int i, spaces = 0;

		data->lineno++;

		/* check space before tab */
		for (i = 1; i < len && (line[i] == ' ' || line[i] == '\t'); i++)
			if (line[i] == ' ')
				spaces++;
		if (line[i - 1] == '\t' && spaces)
			printf("%s:%d: space before tab:%.*s\n",
				data->filename, data->lineno, (int)len, line);

		/* check white space at line end */
		if (line[len - 1] == '\n')
			len--;
		if (isspace(line[len - 1]))
			printf("%s:%d: white space at end: %.*s\n",
				data->filename, data->lineno, (int)len, line);
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
	deflateInit(&stream, Z_BEST_COMPRESSION);
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

static void emit_binary_diff(mmfile_t *one, mmfile_t *two)
{
	void *cp;
	void *delta;
	void *deflated;
	void *data;
	unsigned long orig_size;
	unsigned long delta_size;
	unsigned long deflate_size;
	unsigned long data_size;

	printf("GIT binary patch\n");
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

#define FIRST_FEW_BYTES 8000
static int mmfile_is_binary(mmfile_t *mf)
{
	long sz = mf->size;
	if (FIRST_FEW_BYTES < sz)
		sz = FIRST_FEW_BYTES;
	if (memchr(mf->ptr, 0, sz))
		return 1;
	return 0;
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
	const char *set = get_color(o->color_diff, DIFF_METAINFO);
	const char *reset = get_color(o->color_diff, DIFF_RESET);

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
			emit_rewrite_diff(name_a, name_b, one, two);
			goto free_ab_and_return;
		}
	}

	if (fill_mmfile(&mf1, one) < 0 || fill_mmfile(&mf2, two) < 0)
		die("unable to read files to diff");

	if (mmfile_is_binary(&mf1) || mmfile_is_binary(&mf2)) {
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
		else if (!strncmp(diffopts, "--unified=", 10))
			xecfg.ctxlen = strtoul(diffopts + 10, NULL, 10);
		else if (!strncmp(diffopts, "-u", 2))
			xecfg.ctxlen = strtoul(diffopts + 2, NULL, 10);
		ecb.outf = xdiff_outf;
		ecb.priv = &ecbdata;
		ecbdata.xm.consume = fn_out_consume;
		xdl_diff(&mf1, &mf2, &xpp, &xecfg, &ecb);
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
			     struct diff_filespec *two)
{
	mmfile_t mf1, mf2;
	struct checkdiff_t data;

	if (!two)
		return;

	memset(&data, 0, sizeof(data));
	data.xm.consume = checkdiff_consume;
	data.filename = name_b ? name_b : name_a;
	data.lineno = 0;

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
		memcpy(spec->sha1, sha1, 20);
		spec->sha1_valid = !!memcmp(sha1, null_sha1, 20);
	}
}

/*
 * Given a name and sha1 pair, if the dircache tells us the file in
 * the work tree has that object contents, return true, so that
 * prepare_temp_file() does not have to inflate and extract.
 */
static int work_tree_matches(const char *name, const unsigned char *sha1)
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

	len = strlen(name);
	pos = cache_name_pos(name, len);
	if (pos < 0)
		return 0;
	ce = active_cache[pos];
	if ((lstat(name, &st) < 0) ||
	    !S_ISREG(st.st_mode) || /* careful! */
	    ce_match_stat(ce, &st, 0) ||
	    memcmp(sha1, ce->sha1, 20))
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
		cmp = memcmp(e->sha1, sha1, 20);
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
	memcpy(e->sha1, sha1, 20);
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
	    work_tree_matches(s->path, s->sha1)) {
		struct stat st;
		int fd;
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
		s->data = mmap(NULL, s->size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (s->data == MAP_FAILED)
			goto err_empty;
		s->should_munmap = 1;
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
	if (write(fd, blob, size) != size)
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
	    work_tree_matches(name, one->sha1)) {
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
		memset(one->sha1, 0, 20);
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

	if (memcmp(one->sha1, two->sha1, 20)) {
		int abbrev = o->full_index ? 40 : DEFAULT_ABBREV;

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

	builtin_checkdiff(name, other, p->one, p->two);
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
}

int diff_setup_done(struct diff_options *options)
{
	if ((options->find_copies_harder &&
	     options->detect_rename != DIFF_DETECT_COPY) ||
	    (0 <= options->rename_limit && !options->detect_rename))
		return -1;

	if (options->output_format & (DIFF_FORMAT_NAME |
				      DIFF_FORMAT_NAME_STATUS |
				      DIFF_FORMAT_CHECKDIFF |
				      DIFF_FORMAT_NO_OUTPUT))
		options->output_format &= ~(DIFF_FORMAT_RAW |
					    DIFF_FORMAT_DIFFSTAT |
					    DIFF_FORMAT_SUMMARY |
					    DIFF_FORMAT_PATCH);

	/*
	 * These cases always need recursive; we do not drop caller-supplied
	 * recursive bits for other formats here.
	 */
	if (options->output_format & (DIFF_FORMAT_PATCH |
				      DIFF_FORMAT_DIFFSTAT |
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
	else if (!strcmp(arg, "--stat"))
		options->output_format |= DIFF_FORMAT_DIFFSTAT;
	else if (!strcmp(arg, "--check"))
		options->output_format |= DIFF_FORMAT_CHECKDIFF;
	else if (!strcmp(arg, "--summary"))
		options->output_format |= DIFF_FORMAT_SUMMARY;
	else if (!strcmp(arg, "--patch-with-stat")) {
		options->output_format |= DIFF_FORMAT_PATCH | DIFF_FORMAT_DIFFSTAT;
	}
	else if (!strcmp(arg, "-z"))
		options->line_termination = 0;
	else if (!strncmp(arg, "-l", 2))
		options->rename_limit = strtoul(arg+2, NULL, 10);
	else if (!strcmp(arg, "--full-index"))
		options->full_index = 1;
	else if (!strcmp(arg, "--binary")) {
		options->output_format |= DIFF_FORMAT_PATCH;
		options->full_index = options->binary = 1;
	}
	else if (!strcmp(arg, "--name-only"))
		options->output_format |= DIFF_FORMAT_NAME;
	else if (!strcmp(arg, "--name-status"))
		options->output_format |= DIFF_FORMAT_NAME_STATUS;
	else if (!strcmp(arg, "-R"))
		options->reverse_diff = 1;
	else if (!strncmp(arg, "-S", 2))
		options->pickaxe = arg + 2;
	else if (!strcmp(arg, "-s")) {
		options->output_format |= DIFF_FORMAT_NO_OUTPUT;
	}
	else if (!strncmp(arg, "-O", 2))
		options->orderfile = arg + 2;
	else if (!strncmp(arg, "--diff-filter=", 14))
		options->filter = arg + 14;
	else if (!strcmp(arg, "--pickaxe-all"))
		options->pickaxe_opts = DIFF_PICKAXE_ALL;
	else if (!strcmp(arg, "--pickaxe-regex"))
		options->pickaxe_opts = DIFF_PICKAXE_REGEX;
	else if (!strncmp(arg, "-B", 2)) {
		if ((options->break_opt =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
	}
	else if (!strncmp(arg, "-M", 2)) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_RENAME;
	}
	else if (!strncmp(arg, "-C", 2)) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_COPY;
	}
	else if (!strcmp(arg, "--find-copies-harder"))
		options->find_copies_harder = 1;
	else if (!strcmp(arg, "--abbrev"))
		options->abbrev = DEFAULT_ABBREV;
	else if (!strncmp(arg, "--abbrev=", 9)) {
		options->abbrev = strtoul(arg + 9, NULL, 10);
		if (options->abbrev < MINIMUM_ABBREV)
			options->abbrev = MINIMUM_ABBREV;
		else if (40 < options->abbrev)
			options->abbrev = 40;
	}
	else if (!strcmp(arg, "--color"))
		options->color_diff = 1;
	else if (!strcmp(arg, "-w") || !strcmp(arg, "--ignore-all-space"))
		options->xdl_opts |= XDF_IGNORE_WHITESPACE;
	else if (!strcmp(arg, "-b") || !strcmp(arg, "--ignore-space-change"))
		options->xdl_opts |= XDF_IGNORE_WHITESPACE_CHANGE;
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
	struct diff_filepair *dp = xmalloc(sizeof(*dp));
	dp->one = one;
	dp->two = two;
	dp->score = 0;
	dp->status = 0;
	dp->source_stays = 0;
	dp->broken_pair = 0;
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

static void diff_flush_name(struct diff_filepair *p, int line_termination)
{
	char *path = p->two->path;

	if (line_termination)
		path = quote_one(p->two->path);
	printf("%s%c", path, line_termination);
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
	    !memcmp(one->sha1, two->sha1, sizeof(one->sha1)))
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
		else if (memcmp(p->one->sha1, p->two->sha1, 20) ||
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
		diff_flush_name(p, opt->line_termination);
}

static void show_file_mode_name(const char *newdelete, struct diff_filespec *fs)
{
	if (fs->mode)
		printf(" %s mode %06o %s\n", newdelete, fs->mode, fs->path);
	else
		printf(" %s %s\n", newdelete, fs->path);
}


static void show_mode_change(struct diff_filepair *p, int show_name)
{
	if (p->one->mode && p->two->mode && p->one->mode != p->two->mode) {
		if (show_name)
			printf(" mode change %06o => %06o %s\n",
			       p->one->mode, p->two->mode, p->two->path);
		else
			printf(" mode change %06o => %06o\n",
			       p->one->mode, p->two->mode);
	}
}

static void show_rename_copy(const char *renamecopy, struct diff_filepair *p)
{
	const char *old, *new;

	/* Find common prefix */
	old = p->one->path;
	new = p->two->path;
	while (1) {
		const char *slash_old, *slash_new;
		slash_old = strchr(old, '/');
		slash_new = strchr(new, '/');
		if (!slash_old ||
		    !slash_new ||
		    slash_old - old != slash_new - new ||
		    memcmp(old, new, slash_new - new))
			break;
		old = slash_old + 1;
		new = slash_new + 1;
	}
	/* p->one->path thru old is the common prefix, and old and new
	 * through the end of names are renames
	 */
	if (old != p->one->path)
		printf(" %s %.*s{%s => %s} (%d%%)\n", renamecopy,
		       (int)(old - p->one->path), p->one->path,
		       old, new, (int)(0.5 + p->score * 100.0/MAX_SCORE));
	else
		printf(" %s %s => %s (%d%%)\n", renamecopy,
		       p->one->path, p->two->path,
		       (int)(0.5 + p->score * 100.0/MAX_SCORE));
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
			printf(" rewrite %s (%d%%)\n", p->two->path,
				(int)(0.5 + p->score * 100.0/MAX_SCORE));
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
	if (!strncmp(line, "@@ -", 4))
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

	if (output_format & DIFF_FORMAT_DIFFSTAT) {
		struct diffstat_t diffstat;

		memset(&diffstat, 0, sizeof(struct diffstat_t));
		diffstat.xm.consume = diffstat_consume;
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (check_pair_status(p))
				diff_flush_stat(p, options, &diffstat);
		}
		show_stats(&diffstat);
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
		  const char *path)
{
	struct diff_filespec *one, *two;
	one = alloc_filespec(path);
	two = alloc_filespec(path);
	diff_queue(&diff_queued_diff, one, two);
}
