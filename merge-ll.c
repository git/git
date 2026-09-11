/*
 * Low level 3-way in-core file merge.
 *
 * Copyright (c) 2007 Junio C Hamano
 */

#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "config.h"
#include "convert.h"
#include "attr.h"
#include "xdiff-interface.h"
#include "run-command.h"
#include "merge-ll.h"
#include "quote.h"
#include "strbuf.h"
#include "gettext.h"
#include "tempfile.h"

struct ll_merge_driver;

typedef enum ll_merge_result (*ll_merge_fn)(const struct ll_merge_driver *,
			   mmbuffer_t *result,
			   const char *path,
			   mmfile_t *orig, const char *orig_name,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   const struct ll_merge_options *opts,
			   int marker_size);

struct ll_merge_driver {
	const char *name;
	const char *description;
	ll_merge_fn fn;
	char *recursive;
	struct ll_merge_driver *next;
	char *cmdline;
};

static struct attr_check *merge_attributes;
static struct attr_check *load_merge_attributes(void)
{
	if (!merge_attributes)
		merge_attributes = attr_check_initl("merge", "conflict-marker-size", NULL);
	return merge_attributes;
}

void reset_merge_attributes(void)
{
	attr_check_free(merge_attributes);
	merge_attributes = NULL;
}

/*
 * Built-in low-levels
 */
static enum ll_merge_result ll_binary_merge(const struct ll_merge_driver *drv UNUSED,
			   mmbuffer_t *result,
			   const char *path UNUSED,
			   mmfile_t *orig, const char *orig_name UNUSED,
			   mmfile_t *src1, const char *name1 UNUSED,
			   mmfile_t *src2, const char *name2 UNUSED,
			   const struct ll_merge_options *opts,
			   int marker_size UNUSED)
{
	enum ll_merge_result ret;
	mmfile_t *stolen;
	assert(opts);

	/*
	 * The tentative merge result is the common ancestor for an
	 * internal merge.  For the final merge, it is "ours" by
	 * default but -Xours/-Xtheirs can tweak the choice.
	 */
	if (opts->virtual_ancestor) {
		stolen = orig;
		ret = LL_MERGE_OK;
	} else {
		switch (opts->variant) {
		default:
			ret = LL_MERGE_BINARY_CONFLICT;
			stolen = src1;
			break;
		case XDL_MERGE_FAVOR_OURS:
			ret = LL_MERGE_OK;
			stolen = src1;
			break;
		case XDL_MERGE_FAVOR_THEIRS:
			ret = LL_MERGE_OK;
			stolen = src2;
			break;
		}
	}

	result->ptr = stolen->ptr;
	result->size = stolen->size;
	stolen->ptr = NULL;

	return ret;
}

static enum ll_merge_result ll_xdl_merge(const struct ll_merge_driver *drv_unused,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig, const char *orig_name,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			const struct ll_merge_options *opts,
			int marker_size)
{
	enum ll_merge_result ret;
	xmparam_t xmp;
	int status;
	assert(opts);

	if (orig->size > MAX_XDIFF_SIZE ||
	    src1->size > MAX_XDIFF_SIZE ||
	    src2->size > MAX_XDIFF_SIZE ||
	    buffer_is_binary(orig->ptr, orig->size) ||
	    buffer_is_binary(src1->ptr, src1->size) ||
	    buffer_is_binary(src2->ptr, src2->size)) {
		return ll_binary_merge(drv_unused, result,
				       path,
				       orig, orig_name,
				       src1, name1,
				       src2, name2,
				       opts, marker_size);
	}

	memset(&xmp, 0, sizeof(xmp));
	xmp.level = XDL_MERGE_ZEALOUS;
	xmp.favor = opts->variant;
	xmp.xpp.flags = opts->xdl_opts;
	if (opts->conflict_style >= 0)
		xmp.style = opts->conflict_style;
	else if (git_xmerge_style >= 0)
		xmp.style = git_xmerge_style;
	if (marker_size > 0)
		xmp.marker_size = marker_size;
	xmp.ancestor = orig_name;
	xmp.file1 = name1;
	xmp.file2 = name2;
	status = xdl_merge(orig, src1, src2, &xmp, result);
	ret = (status > 0) ? LL_MERGE_CONFLICT : status;
	return ret;
}

static enum ll_merge_result ll_union_merge(const struct ll_merge_driver *drv_unused,
			  mmbuffer_t *result,
			  const char *path,
			  mmfile_t *orig, const char *orig_name,
			  mmfile_t *src1, const char *name1,
			  mmfile_t *src2, const char *name2,
			  const struct ll_merge_options *opts,
			  int marker_size)
{
	/* Use union favor */
	struct ll_merge_options o;
	assert(opts);
	o = *opts;
	o.variant = XDL_MERGE_FAVOR_UNION;
	return ll_xdl_merge(drv_unused, result, path,
			    orig, orig_name, src1, name1, src2, name2,
			    &o, marker_size);
}

#define LL_BINARY_MERGE 0
#define LL_TEXT_MERGE 1
#define LL_UNION_MERGE 2
static struct ll_merge_driver ll_merge_drv[] = {
	{ "binary", "built-in binary merge", ll_binary_merge },
	{ "text", "built-in 3-way text merge", ll_xdl_merge },
	{ "union", "built-in union merge", ll_union_merge },
};

static struct tempfile *create_temp(mmfile_t *src)
{
	struct tempfile *t = xmks_tempfile(".merge_file_XXXXXX");
	if (write_in_full(t->fd, src->ptr, src->size) < 0 ||
	    close_tempfile_gently(t) < 0)
		die_errno("unable to write temp-file");
	return t;
}

static const char *temp_path_basename(struct tempfile *t)
{
	/*
	 * basename() takes a non-const pointer because it can
	 * modify the input string to remove trailing directory
	 * separators. We know that we don't have any because
	 * this is a clean path generated from our vanilla
	 * tempfile template.
	 *
	 * So casting away the const here is safe, albeit gross.
	 */
	return basename((char *)get_tempfile_path(t));
}

/*
 * User defined low-level merge driver support.
 */
static enum ll_merge_result ll_ext_merge(const struct ll_merge_driver *fn,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig, const char *orig_name,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			const struct ll_merge_options *opts,
			int marker_size)
{
	struct tempfile *tmp_o, *tmp_a, *tmp_b;
	struct strbuf cmd = STRBUF_INIT;
	const char *format = fn->cmdline;
	struct child_process child = CHILD_PROCESS_INIT;
	int status;
	struct strbuf result_buf = STRBUF_INIT;
	enum ll_merge_result ret;
	assert(opts);

	if (!fn->cmdline)
		die("custom merge driver %s lacks command line.", fn->name);

	result->ptr = NULL;
	result->size = 0;
	tmp_o = create_temp(orig);
	tmp_a = create_temp(src1);
	tmp_b = create_temp(src2);

	while (strbuf_expand_step(&cmd, &format)) {
		if (skip_prefix(format, "%", &format))
			strbuf_addch(&cmd, '%');
		else if (skip_prefix(format, "O", &format))
			strbuf_addstr(&cmd, temp_path_basename(tmp_o));
		else if (skip_prefix(format, "A", &format))
			strbuf_addstr(&cmd, temp_path_basename(tmp_a));
		else if (skip_prefix(format, "B", &format))
			strbuf_addstr(&cmd, temp_path_basename(tmp_b));
		else if (skip_prefix(format, "L", &format))
			strbuf_addf(&cmd, "%d", marker_size);
		else if (skip_prefix(format, "P", &format))
			sq_quote_buf(&cmd, path);
		else if (skip_prefix(format, "S", &format))
			sq_quote_buf(&cmd, orig_name ? orig_name : "");
		else if (skip_prefix(format, "X", &format))
			sq_quote_buf(&cmd, name1 ? name1 : "");
		else if (skip_prefix(format, "Y", &format))
			sq_quote_buf(&cmd, name2 ? name2 : "");
		else
			strbuf_addch(&cmd, '%');
	}

	child.use_shell = 1;
	strvec_push(&child.args, cmd.buf);
	status = run_command(&child);

	if (strbuf_read_file(&result_buf, get_tempfile_path(tmp_a), 0) >= 0) {
		result->size = result_buf.len;
		result->ptr = strbuf_detach(&result_buf, NULL);
	}

	delete_tempfile(&tmp_o);
	delete_tempfile(&tmp_a);
	delete_tempfile(&tmp_b);
	strbuf_release(&cmd);
	if (!status)
		ret = LL_MERGE_OK;
	else if (status <= 128)
		ret = LL_MERGE_CONFLICT;
	else
		/* died due to a signal: WTERMSIG(status) + 128 */
		ret = LL_MERGE_ERROR;
	return ret;
}

/*
 * merge.default and merge.driver configuration items
 */
static struct ll_merge_driver *ll_user_merge, **ll_user_merge_tail;
static char *default_ll_merge;

static int read_merge_config(const char *var, const char *value,
			     const struct config_context *ctx UNUSED,
			     void *cb UNUSED)
{
	struct ll_merge_driver *fn;
	const char *key, *name;
	size_t namelen;

	if (!strcmp(var, "merge.default"))
		return git_config_string(&default_ll_merge, var, value);

	/*
	 * We are not interested in anything but "merge.<name>.variable";
	 * especially, we do not want to look at variables such as
	 * "merge.summary", "merge.tool", and "merge.verbosity".
	 */
	if (parse_config_key(var, "merge", &name, &namelen, &key) < 0 || !name)
		return 0;

	/*
	 * Find existing one as we might be processing merge.<name>.var2
	 * after seeing merge.<name>.var1.
	 */
	for (fn = ll_user_merge; fn; fn = fn->next)
		if (!xstrncmpz(fn->name, name, namelen))
			break;
	if (!fn) {
		CALLOC_ARRAY(fn, 1);
		fn->name = xmemdupz(name, namelen);
		fn->fn = ll_ext_merge;
		*ll_user_merge_tail = fn;
		ll_user_merge_tail = &(fn->next);
	}

	if (!strcmp("name", key)) {
		/*
		 * The description is leaking, but that's okay as we want to
		 * keep around the merge drivers anyway.
		 */
		return git_config_string((char **) &fn->description, var, value);
	}

	if (!strcmp("driver", key)) {
		if (!value)
			return config_error_nonbool(var);
		/*
		 * merge.<name>.driver specifies the command line:
		 *
		 *	command-line
		 *
		 * The command-line will be interpolated with the following
		 * tokens and is given to the shell:
		 *
		 *    %O - temporary file name for the merge base.
		 *    %A - temporary file name for our version.
		 *    %B - temporary file name for the other branches' version.
		 *    %L - conflict marker length
		 *    %P - the original path (safely quoted for the shell)
		 *    %S - the revision for the merge base
		 *    %X - the revision for our version
		 *    %Y - the revision for their version
		 *
		 * If the file is not named identically in all versions, then each
		 * revision is joined with the corresponding path, separated by a colon.
		 * The external merge driver should write the results in the
		 * file named by %A, and signal that it has done with zero exit
		 * status.
		 */
		fn->cmdline = xstrdup(value);
		return 0;
	}

	if (!strcmp("recursive", key))
		return git_config_string(&fn->recursive, var, value);

	return 0;
}

static void initialize_ll_merge(void)
{
	if (ll_user_merge_tail)
		return;
	ll_user_merge_tail = &ll_user_merge;
	repo_config(the_repository, read_merge_config, NULL);
}

static const struct ll_merge_driver *find_ll_merge_driver(const char *merge_attr)
{
	struct ll_merge_driver *fn;
	const char *name;
	int i;

	initialize_ll_merge();

	if (ATTR_TRUE(merge_attr))
		return &ll_merge_drv[LL_TEXT_MERGE];
	else if (ATTR_FALSE(merge_attr))
		return &ll_merge_drv[LL_BINARY_MERGE];
	else if (ATTR_UNSET(merge_attr)) {
		if (!default_ll_merge)
			return &ll_merge_drv[LL_TEXT_MERGE];
		else
			name = default_ll_merge;
	}
	else
		name = merge_attr;

	for (fn = ll_user_merge; fn; fn = fn->next)
		if (!strcmp(fn->name, name))
			return fn;

	for (i = 0; i < ARRAY_SIZE(ll_merge_drv); i++)
		if (!strcmp(ll_merge_drv[i].name, name))
			return &ll_merge_drv[i];

	/* default to the 3-way */
	return &ll_merge_drv[LL_TEXT_MERGE];
}

static void normalize_file(mmfile_t *mm, const char *path, struct index_state *istate)
{
	struct strbuf strbuf = STRBUF_INIT;
	if (renormalize_buffer(istate, path, mm->ptr, mm->size, &strbuf)) {
		free(mm->ptr);
		mm->size = strbuf.len;
		mm->ptr = strbuf_detach(&strbuf, NULL);
	}
}

enum ll_merge_result ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     struct index_state *istate,
	     const struct ll_merge_options *opts)
{
	struct attr_check *check = load_merge_attributes();
	static const struct ll_merge_options default_opts = LL_MERGE_OPTIONS_INIT;
	const char *ll_driver_name = NULL;
	int marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	const struct ll_merge_driver *driver;

	if (!opts)
		opts = &default_opts;

	if (opts->renormalize) {
		normalize_file(ancestor, path, istate);
		normalize_file(ours, path, istate);
		normalize_file(theirs, path, istate);
	}

	git_check_attr(istate, path, check);
	ll_driver_name = check->items[0].value;
	if (check->items[1].value) {
		if (strtol_i(check->items[1].value, 10, &marker_size)) {
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
			warning(_("invalid marker-size '%s', expecting an integer"), check->items[1].value);
		}
		if (marker_size <= 0)
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	}
	driver = find_ll_merge_driver(ll_driver_name);

	if (opts->virtual_ancestor) {
		if (driver->recursive)
			driver = find_ll_merge_driver(driver->recursive);
	}
	if (opts->extra_marker_size) {
		marker_size += opts->extra_marker_size;
	}
	return driver->fn(driver, result_buf, path, ancestor, ancestor_label,
			  ours, our_label, theirs, their_label,
			  opts, marker_size);
}

int ll_merge_marker_size(struct index_state *istate, const char *path)
{
	static struct attr_check *check;
	int marker_size = DEFAULT_CONFLICT_MARKER_SIZE;

	if (!check)
		check = attr_check_initl("conflict-marker-size", NULL);
	git_check_attr(istate, path, check);
	if (check->items[0].value) {
		if (strtol_i(check->items[0].value, 10, &marker_size)) {
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
			warning(_("invalid marker-size '%s', expecting an integer"), check->items[0].value);
		}
		if (marker_size <= 0)
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	}
	return marker_size;
}

int is_conflict_marker_line(const char *line, unsigned long len, int marker_size)
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

	for (cnt = 1; cnt < marker_size; cnt++) {
		if (line[cnt] != firstchar)
			return 0;
	}

	if (((firstchar == '<') || (firstchar == '>')) &&
	    line[marker_size] != ' ')
		return 0;

	if (!isspace((unsigned char)line[marker_size]))
		return 0;

	return firstchar;
}

int has_conflict_markers(struct index_state *istate, const char *path)
{
	FILE *f;
	struct strbuf sb = STRBUF_INIT;
	int marker_size = ll_merge_marker_size(istate, path);
	int has_markers = 0;

	f = fopen(path, "r");
	if (!f)
		return 0;

	while (strbuf_getwholeline(&sb, f, '\n') != EOF) {
		if (is_conflict_marker_line(sb.buf, sb.len, marker_size)) {
			has_markers = 1;
			break;
		}
		if (buffer_is_binary(sb.buf,
				     ULONG_MAX <= sb.len ? ULONG_MAX : sb.len))
			break;
	}
	fclose(f);
	strbuf_release(&sb);
	return has_markers;
}
