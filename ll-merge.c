/*
 * Low level 3-way in-core file merge.
 *
 * Copyright (c) 2007 Junio C Hamano
 */

#include "cache.h"
#include "attr.h"
#include "xdiff-interface.h"
#include "run-command.h"
#include "ll-merge.h"

struct ll_merge_driver;

typedef int (*ll_merge_fn)(const struct ll_merge_driver *,
			   mmbuffer_t *result,
			   const char *path,
			   mmfile_t *orig,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   int flag,
			   int marker_size);

struct ll_merge_driver {
	const char *name;
	const char *description;
	ll_merge_fn fn;
	const char *recursive;
	struct ll_merge_driver *next;
	char *cmdline;
};

/*
 * Built-in low-levels
 */
static int ll_binary_merge(const struct ll_merge_driver *drv_unused,
			   mmbuffer_t *result,
			   const char *path_unused,
			   mmfile_t *orig,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   int flag, int marker_size)
{
	/*
	 * The tentative merge result is "ours" for the final round,
	 * or common ancestor for an internal merge.  Still return
	 * "conflicted merge" status.
	 */
	mmfile_t *stolen = (flag & 01) ? orig : src1;

	result->ptr = stolen->ptr;
	result->size = stolen->size;
	stolen->ptr = NULL;
	return 1;
}

static int ll_xdl_merge(const struct ll_merge_driver *drv_unused,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			int flag, int marker_size)
{
	xmparam_t xmp;
	int style = 0;
	int favor = (flag >> 1) & 03;

	if (buffer_is_binary(orig->ptr, orig->size) ||
	    buffer_is_binary(src1->ptr, src1->size) ||
	    buffer_is_binary(src2->ptr, src2->size)) {
		warning("Cannot merge binary files: %s (%s vs. %s)\n",
			path, name1, name2);
		return ll_binary_merge(drv_unused, result,
				       path,
				       orig, src1, name1,
				       src2, name2,
				       flag, marker_size);
	}

	memset(&xmp, 0, sizeof(xmp));
	if (git_xmerge_style >= 0)
		style = git_xmerge_style;
	if (marker_size > 0)
		xmp.marker_size = marker_size;
	return xdl_merge(orig,
			 src1, name1,
			 src2, name2,
			 &xmp, XDL_MERGE_FLAGS(XDL_MERGE_ZEALOUS, style, favor),
			 result);
}

static int ll_union_merge(const struct ll_merge_driver *drv_unused,
			  mmbuffer_t *result,
			  const char *path_unused,
			  mmfile_t *orig,
			  mmfile_t *src1, const char *name1,
			  mmfile_t *src2, const char *name2,
			  int flag, int marker_size)
{
	char *src, *dst;
	long size;
	int status, saved_style;

	/* We have to force the RCS "merge" style */
	saved_style = git_xmerge_style;
	git_xmerge_style = 0;
	status = ll_xdl_merge(drv_unused, result, path_unused,
			      orig, src1, NULL, src2, NULL,
			      flag, marker_size);
	git_xmerge_style = saved_style;
	if (status <= 0)
		return status;
	size = result->size;
	src = dst = result->ptr;
	while (size) {
		char ch;
		if ((marker_size < size) &&
		    (*src == '<' || *src == '=' || *src == '>')) {
			int i;
			ch = *src;
			for (i = 0; i < marker_size; i++)
				if (src[i] != ch)
					goto not_a_marker;
			if (src[marker_size] != '\n')
				goto not_a_marker;
			src += marker_size + 1;
			size -= marker_size + 1;
			continue;
		}
	not_a_marker:
		do {
			ch = *src++;
			*dst++ = ch;
			size--;
		} while (ch != '\n' && size);
	}
	result->size = dst - result->ptr;
	return 0;
}

#define LL_BINARY_MERGE 0
#define LL_TEXT_MERGE 1
#define LL_UNION_MERGE 2
static struct ll_merge_driver ll_merge_drv[] = {
	{ "binary", "built-in binary merge", ll_binary_merge },
	{ "text", "built-in 3-way text merge", ll_xdl_merge },
	{ "union", "built-in union merge", ll_union_merge },
};

static void create_temp(mmfile_t *src, char *path)
{
	int fd;

	strcpy(path, ".merge_file_XXXXXX");
	fd = xmkstemp(path);
	if (write_in_full(fd, src->ptr, src->size) != src->size)
		die_errno("unable to write temp-file");
	close(fd);
}

/*
 * User defined low-level merge driver support.
 */
static int ll_ext_merge(const struct ll_merge_driver *fn,
			mmbuffer_t *result,
			const char *path,
			mmfile_t *orig,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			int flag, int marker_size)
{
	char temp[4][50];
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf_expand_dict_entry dict[] = {
		{ "O", temp[0] },
		{ "A", temp[1] },
		{ "B", temp[2] },
		{ "L", temp[3] },
		{ NULL }
	};
	const char *args[] = { NULL, NULL };
	int status, fd, i;
	struct stat st;

	if (fn->cmdline == NULL)
		die("custom merge driver %s lacks command line.", fn->name);

	result->ptr = NULL;
	result->size = 0;
	create_temp(orig, temp[0]);
	create_temp(src1, temp[1]);
	create_temp(src2, temp[2]);
	sprintf(temp[3], "%d", marker_size);

	strbuf_expand(&cmd, fn->cmdline, strbuf_expand_dict_cb, &dict);

	args[0] = cmd.buf;
	status = run_command_v_opt(args, RUN_USING_SHELL);
	fd = open(temp[1], O_RDONLY);
	if (fd < 0)
		goto bad;
	if (fstat(fd, &st))
		goto close_bad;
	result->size = st.st_size;
	result->ptr = xmalloc(result->size + 1);
	if (read_in_full(fd, result->ptr, result->size) != result->size) {
		free(result->ptr);
		result->ptr = NULL;
		result->size = 0;
	}
 close_bad:
	close(fd);
 bad:
	for (i = 0; i < 3; i++)
		unlink_or_warn(temp[i]);
	strbuf_release(&cmd);
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
	const char *ep, *name;
	int namelen;

	if (!strcmp(var, "merge.default")) {
		if (value)
			default_ll_merge = xstrdup(value);
		return 0;
	}

	/*
	 * We are not interested in anything but "merge.<name>.variable";
	 * especially, we do not want to look at variables such as
	 * "merge.summary", "merge.tool", and "merge.verbosity".
	 */
	if (prefixcmp(var, "merge.") || (ep = strrchr(var, '.')) == var + 5)
		return 0;

	/*
	 * Find existing one as we might be processing merge.<name>.var2
	 * after seeing merge.<name>.var1.
	 */
	name = var + 6;
	namelen = ep - name;
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

	ep++;

	if (!strcmp("name", ep)) {
		if (!value)
			return error("%s: lacks value", var);
		fn->description = xstrdup(value);
		return 0;
	}

	if (!strcmp("driver", ep)) {
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
		 *
		 * The external merge driver should write the results in the
		 * file named by %A, and signal that it has done with zero exit
		 * status.
		 */
		fn->cmdline = xstrdup(value);
		return 0;
	}

	if (!strcmp("recursive", ep)) {
		if (!value)
			return error("%s: lacks value", var);
		fn->recursive = xstrdup(value);
		return 0;
	}

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

static int git_path_check_merge(const char *path, struct git_attr_check check[2])
{
	if (!check[0].attr) {
		check[0].attr = git_attr("merge");
		check[1].attr = git_attr("conflict-marker-size");
	}
	return git_checkattr(path, 2, check);
}

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     int flag)
{
	static struct git_attr_check check[2];
	const char *ll_driver_name = NULL;
	int marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	const struct ll_merge_driver *driver;
	int virtual_ancestor = flag & 01;

	if (!git_path_check_merge(path, check)) {
		ll_driver_name = check[0].value;
		if (check[1].value) {
			marker_size = atoi(check[1].value);
			if (marker_size <= 0)
				marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
		}
	}
	driver = find_ll_merge_driver(ll_driver_name);
	if (virtual_ancestor && driver->recursive)
		driver = find_ll_merge_driver(driver->recursive);
	return driver->fn(driver, result_buf, path, ancestor,
			  ours, our_label, theirs, their_label,
			  flag, marker_size);
}

int ll_merge_marker_size(const char *path)
{
	static struct git_attr_check check;
	int marker_size = DEFAULT_CONFLICT_MARKER_SIZE;

	if (!check.attr)
		check.attr = git_attr("conflict-marker-size");
	if (!git_checkattr(path, 1, &check) && check.value) {
		marker_size = atoi(check.value);
		if (marker_size <= 0)
			marker_size = DEFAULT_CONFLICT_MARKER_SIZE;
	}
	return marker_size;
}
