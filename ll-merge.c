/*
 * Low level 3-way in-core file merge.
 *
 * Copyright (c) 2007 Junio C Hamano
 */

#include "cache.h"
#include "config.h"
#include "attr.h"
#include "xdiff-interface.h"
#include "run-command.h"
#include "ll-merge.h"
#include "quote.h"

struct ll_merge_driver;

typedef int (*ll_merge_fn)(const struct ll_merge_driver *,
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
	const char *recursive;
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
static int ll_binary_merge(const struct ll_merge_driver *drv_unused,
			   mmbuffer_t *result,
			   const char *path,
			   mmfile_t *orig, const char *orig_name,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   const struct ll_merge_options *opts,
			   int marker_size)
{
	mmfile_t *stolen;
	assert(opts);

	/*
	 * The tentative merge result is the common ancestor for an
	 * internal merge.  For the final merge, it is "ours" by
	 * default but -Xours/-Xtheirs can tweak the choice.
	 */
	if (opts->virtual_ancestor) {
		stolen = orig;
	} else {
		switch (opts->variant) {
		default:
			warning("Cannot merge binary files: %s (%s vs. %s)",
				path, name1, name2);
			/* fallthru */
		case XDL_MERGE_FAVOR_OURS:
			stolen = src1;
			break;
		case XDL_MERGE_FAVOR_THEIRS:
			stolen = src2;
			break;
		}
	}

	result->ptr = stolen->ptr;
	result->size = stolen->size;
	stolen->ptr = NULL;

	/*
	 * With -Xtheirs or -Xours, we have cleanly merged;
	 * otherwise we got a conflict.
	 */
	return (opts->variant ? 0 : 1);
}

static int ll_xdl_merge(const struct ll_merge_driver *drv_unused,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig, const char *orig_name,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			const struct ll_merge_options *opts,
			int marker_size)
{
	xmparam_t xmp;
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
	if (git_xmerge_style >= 0)
		xmp.style = git_xmerge_style;
	if (marker_size > 0)
		xmp.marker_size = marker_size;
	xmp.ancestor = orig_name;
	xmp.file1 = name1;
	xmp.file2 = name2;
	return xdl_merge(orig, src1, src2, &xmp, result);
}

static int ll_union_merge(const struct ll_merge_driver *drv_unused,
			  mmbuffer_t *result,
			  const char *path_unused,
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
	return ll_xdl_merge(drv_unused, result, path_unused,
			    orig, NULL, src1, NULL, src2, NULL,
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

static void create_temp(mmfile_t *src, char *path, size_t len)
{
	int fd;

	xsnprintf(path, len, ".merge_file_XXXXXX");
	fd = xmkstemp(path);
	if (write_in_full(fd, src->ptr, src->size) < 0)
		die_errno("unable to write temp-file");
	close(fd);
}

/*
 * User defined low-level merge driver support.
 */
static int ll_ext_merge(const struct ll_merge_driver *fn,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig, const char *orig_name,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			const struct ll_merge_options *opts,
			int marker_size)
{
	char temp[4][50];
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf_expand_dict_entry dict[6];
	struct strbuf path_sq = STRBUF_INIT;
	const char *args[] = { NULL, NULL };
	int status, fd, i;
	struct stat st;
	assert(opts);

	sq_quote_buf(&path_sq, path);
	dict[0].placeholder = "O"; dict[0].value = temp[0];
	dict[1].placeholder = "A"; dict[1].value = temp[1];
	dict[2].placeholder = "B"; dict[2].value = temp[2];
	dict[3].placeholder = "L"; dict[3].value = temp[3];
	dict[4].placeholder = "P"; dict[4].value = path_sq.buf;
	dict[5].placeholder = NULL; dict[5].value = NULL;

	if (fn->cmdline == NULL)
		die("custom merge driver %s lacks command line.", fn->name);

	result->ptr = NULL;
	result->size = 0;
	create_temp(orig, temp[0], sizeof(temp[0]));
	create_temp(src1, temp[1], sizeof(temp[1]));
	create_temp(src2, temp[2], sizeof(temp[2]));
	xsnprintf(temp[3], sizeof(temp[3]), "%d", marker_size);

	strbuf_expand(&cmd, fn->cmdline, strbuf_expand_dict_cb, &dict);

	args[0] = cmd.buf;
	status = run_command_v_opt(args, RUN_USING_SHELL);
	fd = open(temp[1], O_RDONLY);
	if (fd < 0)
		goto bad;
	if (fstat(fd, &st))
		goto close_bad;
	result->size = st.st_size;
	result->ptr = xmallocz(result->size);
	if (read_in_full(fd, result->ptr, result->size) != result->size) {
		FREE_AND_NULL(result->ptr);
		result->size = 0;
	}
 close_bad:
	close(fd);
 bad:
	for (i = 0; i < 3; i++)
		unlink_or_warn(temp[i]);
	strbuf_release(&cmd);
	strbuf_release(&path_sq);
	return status;
}

/*
 * merge.default and merge.driver configuration items
 */
static struct ll_merge_driver *ll_user_merge, **ll_user_merge_tail;
static const char *default_ll_merge;

static int read_merge_config(const char *var, const char *value, void *cb)
{
	struct ll_merge_driver *fn;
	const char *key, *name;
	int namelen;

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
		if (!strncmp(fn->name, name, namelen) && !fn->name[namelen])
			break;
	if (!fn) {
		fn = xcalloc(1, sizeof(struct ll_merge_driver));
		fn->name = xmemdupz(name, namelen);
		fn->fn = ll_ext_merge;
		*ll_user_merge_tail = fn;
		ll_user_merge_tail = &(fn->next);
	}

	if (!strcmp("name", key))
		return git_config_string(&fn->description, var, value);

	if (!strcmp("driver", key)) {
		if (!value)
			return error("%s: lacks value", var);
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
		 *
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
	git_config(read_merge_config, NULL);
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

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     struct index_state *istate,
	     const struct ll_merge_options *opts)
{
	struct attr_check *check = load_merge_attributes();
	static const struct ll_merge_options default_opts;
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
		marker_size = atoi(check->items[1].value);
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
		marker_size = atoi(check->items[0].value);
		if (marker_size <= 0)
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	}
	return marker_size;
}
