/*
 * Recursive Merge algorithm stolen from git-merge-recursive.py by
 * Fredrik Kuivinen.
 * The thieves were Alex Riesen and Johannes Schindelin, in June/July 2006
 */
#include "cache.h"
#include "cache-tree.h"
#include "commit.h"
#include "blob.h"
#include "tree-walk.h"
#include "diff.h"
#include "diffcore.h"
#include "run-command.h"
#include "tag.h"
#include "unpack-trees.h"
#include "path-list.h"
#include "xdiff-interface.h"
#include "interpolate.h"
#include "attr.h"

static int subtree_merge;

static struct tree *shift_tree_object(struct tree *one, struct tree *two)
{
	unsigned char shifted[20];

	/*
	 * NEEDSWORK: this limits the recursion depth to hardcoded
	 * value '2' to avoid excessive overhead.
	 */
	shift_tree(one->object.sha1, two->object.sha1, shifted, 2);
	if (!hashcmp(two->object.sha1, shifted))
		return two;
	return lookup_tree(shifted);
}

/*
 * A virtual commit has
 * - (const char *)commit->util set to the name, and
 * - *(int *)commit->object.sha1 set to the virtual id.
 */

static unsigned commit_list_count(const struct commit_list *l)
{
	unsigned c = 0;
	for (; l; l = l->next )
		c++;
	return c;
}

static struct commit *make_virtual_commit(struct tree *tree, const char *comment)
{
	struct commit *commit = xcalloc(1, sizeof(struct commit));
	static unsigned virtual_id = 1;
	commit->tree = tree;
	commit->util = (void*)comment;
	*(int*)commit->object.sha1 = virtual_id++;
	/* avoid warnings */
	commit->object.parsed = 1;
	return commit;
}

/*
 * Since we use get_tree_entry(), which does not put the read object into
 * the object pool, we cannot rely on a == b.
 */
static int sha_eq(const unsigned char *a, const unsigned char *b)
{
	if (!a && !b)
		return 2;
	return a && b && hashcmp(a, b) == 0;
}

/*
 * Since we want to write the index eventually, we cannot reuse the index
 * for these (temporary) data.
 */
struct stage_data
{
	struct
	{
		unsigned mode;
		unsigned char sha[20];
	} stages[4];
	unsigned processed:1;
};

struct output_buffer
{
	struct output_buffer *next;
	char *str;
};

static struct path_list current_file_set = {NULL, 0, 0, 1};
static struct path_list current_directory_set = {NULL, 0, 0, 1};

static int call_depth = 0;
static int verbosity = 2;
static int buffer_output = 1;
static struct output_buffer *output_list, *output_end;

static int show (int v)
{
	return (!call_depth && verbosity >= v) || verbosity >= 5;
}

static void output(int v, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (buffer_output && show(v)) {
		struct output_buffer *b = xmalloc(sizeof(*b));
		nfvasprintf(&b->str, fmt, args);
		b->next = NULL;
		if (output_end)
			output_end->next = b;
		else
			output_list = b;
		output_end = b;
	} else if (show(v)) {
		int i;
		for (i = call_depth; i--;)
			fputs("  ", stdout);
		vfprintf(stdout, fmt, args);
		fputc('\n', stdout);
	}
	va_end(args);
}

static void flush_output()
{
	struct output_buffer *b, *n;
	for (b = output_list; b; b = n) {
		int i;
		for (i = call_depth; i--;)
			fputs("  ", stdout);
		fputs(b->str, stdout);
		fputc('\n', stdout);
		n = b->next;
		free(b->str);
		free(b);
	}
	output_list = NULL;
	output_end = NULL;
}

static void output_commit_title(struct commit *commit)
{
	int i;
	flush_output();
	for (i = call_depth; i--;)
		fputs("  ", stdout);
	if (commit->util)
		printf("virtual %s\n", (char *)commit->util);
	else {
		printf("%s ", find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV));
		if (parse_commit(commit) != 0)
			printf("(bad commit)\n");
		else {
			const char *s;
			int len;
			for (s = commit->buffer; *s; s++)
				if (*s == '\n' && s[1] == '\n') {
					s += 2;
					break;
				}
			for (len = 0; s[len] && '\n' != s[len]; len++)
				; /* do nothing */
			printf("%.*s\n", len, s);
		}
	}
}

static struct cache_entry *make_cache_entry(unsigned int mode,
		const unsigned char *sha1, const char *path, int stage, int refresh)
{
	int size, len;
	struct cache_entry *ce;

	if (!verify_path(path))
		return NULL;

	len = strlen(path);
	size = cache_entry_size(len);
	ce = xcalloc(1, size);

	hashcpy(ce->sha1, sha1);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(len, stage);
	ce->ce_mode = create_ce_mode(mode);

	if (refresh)
		return refresh_cache_entry(ce, 0);

	return ce;
}

static int add_cacheinfo(unsigned int mode, const unsigned char *sha1,
		const char *path, int stage, int refresh, int options)
{
	struct cache_entry *ce;
	ce = make_cache_entry(mode, sha1 ? sha1 : null_sha1, path, stage, refresh);
	if (!ce)
		return error("addinfo_cache failed for path '%s'", path);
	return add_cache_entry(ce, options);
}

/*
 * This is a global variable which is used in a number of places but
 * only written to in the 'merge' function.
 *
 * index_only == 1    => Don't leave any non-stage 0 entries in the cache and
 *                       don't update the working directory.
 *               0    => Leave unmerged entries in the cache and update
 *                       the working directory.
 */
static int index_only = 0;

static int git_merge_trees(int index_only,
			   struct tree *common,
			   struct tree *head,
			   struct tree *merge)
{
	int rc;
	struct object_list *trees = NULL;
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	if (index_only)
		opts.index_only = 1;
	else
		opts.update = 1;
	opts.merge = 1;
	opts.head_idx = 2;
	opts.fn = threeway_merge;

	object_list_append(&common->object, &trees);
	object_list_append(&head->object, &trees);
	object_list_append(&merge->object, &trees);

	rc = unpack_trees(trees, &opts);
	cache_tree_free(&active_cache_tree);
	return rc;
}

static int unmerged_index(void)
{
	int i;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce))
			return 1;
	}
	return 0;
}

static struct tree *git_write_tree(void)
{
	struct tree *result = NULL;

	if (unmerged_index()) {
		int i;
		output(0, "There are unmerged index entries:");
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (ce_stage(ce))
				output(0, "%d %.*s", ce_stage(ce), ce_namelen(ce), ce->name);
		}
		return NULL;
	}

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	if (!cache_tree_fully_valid(active_cache_tree) &&
	    cache_tree_update(active_cache_tree,
			      active_cache, active_nr, 0, 0) < 0)
		die("error building trees");

	result = lookup_tree(active_cache_tree->sha1);

	return result;
}

static int save_files_dirs(const unsigned char *sha1,
		const char *base, int baselen, const char *path,
		unsigned int mode, int stage)
{
	int len = strlen(path);
	char *newpath = xmalloc(baselen + len + 1);
	memcpy(newpath, base, baselen);
	memcpy(newpath + baselen, path, len);
	newpath[baselen + len] = '\0';

	if (S_ISDIR(mode))
		path_list_insert(newpath, &current_directory_set);
	else
		path_list_insert(newpath, &current_file_set);
	free(newpath);

	return READ_TREE_RECURSIVE;
}

static int get_files_dirs(struct tree *tree)
{
	int n;
	if (read_tree_recursive(tree, "", 0, 0, NULL, save_files_dirs) != 0)
		return 0;
	n = current_file_set.nr + current_directory_set.nr;
	return n;
}

/*
 * Returns a index_entry instance which doesn't have to correspond to
 * a real cache entry in Git's index.
 */
static struct stage_data *insert_stage_data(const char *path,
		struct tree *o, struct tree *a, struct tree *b,
		struct path_list *entries)
{
	struct path_list_item *item;
	struct stage_data *e = xcalloc(1, sizeof(struct stage_data));
	get_tree_entry(o->object.sha1, path,
			e->stages[1].sha, &e->stages[1].mode);
	get_tree_entry(a->object.sha1, path,
			e->stages[2].sha, &e->stages[2].mode);
	get_tree_entry(b->object.sha1, path,
			e->stages[3].sha, &e->stages[3].mode);
	item = path_list_insert(path, entries);
	item->util = e;
	return e;
}

/*
 * Create a dictionary mapping file names to stage_data objects. The
 * dictionary contains one entry for every path with a non-zero stage entry.
 */
static struct path_list *get_unmerged(void)
{
	struct path_list *unmerged = xcalloc(1, sizeof(struct path_list));
	int i;

	unmerged->strdup_paths = 1;

	for (i = 0; i < active_nr; i++) {
		struct path_list_item *item;
		struct stage_data *e;
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;

		item = path_list_lookup(ce->name, unmerged);
		if (!item) {
			item = path_list_insert(ce->name, unmerged);
			item->util = xcalloc(1, sizeof(struct stage_data));
		}
		e = item->util;
		e->stages[ce_stage(ce)].mode = ntohl(ce->ce_mode);
		hashcpy(e->stages[ce_stage(ce)].sha, ce->sha1);
	}

	return unmerged;
}

struct rename
{
	struct diff_filepair *pair;
	struct stage_data *src_entry;
	struct stage_data *dst_entry;
	unsigned processed:1;
};

/*
 * Get information of all renames which occurred between 'o_tree' and
 * 'tree'. We need the three trees in the merge ('o_tree', 'a_tree' and
 * 'b_tree') to be able to associate the correct cache entries with
 * the rename information. 'tree' is always equal to either a_tree or b_tree.
 */
static struct path_list *get_renames(struct tree *tree,
					struct tree *o_tree,
					struct tree *a_tree,
					struct tree *b_tree,
					struct path_list *entries)
{
	int i;
	struct path_list *renames;
	struct diff_options opts;

	renames = xcalloc(1, sizeof(struct path_list));
	diff_setup(&opts);
	opts.recursive = 1;
	opts.detect_rename = DIFF_DETECT_RENAME;
	opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	if (diff_setup_done(&opts) < 0)
		die("diff setup failed");
	diff_tree_sha1(o_tree->object.sha1, tree->object.sha1, "", &opts);
	diffcore_std(&opts);
	for (i = 0; i < diff_queued_diff.nr; ++i) {
		struct path_list_item *item;
		struct rename *re;
		struct diff_filepair *pair = diff_queued_diff.queue[i];
		if (pair->status != 'R') {
			diff_free_filepair(pair);
			continue;
		}
		re = xmalloc(sizeof(*re));
		re->processed = 0;
		re->pair = pair;
		item = path_list_lookup(re->pair->one->path, entries);
		if (!item)
			re->src_entry = insert_stage_data(re->pair->one->path,
					o_tree, a_tree, b_tree, entries);
		else
			re->src_entry = item->util;

		item = path_list_lookup(re->pair->two->path, entries);
		if (!item)
			re->dst_entry = insert_stage_data(re->pair->two->path,
					o_tree, a_tree, b_tree, entries);
		else
			re->dst_entry = item->util;
		item = path_list_insert(pair->one->path, renames);
		item->util = re;
	}
	opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_queued_diff.nr = 0;
	diff_flush(&opts);
	return renames;
}

static int update_stages(const char *path, struct diff_filespec *o,
			 struct diff_filespec *a, struct diff_filespec *b,
			 int clear)
{
	int options = ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE;
	if (clear)
		if (remove_file_from_cache(path))
			return -1;
	if (o)
		if (add_cacheinfo(o->mode, o->sha1, path, 1, 0, options))
			return -1;
	if (a)
		if (add_cacheinfo(a->mode, a->sha1, path, 2, 0, options))
			return -1;
	if (b)
		if (add_cacheinfo(b->mode, b->sha1, path, 3, 0, options))
			return -1;
	return 0;
}

static int remove_path(const char *name)
{
	int ret, len;
	char *slash, *dirs;

	ret = unlink(name);
	if (ret)
		return ret;
	len = strlen(name);
	dirs = xmalloc(len+1);
	memcpy(dirs, name, len);
	dirs[len] = '\0';
	while ((slash = strrchr(name, '/'))) {
		*slash = '\0';
		len = slash - name;
		if (rmdir(name) != 0)
			break;
	}
	free(dirs);
	return ret;
}

static int remove_file(int clean, const char *path, int no_wd)
{
	int update_cache = index_only || clean;
	int update_working_directory = !index_only && !no_wd;

	if (update_cache) {
		if (remove_file_from_cache(path))
			return -1;
	}
	if (update_working_directory) {
		unlink(path);
		if (errno != ENOENT || errno != EISDIR)
			return -1;
		remove_path(path);
	}
	return 0;
}

static char *unique_path(const char *path, const char *branch)
{
	char *newpath = xmalloc(strlen(path) + 1 + strlen(branch) + 8 + 1);
	int suffix = 0;
	struct stat st;
	char *p = newpath + strlen(path);
	strcpy(newpath, path);
	*(p++) = '~';
	strcpy(p, branch);
	for (; *p; ++p)
		if ('/' == *p)
			*p = '_';
	while (path_list_has_path(&current_file_set, newpath) ||
	       path_list_has_path(&current_directory_set, newpath) ||
	       lstat(newpath, &st) == 0)
		sprintf(p, "_%d", suffix++);

	path_list_insert(newpath, &current_file_set);
	return newpath;
}

static int mkdir_p(const char *path, unsigned long mode)
{
	/* path points to cache entries, so xstrdup before messing with it */
	char *buf = xstrdup(path);
	int result = safe_create_leading_directories(buf);
	free(buf);
	return result;
}

static void flush_buffer(int fd, const char *buf, unsigned long size)
{
	while (size > 0) {
		long ret = write_in_full(fd, buf, size);
		if (ret < 0) {
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die("merge-recursive: %s", strerror(errno));
		} else if (!ret) {
			die("merge-recursive: disk full?");
		}
		size -= ret;
		buf += ret;
	}
}

static int make_room_for_path(const char *path)
{
	int status;
	const char *msg = "failed to create path '%s'%s";

	status = mkdir_p(path, 0777);
	if (status) {
		if (status == -3) {
			/* something else exists */
			error(msg, path, ": perhaps a D/F conflict?");
			return -1;
		}
		die(msg, path, "");
	}

	/* Successful unlink is good.. */
	if (!unlink(path))
		return 0;
	/* .. and so is no existing file */
	if (errno == ENOENT)
		return 0;
	/* .. but not some other error (who really cares what?) */
	return error(msg, path, ": perhaps a D/F conflict?");
}

static void update_file_flags(const unsigned char *sha,
			      unsigned mode,
			      const char *path,
			      int update_cache,
			      int update_wd)
{
	if (index_only)
		update_wd = 0;

	if (update_wd) {
		enum object_type type;
		void *buf;
		unsigned long size;

		buf = read_sha1_file(sha, &type, &size);
		if (!buf)
			die("cannot read object %s '%s'", sha1_to_hex(sha), path);
		if (type != OBJ_BLOB)
			die("blob expected for %s '%s'", sha1_to_hex(sha), path);

		if (make_room_for_path(path) < 0) {
			update_wd = 0;
			goto update_index;
		}
		if (S_ISREG(mode) || (!has_symlinks && S_ISLNK(mode))) {
			int fd;
			if (mode & 0100)
				mode = 0777;
			else
				mode = 0666;
			fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, mode);
			if (fd < 0)
				die("failed to open %s: %s", path, strerror(errno));
			flush_buffer(fd, buf, size);
			close(fd);
		} else if (S_ISLNK(mode)) {
			char *lnk = xmalloc(size + 1);
			memcpy(lnk, buf, size);
			lnk[size] = '\0';
			mkdir_p(path, 0777);
			unlink(path);
			symlink(lnk, path);
			free(lnk);
		} else
			die("do not know what to do with %06o %s '%s'",
			    mode, sha1_to_hex(sha), path);
	}
 update_index:
	if (update_cache)
		add_cacheinfo(mode, sha, path, 0, update_wd, ADD_CACHE_OK_TO_ADD);
}

static void update_file(int clean,
			const unsigned char *sha,
			unsigned mode,
			const char *path)
{
	update_file_flags(sha, mode, path, index_only || clean, !index_only);
}

/* Low level file merging, update and removal */

struct merge_file_info
{
	unsigned char sha[20];
	unsigned mode;
	unsigned clean:1,
		 merge:1;
};

static void fill_mm(const unsigned char *sha1, mmfile_t *mm)
{
	unsigned long size;
	enum object_type type;

	if (!hashcmp(sha1, null_sha1)) {
		mm->ptr = xstrdup("");
		mm->size = 0;
		return;
	}

	mm->ptr = read_sha1_file(sha1, &type, &size);
	if (!mm->ptr || type != OBJ_BLOB)
		die("unable to read blob object %s", sha1_to_hex(sha1));
	mm->size = size;
}

/*
 * Customizable low-level merge drivers support.
 */

struct ll_merge_driver;
typedef int (*ll_merge_fn)(const struct ll_merge_driver *,
			   const char *path,
			   mmfile_t *orig,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   mmbuffer_t *result);

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
static int ll_xdl_merge(const struct ll_merge_driver *drv_unused,
			const char *path_unused,
			mmfile_t *orig,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			mmbuffer_t *result)
{
	xpparam_t xpp;

	memset(&xpp, 0, sizeof(xpp));
	return xdl_merge(orig,
			 src1, name1,
			 src2, name2,
			 &xpp, XDL_MERGE_ZEALOUS,
			 result);
}

static int ll_union_merge(const struct ll_merge_driver *drv_unused,
			  const char *path_unused,
			  mmfile_t *orig,
			  mmfile_t *src1, const char *name1,
			  mmfile_t *src2, const char *name2,
			  mmbuffer_t *result)
{
	char *src, *dst;
	long size;
	const int marker_size = 7;

	int status = ll_xdl_merge(drv_unused, path_unused,
				  orig, src1, NULL, src2, NULL, result);
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

static int ll_binary_merge(const struct ll_merge_driver *drv_unused,
			   const char *path_unused,
			   mmfile_t *orig,
			   mmfile_t *src1, const char *name1,
			   mmfile_t *src2, const char *name2,
			   mmbuffer_t *result)
{
	/*
	 * The tentative merge result is "ours" for the final round,
	 * or common ancestor for an internal merge.  Still return
	 * "conflicted merge" status.
	 */
	mmfile_t *stolen = index_only ? orig : src1;

	result->ptr = stolen->ptr;
	result->size = stolen->size;
	stolen->ptr = NULL;
	return 1;
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
	fd = mkstemp(path);
	if (fd < 0)
		die("unable to create temp-file");
	if (write_in_full(fd, src->ptr, src->size) != src->size)
		die("unable to write temp-file");
	close(fd);
}

/*
 * User defined low-level merge driver support.
 */
static int ll_ext_merge(const struct ll_merge_driver *fn,
			const char *path,
			mmfile_t *orig,
			mmfile_t *src1, const char *name1,
			mmfile_t *src2, const char *name2,
			mmbuffer_t *result)
{
	char temp[3][50];
	char cmdbuf[2048];
	struct interp table[] = {
		{ "%O" },
		{ "%A" },
		{ "%B" },
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

	interp_set_entry(table, 0, temp[0]);
	interp_set_entry(table, 1, temp[1]);
	interp_set_entry(table, 2, temp[2]);

	output(1, "merging %s using %s", path,
	       fn->description ? fn->description : fn->name);

	interpolate(cmdbuf, sizeof(cmdbuf), fn->cmdline, table, 3);

	memset(&child, 0, sizeof(child));
	child.argv = args;
	args[0] = "sh";
	args[1] = "-c";
	args[2] = cmdbuf;
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
	return status;
}

/*
 * merge.default and merge.driver configuration items
 */
static struct ll_merge_driver *ll_user_merge, **ll_user_merge_tail;
static const char *default_ll_merge;

static int read_merge_config(const char *var, const char *value)
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
		char *namebuf;
		fn = xcalloc(1, sizeof(struct ll_merge_driver));
		namebuf = xmalloc(namelen + 1);
		memcpy(namebuf, name, namelen);
		namebuf[namelen] = 0;
		fn->name = namebuf;
		fn->fn = ll_ext_merge;
		fn->next = NULL;
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
	git_config(read_merge_config);
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

static int ll_merge(mmbuffer_t *result_buf,
		    struct diff_filespec *o,
		    struct diff_filespec *a,
		    struct diff_filespec *b,
		    const char *branch1,
		    const char *branch2)
{
	mmfile_t orig, src1, src2;
	char *name1, *name2;
	int merge_status;
	const char *ll_driver_name;
	const struct ll_merge_driver *driver;

	name1 = xstrdup(mkpath("%s:%s", branch1, a->path));
	name2 = xstrdup(mkpath("%s:%s", branch2, b->path));

	fill_mm(o->sha1, &orig);
	fill_mm(a->sha1, &src1);
	fill_mm(b->sha1, &src2);

	ll_driver_name = git_path_check_merge(a->path);
	driver = find_ll_merge_driver(ll_driver_name);

	if (index_only && driver->recursive)
		driver = find_ll_merge_driver(driver->recursive);
	merge_status = driver->fn(driver, a->path,
				  &orig, &src1, name1, &src2, name2,
				  result_buf);

	free(name1);
	free(name2);
	free(orig.ptr);
	free(src1.ptr);
	free(src2.ptr);
	return merge_status;
}

static struct merge_file_info merge_file(struct diff_filespec *o,
		struct diff_filespec *a, struct diff_filespec *b,
		const char *branch1, const char *branch2)
{
	struct merge_file_info result;
	result.merge = 0;
	result.clean = 1;

	if ((S_IFMT & a->mode) != (S_IFMT & b->mode)) {
		result.clean = 0;
		if (S_ISREG(a->mode)) {
			result.mode = a->mode;
			hashcpy(result.sha, a->sha1);
		} else {
			result.mode = b->mode;
			hashcpy(result.sha, b->sha1);
		}
	} else {
		if (!sha_eq(a->sha1, o->sha1) && !sha_eq(b->sha1, o->sha1))
			result.merge = 1;

		result.mode = a->mode == o->mode ? b->mode: a->mode;

		if (sha_eq(a->sha1, o->sha1))
			hashcpy(result.sha, b->sha1);
		else if (sha_eq(b->sha1, o->sha1))
			hashcpy(result.sha, a->sha1);
		else if (S_ISREG(a->mode)) {
			mmbuffer_t result_buf;
			int merge_status;

			merge_status = ll_merge(&result_buf, o, a, b,
						branch1, branch2);

			if ((merge_status < 0) || !result_buf.ptr)
				die("Failed to execute internal merge");

			if (write_sha1_file(result_buf.ptr, result_buf.size,
					    blob_type, result.sha))
				die("Unable to add %s to database",
				    a->path);

			free(result_buf.ptr);
			result.clean = (merge_status == 0);
		} else {
			if (!(S_ISLNK(a->mode) || S_ISLNK(b->mode)))
				die("cannot merge modes?");

			hashcpy(result.sha, a->sha1);

			if (!sha_eq(a->sha1, b->sha1))
				result.clean = 0;
		}
	}

	return result;
}

static void conflict_rename_rename(struct rename *ren1,
				   const char *branch1,
				   struct rename *ren2,
				   const char *branch2)
{
	char *del[2];
	int delp = 0;
	const char *ren1_dst = ren1->pair->two->path;
	const char *ren2_dst = ren2->pair->two->path;
	const char *dst_name1 = ren1_dst;
	const char *dst_name2 = ren2_dst;
	if (path_list_has_path(&current_directory_set, ren1_dst)) {
		dst_name1 = del[delp++] = unique_path(ren1_dst, branch1);
		output(1, "%s is a directory in %s added as %s instead",
		       ren1_dst, branch2, dst_name1);
		remove_file(0, ren1_dst, 0);
	}
	if (path_list_has_path(&current_directory_set, ren2_dst)) {
		dst_name2 = del[delp++] = unique_path(ren2_dst, branch2);
		output(1, "%s is a directory in %s added as %s instead",
		       ren2_dst, branch1, dst_name2);
		remove_file(0, ren2_dst, 0);
	}
	if (index_only) {
		remove_file_from_cache(dst_name1);
		remove_file_from_cache(dst_name2);
		/*
		 * Uncomment to leave the conflicting names in the resulting tree
		 *
		 * update_file(0, ren1->pair->two->sha1, ren1->pair->two->mode, dst_name1);
		 * update_file(0, ren2->pair->two->sha1, ren2->pair->two->mode, dst_name2);
		 */
	} else {
		update_stages(dst_name1, NULL, ren1->pair->two, NULL, 1);
		update_stages(dst_name2, NULL, NULL, ren2->pair->two, 1);
	}
	while (delp--)
		free(del[delp]);
}

static void conflict_rename_dir(struct rename *ren1,
				const char *branch1)
{
	char *new_path = unique_path(ren1->pair->two->path, branch1);
	output(1, "Renamed %s to %s instead", ren1->pair->one->path, new_path);
	remove_file(0, ren1->pair->two->path, 0);
	update_file(0, ren1->pair->two->sha1, ren1->pair->two->mode, new_path);
	free(new_path);
}

static void conflict_rename_rename_2(struct rename *ren1,
				     const char *branch1,
				     struct rename *ren2,
				     const char *branch2)
{
	char *new_path1 = unique_path(ren1->pair->two->path, branch1);
	char *new_path2 = unique_path(ren2->pair->two->path, branch2);
	output(1, "Renamed %s to %s and %s to %s instead",
	       ren1->pair->one->path, new_path1,
	       ren2->pair->one->path, new_path2);
	remove_file(0, ren1->pair->two->path, 0);
	update_file(0, ren1->pair->two->sha1, ren1->pair->two->mode, new_path1);
	update_file(0, ren2->pair->two->sha1, ren2->pair->two->mode, new_path2);
	free(new_path2);
	free(new_path1);
}

static int process_renames(struct path_list *a_renames,
			   struct path_list *b_renames,
			   const char *a_branch,
			   const char *b_branch)
{
	int clean_merge = 1, i, j;
	struct path_list a_by_dst = {NULL, 0, 0, 0}, b_by_dst = {NULL, 0, 0, 0};
	const struct rename *sre;

	for (i = 0; i < a_renames->nr; i++) {
		sre = a_renames->items[i].util;
		path_list_insert(sre->pair->two->path, &a_by_dst)->util
			= sre->dst_entry;
	}
	for (i = 0; i < b_renames->nr; i++) {
		sre = b_renames->items[i].util;
		path_list_insert(sre->pair->two->path, &b_by_dst)->util
			= sre->dst_entry;
	}

	for (i = 0, j = 0; i < a_renames->nr || j < b_renames->nr;) {
		int compare;
		char *src;
		struct path_list *renames1, *renames2, *renames2Dst;
		struct rename *ren1 = NULL, *ren2 = NULL;
		const char *branch1, *branch2;
		const char *ren1_src, *ren1_dst;

		if (i >= a_renames->nr) {
			compare = 1;
			ren2 = b_renames->items[j++].util;
		} else if (j >= b_renames->nr) {
			compare = -1;
			ren1 = a_renames->items[i++].util;
		} else {
			compare = strcmp(a_renames->items[i].path,
					b_renames->items[j].path);
			if (compare <= 0)
				ren1 = a_renames->items[i++].util;
			if (compare >= 0)
				ren2 = b_renames->items[j++].util;
		}

		/* TODO: refactor, so that 1/2 are not needed */
		if (ren1) {
			renames1 = a_renames;
			renames2 = b_renames;
			renames2Dst = &b_by_dst;
			branch1 = a_branch;
			branch2 = b_branch;
		} else {
			struct rename *tmp;
			renames1 = b_renames;
			renames2 = a_renames;
			renames2Dst = &a_by_dst;
			branch1 = b_branch;
			branch2 = a_branch;
			tmp = ren2;
			ren2 = ren1;
			ren1 = tmp;
		}
		src = ren1->pair->one->path;

		ren1->dst_entry->processed = 1;
		ren1->src_entry->processed = 1;

		if (ren1->processed)
			continue;
		ren1->processed = 1;

		ren1_src = ren1->pair->one->path;
		ren1_dst = ren1->pair->two->path;

		if (ren2) {
			const char *ren2_src = ren2->pair->one->path;
			const char *ren2_dst = ren2->pair->two->path;
			/* Renamed in 1 and renamed in 2 */
			if (strcmp(ren1_src, ren2_src) != 0)
				die("ren1.src != ren2.src");
			ren2->dst_entry->processed = 1;
			ren2->processed = 1;
			if (strcmp(ren1_dst, ren2_dst) != 0) {
				clean_merge = 0;
				output(1, "CONFLICT (rename/rename): "
				       "Rename \"%s\"->\"%s\" in branch \"%s\" "
				       "rename \"%s\"->\"%s\" in \"%s\"%s",
				       src, ren1_dst, branch1,
				       src, ren2_dst, branch2,
				       index_only ? " (left unresolved)": "");
				if (index_only) {
					remove_file_from_cache(src);
					update_file(0, ren1->pair->one->sha1,
						    ren1->pair->one->mode, src);
				}
				conflict_rename_rename(ren1, branch1, ren2, branch2);
			} else {
				struct merge_file_info mfi;
				remove_file(1, ren1_src, 1);
				mfi = merge_file(ren1->pair->one,
						 ren1->pair->two,
						 ren2->pair->two,
						 branch1,
						 branch2);
				if (mfi.merge || !mfi.clean)
					output(1, "Renamed %s->%s", src, ren1_dst);

				if (mfi.merge)
					output(2, "Auto-merged %s", ren1_dst);

				if (!mfi.clean) {
					output(1, "CONFLICT (content): merge conflict in %s",
					       ren1_dst);
					clean_merge = 0;

					if (!index_only)
						update_stages(ren1_dst,
							      ren1->pair->one,
							      ren1->pair->two,
							      ren2->pair->two,
							      1 /* clear */);
				}
				update_file(mfi.clean, mfi.sha, mfi.mode, ren1_dst);
			}
		} else {
			/* Renamed in 1, maybe changed in 2 */
			struct path_list_item *item;
			/* we only use sha1 and mode of these */
			struct diff_filespec src_other, dst_other;
			int try_merge, stage = a_renames == renames1 ? 3: 2;

			remove_file(1, ren1_src, index_only || stage == 3);

			hashcpy(src_other.sha1, ren1->src_entry->stages[stage].sha);
			src_other.mode = ren1->src_entry->stages[stage].mode;
			hashcpy(dst_other.sha1, ren1->dst_entry->stages[stage].sha);
			dst_other.mode = ren1->dst_entry->stages[stage].mode;

			try_merge = 0;

			if (path_list_has_path(&current_directory_set, ren1_dst)) {
				clean_merge = 0;
				output(1, "CONFLICT (rename/directory): Renamed %s->%s in %s "
				       " directory %s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				conflict_rename_dir(ren1, branch1);
			} else if (sha_eq(src_other.sha1, null_sha1)) {
				clean_merge = 0;
				output(1, "CONFLICT (rename/delete): Renamed %s->%s in %s "
				       "and deleted in %s",
				       ren1_src, ren1_dst, branch1,
				       branch2);
				update_file(0, ren1->pair->two->sha1, ren1->pair->two->mode, ren1_dst);
			} else if (!sha_eq(dst_other.sha1, null_sha1)) {
				const char *new_path;
				clean_merge = 0;
				try_merge = 1;
				output(1, "CONFLICT (rename/add): Renamed %s->%s in %s. "
				       "%s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				new_path = unique_path(ren1_dst, branch2);
				output(1, "Added as %s instead", new_path);
				update_file(0, dst_other.sha1, dst_other.mode, new_path);
			} else if ((item = path_list_lookup(ren1_dst, renames2Dst))) {
				ren2 = item->util;
				clean_merge = 0;
				ren2->processed = 1;
				output(1, "CONFLICT (rename/rename): Renamed %s->%s in %s. "
				       "Renamed %s->%s in %s",
				       ren1_src, ren1_dst, branch1,
				       ren2->pair->one->path, ren2->pair->two->path, branch2);
				conflict_rename_rename_2(ren1, branch1, ren2, branch2);
			} else
				try_merge = 1;

			if (try_merge) {
				struct diff_filespec *o, *a, *b;
				struct merge_file_info mfi;
				src_other.path = (char *)ren1_src;

				o = ren1->pair->one;
				if (a_renames == renames1) {
					a = ren1->pair->two;
					b = &src_other;
				} else {
					b = ren1->pair->two;
					a = &src_other;
				}
				mfi = merge_file(o, a, b,
						a_branch, b_branch);

				if (mfi.clean &&
				    sha_eq(mfi.sha, ren1->pair->two->sha1) &&
				    mfi.mode == ren1->pair->two->mode)
					/*
					 * This messaged is part of
					 * t6022 test. If you change
					 * it update the test too.
					 */
					output(3, "Skipped %s (merged same as existing)", ren1_dst);
				else {
					if (mfi.merge || !mfi.clean)
						output(1, "Renamed %s => %s", ren1_src, ren1_dst);
					if (mfi.merge)
						output(2, "Auto-merged %s", ren1_dst);
					if (!mfi.clean) {
						output(1, "CONFLICT (rename/modify): Merge conflict in %s",
						       ren1_dst);
						clean_merge = 0;

						if (!index_only)
							update_stages(ren1_dst,
								      o, a, b, 1);
					}
					update_file(mfi.clean, mfi.sha, mfi.mode, ren1_dst);
				}
			}
		}
	}
	path_list_clear(&a_by_dst, 0);
	path_list_clear(&b_by_dst, 0);

	return clean_merge;
}

static unsigned char *stage_sha(const unsigned char *sha, unsigned mode)
{
	return (is_null_sha1(sha) || mode == 0) ? NULL: (unsigned char *)sha;
}

/* Per entry merge function */
static int process_entry(const char *path, struct stage_data *entry,
			 const char *branch1,
			 const char *branch2)
{
	/*
	printf("processing entry, clean cache: %s\n", index_only ? "yes": "no");
	print_index_entry("\tpath: ", entry);
	*/
	int clean_merge = 1;
	unsigned o_mode = entry->stages[1].mode;
	unsigned a_mode = entry->stages[2].mode;
	unsigned b_mode = entry->stages[3].mode;
	unsigned char *o_sha = stage_sha(entry->stages[1].sha, o_mode);
	unsigned char *a_sha = stage_sha(entry->stages[2].sha, a_mode);
	unsigned char *b_sha = stage_sha(entry->stages[3].sha, b_mode);

	if (o_sha && (!a_sha || !b_sha)) {
		/* Case A: Deleted in one */
		if ((!a_sha && !b_sha) ||
		    (sha_eq(a_sha, o_sha) && !b_sha) ||
		    (!a_sha && sha_eq(b_sha, o_sha))) {
			/* Deleted in both or deleted in one and
			 * unchanged in the other */
			if (a_sha)
				output(2, "Removed %s", path);
			/* do not touch working file if it did not exist */
			remove_file(1, path, !a_sha);
		} else {
			/* Deleted in one and changed in the other */
			clean_merge = 0;
			if (!a_sha) {
				output(1, "CONFLICT (delete/modify): %s deleted in %s "
				       "and modified in %s. Version %s of %s left in tree.",
				       path, branch1,
				       branch2, branch2, path);
				update_file(0, b_sha, b_mode, path);
			} else {
				output(1, "CONFLICT (delete/modify): %s deleted in %s "
				       "and modified in %s. Version %s of %s left in tree.",
				       path, branch2,
				       branch1, branch1, path);
				update_file(0, a_sha, a_mode, path);
			}
		}

	} else if ((!o_sha && a_sha && !b_sha) ||
		   (!o_sha && !a_sha && b_sha)) {
		/* Case B: Added in one. */
		const char *add_branch;
		const char *other_branch;
		unsigned mode;
		const unsigned char *sha;
		const char *conf;

		if (a_sha) {
			add_branch = branch1;
			other_branch = branch2;
			mode = a_mode;
			sha = a_sha;
			conf = "file/directory";
		} else {
			add_branch = branch2;
			other_branch = branch1;
			mode = b_mode;
			sha = b_sha;
			conf = "directory/file";
		}
		if (path_list_has_path(&current_directory_set, path)) {
			const char *new_path = unique_path(path, add_branch);
			clean_merge = 0;
			output(1, "CONFLICT (%s): There is a directory with name %s in %s. "
			       "Added %s as %s",
			       conf, path, other_branch, path, new_path);
			remove_file(0, path, 0);
			update_file(0, sha, mode, new_path);
		} else {
			output(2, "Added %s", path);
			update_file(1, sha, mode, path);
		}
	} else if (a_sha && b_sha) {
		/* Case C: Added in both (check for same permissions) and */
		/* case D: Modified in both, but differently. */
		const char *reason = "content";
		struct merge_file_info mfi;
		struct diff_filespec o, a, b;

		if (!o_sha) {
			reason = "add/add";
			o_sha = (unsigned char *)null_sha1;
		}
		output(2, "Auto-merged %s", path);
		o.path = a.path = b.path = (char *)path;
		hashcpy(o.sha1, o_sha);
		o.mode = o_mode;
		hashcpy(a.sha1, a_sha);
		a.mode = a_mode;
		hashcpy(b.sha1, b_sha);
		b.mode = b_mode;

		mfi = merge_file(&o, &a, &b,
				 branch1, branch2);

		if (mfi.clean)
			update_file(1, mfi.sha, mfi.mode, path);
		else {
			clean_merge = 0;
			output(1, "CONFLICT (%s): Merge conflict in %s",
					reason, path);

			if (index_only)
				update_file(0, mfi.sha, mfi.mode, path);
			else
				update_file_flags(mfi.sha, mfi.mode, path,
					      0 /* update_cache */, 1 /* update_working_directory */);
		}
	} else if (!o_sha && !a_sha && !b_sha) {
		/*
		 * this entry was deleted altogether. a_mode == 0 means
		 * we had that path and want to actively remove it.
		 */
		remove_file(1, path, !a_mode);
	} else
		die("Fatal merge failure, shouldn't happen.");

	return clean_merge;
}

static int merge_trees(struct tree *head,
		       struct tree *merge,
		       struct tree *common,
		       const char *branch1,
		       const char *branch2,
		       struct tree **result)
{
	int code, clean;

	if (subtree_merge) {
		merge = shift_tree_object(head, merge);
		common = shift_tree_object(head, common);
	}

	if (sha_eq(common->object.sha1, merge->object.sha1)) {
		output(0, "Already uptodate!");
		*result = head;
		return 1;
	}

	code = git_merge_trees(index_only, common, head, merge);

	if (code != 0)
		die("merging of trees %s and %s failed",
		    sha1_to_hex(head->object.sha1),
		    sha1_to_hex(merge->object.sha1));

	if (unmerged_index()) {
		struct path_list *entries, *re_head, *re_merge;
		int i;
		path_list_clear(&current_file_set, 1);
		path_list_clear(&current_directory_set, 1);
		get_files_dirs(head);
		get_files_dirs(merge);

		entries = get_unmerged();
		re_head  = get_renames(head, common, head, merge, entries);
		re_merge = get_renames(merge, common, head, merge, entries);
		clean = process_renames(re_head, re_merge,
				branch1, branch2);
		for (i = 0; i < entries->nr; i++) {
			const char *path = entries->items[i].path;
			struct stage_data *e = entries->items[i].util;
			if (!e->processed
				&& !process_entry(path, e, branch1, branch2))
				clean = 0;
		}

		path_list_clear(re_merge, 0);
		path_list_clear(re_head, 0);
		path_list_clear(entries, 1);

	}
	else
		clean = 1;

	if (index_only)
		*result = git_write_tree();

	return clean;
}

static struct commit_list *reverse_commit_list(struct commit_list *list)
{
	struct commit_list *next = NULL, *current, *backup;
	for (current = list; current; current = backup) {
		backup = current->next;
		current->next = next;
		next = current;
	}
	return next;
}

/*
 * Merge the commits h1 and h2, return the resulting virtual
 * commit object and a flag indicating the cleanness of the merge.
 */
static int merge(struct commit *h1,
		 struct commit *h2,
		 const char *branch1,
		 const char *branch2,
		 struct commit_list *ca,
		 struct commit **result)
{
	struct commit_list *iter;
	struct commit *merged_common_ancestors;
	struct tree *mrtree;
	int clean;

	if (show(4)) {
		output(4, "Merging:");
		output_commit_title(h1);
		output_commit_title(h2);
	}

	if (!ca) {
		ca = get_merge_bases(h1, h2, 1);
		ca = reverse_commit_list(ca);
	}

	if (show(5)) {
		output(5, "found %u common ancestor(s):", commit_list_count(ca));
		for (iter = ca; iter; iter = iter->next)
			output_commit_title(iter->item);
	}

	merged_common_ancestors = pop_commit(&ca);
	if (merged_common_ancestors == NULL) {
		/* if there is no common ancestor, make an empty tree */
		struct tree *tree = xcalloc(1, sizeof(struct tree));

		tree->object.parsed = 1;
		tree->object.type = OBJ_TREE;
		pretend_sha1_file(NULL, 0, OBJ_TREE, tree->object.sha1);
		merged_common_ancestors = make_virtual_commit(tree, "ancestor");
	}

	for (iter = ca; iter; iter = iter->next) {
		call_depth++;
		/*
		 * When the merge fails, the result contains files
		 * with conflict markers. The cleanness flag is
		 * ignored, it was never actually used, as result of
		 * merge_trees has always overwritten it: the committed
		 * "conflicts" were already resolved.
		 */
		discard_cache();
		merge(merged_common_ancestors, iter->item,
		      "Temporary merge branch 1",
		      "Temporary merge branch 2",
		      NULL,
		      &merged_common_ancestors);
		call_depth--;

		if (!merged_common_ancestors)
			die("merge returned no commit");
	}

	discard_cache();
	if (!call_depth) {
		read_cache();
		index_only = 0;
	} else
		index_only = 1;

	clean = merge_trees(h1->tree, h2->tree, merged_common_ancestors->tree,
			    branch1, branch2, &mrtree);

	if (index_only) {
		*result = make_virtual_commit(mrtree, "merged tree");
		commit_list_insert(h1, &(*result)->parents);
		commit_list_insert(h2, &(*result)->parents->next);
	}
	flush_output();
	return clean;
}

static const char *better_branch_name(const char *branch)
{
	static char githead_env[8 + 40 + 1];
	char *name;

	if (strlen(branch) != 40)
		return branch;
	sprintf(githead_env, "GITHEAD_%s", branch);
	name = getenv(githead_env);
	return name ? name : branch;
}

static struct commit *get_ref(const char *ref)
{
	unsigned char sha1[20];
	struct object *object;

	if (get_sha1(ref, sha1))
		die("Could not resolve ref '%s'", ref);
	object = deref_tag(parse_object(sha1), ref, strlen(ref));
	if (object->type == OBJ_TREE)
		return make_virtual_commit((struct tree*)object,
			better_branch_name(ref));
	if (object->type != OBJ_COMMIT)
		return NULL;
	if (parse_commit((struct commit *)object))
		die("Could not parse commit '%s'", sha1_to_hex(object->sha1));
	return (struct commit *)object;
}

static int merge_config(const char *var, const char *value)
{
	if (!strcasecmp(var, "merge.verbosity")) {
		verbosity = git_config_int(var, value);
		return 0;
	}
	return git_default_config(var, value);
}

int main(int argc, char *argv[])
{
	static const char *bases[20];
	static unsigned bases_count = 0;
	int i, clean;
	const char *branch1, *branch2;
	struct commit *result, *h1, *h2;
	struct commit_list *ca = NULL;
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	int index_fd;

	if (argv[0]) {
		int namelen = strlen(argv[0]);
		if (8 < namelen &&
		    !strcmp(argv[0] + namelen - 8, "-subtree"))
			subtree_merge = 1;
	}

	git_config(merge_config);
	if (getenv("GIT_MERGE_VERBOSITY"))
		verbosity = strtol(getenv("GIT_MERGE_VERBOSITY"), NULL, 10);

	if (argc < 4)
		die("Usage: %s <base>... -- <head> <remote> ...\n", argv[0]);

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--"))
			break;
		if (bases_count < sizeof(bases)/sizeof(*bases))
			bases[bases_count++] = argv[i];
	}
	if (argc - i != 3) /* "--" "<head>" "<remote>" */
		die("Not handling anything other than two heads merge.");
	if (verbosity >= 5)
		buffer_output = 0;

	branch1 = argv[++i];
	branch2 = argv[++i];

	h1 = get_ref(branch1);
	h2 = get_ref(branch2);

	branch1 = better_branch_name(branch1);
	branch2 = better_branch_name(branch2);

	if (show(3))
		printf("Merging %s with %s\n", branch1, branch2);

	index_fd = hold_locked_index(lock, 1);

	for (i = 0; i < bases_count; i++) {
		struct commit *ancestor = get_ref(bases[i]);
		ca = commit_list_insert(ancestor, &ca);
	}
	clean = merge(h1, h2, branch1, branch2, ca, &result);

	if (active_cache_changed &&
	    (write_cache(index_fd, active_cache, active_nr) ||
	     close(index_fd) || commit_locked_index(lock)))
			die ("unable to write %s", get_index_file());

	return clean ? 0: 1;
}
