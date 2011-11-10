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
			   int virtual_ancestor);

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
			   int virtual_ancestor)
{
	/*
	 * The tentative merge result is "ours" for the final round,
	 * or common ancestor for an internal merge.  Still return
	 * "conflicted merge" status.
	 */
	mmfile_t *stolen = virtual_ancestor ? orig : src1;

	result->ptr = stolen->ptr;
	result->size = stolen->size;
	stolen->ptr = NULL;
	return 1;
}

static int ll_xdl_merge(const struct ll_merge_driver *drv_unused,
			mmbuffer_t *result,
			const char *path_unused,
			mmfile_t *orig,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			int virtual_ancestor)
{
	xpparam_t xpp;
	int style = 0;

	if (buffer_is_binary(orig->ptr, orig->size) ||
	    buffer_is_binary(src1->ptr, src1->size) ||
	    buffer_is_binary(src2->ptr, src2->size)) {
		warning("Cannot merge binary files: %s vs. %s\n",
			name1, name2);
		return ll_binary_merge(drv_unused, result,
				       path_unused,
				       orig, src1, name1,
				       src2, name2,
				       virtual_ancestor);
	}

	memset(&xpp, 0, sizeof(xpp));
	if (git_xmerge_style >= 0)
		style = git_xmerge_style;
	return xdl_merge(orig,
			 src1, name1,
			 src2, name2,
			 &xpp, XDL_MERGE_ZEALOUS | style,
			 result);
}

static int ll_union_merge(const struct ll_merge_driver *drv_unused,
			  mmbuffer_t *result,
			  const char *path_unused,
			  mmfile_t *orig,
			  mmfile_t *src1, const char *name1,
			  mmfile_t *src2, const char *name2,
			  int virtual_ancestor)
{
	char *src, *dst;
	long size;
	const int marker_size = 7;
	int status, saved_style;

	/* We have to force the RCS "merge" style */
	saved_style = git_xmerge_style;
	git_xmerge_style = 0;
	status = ll_xdl_merge(drv_unused, result, path_unused,
			      orig, src1, NULL, src2, NULL,
			      virtual_ancestor);
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
		die("unable to write temp-file");
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
			int virtual_ancestor)
{
	char temp[3][50];
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf_expand_dict_entry dict[] = {
		{ "O", temp[0] },
		{ "A", temp[1] },
		{ "B", temp[2] },
		{ NULL }
	};
	struct child_process child;
	const char *args[20];
	int status, fd, i;
	struct stat st;

	if (fn->cmdline == NULL)
		die("custom merge driver %s lacks command line.", fn->name);

	result->ptr = NULL;
	result->size = 0;
	create_temp(orig, temp[0]);
	create_temp(src1, temp[1]);
	create_temp(src2, temp[2]);

	strbuf_expand(&cmd, fn->cmdline, strbuf_expand_dict_cb, &dict);

	memset(&child, 0, sizeof(child));
	child.argv = args;
	args[0] = "sh";
	args[1] = "-c";
	args[2] = cmd.buf;
	args[3] = NULL;

	status = run_command(&child);
	if (status < -ERR_RUN_COMMAND_FORK)
		; /* failure in run-command */
	else
		status = -status;
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
		unlink(temp[i]);
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
			default_ll_merge = strdup(value);
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
		fn->description = strdup(value);
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
		 *
		 * The external merge driver should write the results in the
		 * file named by %A, and signal that it has done with zero exit
		 * status.
		 */
		fn->cmdline = strdup(value);
		return 0;
	}

	if (!strcmp("recursive", ep)) {
		if (!value)
			return error("%s: lacks value", var);
		fn->recursive = strdup(value);
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

static const char *git_path_check_merge(const char *path)
{
	static struct git_attr_check attr_merge_check;

	if (!attr_merge_check.attr)
		attr_merge_check.attr = git_attr("merge", 5);

	if (git_checkattr(path, 1, &attr_merge_check))
		return NULL;
	return attr_merge_check.value;
}

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     int virtual_ancestor)
{
	const char *ll_driver_name;
	const struct ll_merge_driver *driver;

	ll_driver_name = git_path_check_merge(path);
	driver = find_ll_merge_driver(ll_driver_name);

	if (virtual_ancestor && driver->recursive)
		driver = find_ll_merge_driver(driver->recursive);
	return driver->fn(driver, result_buf, path,
			  ancestor,
			  ours, our_label,
			  theirs, their_label, virtual_ancestor);
}
