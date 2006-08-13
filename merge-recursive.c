/*
 * Recursive Merge algorithm stolen from git-merge-recursive.py by
 * Fredrik Kuivinen.
 * The thieves were Alex Riesen and Johannes Schindelin, in June/July 2006
 */
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
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
	return a && b && memcmp(a, b, 20) == 0;
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

static struct path_list current_file_set = {NULL, 0, 0, 1};
static struct path_list current_directory_set = {NULL, 0, 0, 1};

static int output_indent = 0;

static void output(const char *fmt, ...)
{
	va_list args;
	int i;
	for (i = output_indent; i--;)
		fputs("  ", stdout);
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
	fputc('\n', stdout);
}

static void output_commit_title(struct commit *commit)
{
	int i;
	for (i = output_indent; i--;)
		fputs("  ", stdout);
	if (commit->util)
		printf("virtual %s\n", (char *)commit->util);
	else {
		printf("%s ", sha1_to_hex(commit->object.sha1));
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

static const char *current_index_file = NULL;
static const char *original_index_file;
static const char *temporary_index_file;
static int cache_dirty = 0;

static int flush_cache(void)
{
	/* flush temporary index */
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	int fd = hold_lock_file_for_update(lock, current_index_file, 1);
	if (write_cache(fd, active_cache, active_nr) ||
			close(fd) || commit_lock_file(lock))
		die ("unable to write %s", current_index_file);
	discard_cache();
	cache_dirty = 0;
	return 0;
}

static void setup_index(int temp)
{
	current_index_file = temp ? temporary_index_file: original_index_file;
	if (cache_dirty) {
		discard_cache();
		cache_dirty = 0;
	}
	unlink(temporary_index_file);
	discard_cache();
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

	memcpy(ce->sha1, sha1, 20);
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
	if (!cache_dirty)
		read_cache_from(current_index_file);
	cache_dirty++;
	ce = make_cache_entry(mode, sha1 ? sha1 : null_sha1, path, stage, refresh);
	if (!ce)
		return error("cache_addinfo failed: %s", strerror(cache_errno));
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

static int git_read_tree(struct tree *tree)
{
	int rc;
	struct object_list *trees = NULL;
	struct unpack_trees_options opts;

	if (cache_dirty)
		die("read-tree with dirty cache");

	memset(&opts, 0, sizeof(opts));
	object_list_append(&tree->object, &trees);
	rc = unpack_trees(trees, &opts);
	cache_tree_free(&active_cache_tree);

	if (rc == 0)
		cache_dirty = 1;

	return rc;
}

static int git_merge_trees(int index_only,
			   struct tree *common,
			   struct tree *head,
			   struct tree *merge)
{
	int rc;
	struct object_list *trees = NULL;
	struct unpack_trees_options opts;

	if (!cache_dirty) {
		read_cache_from(current_index_file);
		cache_dirty = 1;
	}

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

	cache_dirty = 1;

	return rc;
}

static struct tree *git_write_tree(void)
{
	struct tree *result = NULL;

	if (cache_dirty) {
		unsigned i;
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (ce_stage(ce))
				return NULL;
		}
	} else
		read_cache_from(current_index_file);

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	if (!cache_tree_fully_valid(active_cache_tree) &&
			cache_tree_update(active_cache_tree,
				active_cache, active_nr, 0, 0) < 0)
		die("error building trees");

	result = lookup_tree(active_cache_tree->sha1);

	flush_cache();
	cache_dirty = 0;

	return result;
}

static int save_files_dirs(const unsigned char *sha1,
		const char *base, int baselen, const char *path,
		unsigned int mode, int stage)
{
	int len = strlen(path);
	char *newpath = malloc(baselen + len + 1);
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
	if (!cache_dirty) {
		read_cache_from(current_index_file);
		cache_dirty++;
	}
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
		memcpy(e->stages[ce_stage(ce)].sha, ce->sha1, 20);
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
 * Get information of all renames which occured between 'o_tree' and
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

int update_stages(const char *path, struct diff_filespec *o,
		struct diff_filespec *a, struct diff_filespec *b, int clear)
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
	dirs = malloc(len+1);
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

int remove_file(int clean, const char *path)
{
	int update_cache = index_only || clean;
	int update_working_directory = !index_only;

	if (update_cache) {
		if (!cache_dirty)
			read_cache_from(current_index_file);
		cache_dirty++;
		if (remove_file_from_cache(path))
			return -1;
	}
	if (update_working_directory)
	{
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
	/* path points to cache entries, so strdup before messing with it */
	char *buf = strdup(path);
	int result = safe_create_leading_directories(buf);
	free(buf);
	return result;
}

static void flush_buffer(int fd, const char *buf, unsigned long size)
{
	while (size > 0) {
		long ret = xwrite(fd, buf, size);
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

void update_file_flags(const unsigned char *sha,
		       unsigned mode,
		       const char *path,
		       int update_cache,
		       int update_wd)
{
	if (index_only)
		update_wd = 0;

	if (update_wd) {
		char type[20];
		void *buf;
		unsigned long size;

		buf = read_sha1_file(sha, type, &size);
		if (!buf)
			die("cannot read object %s '%s'", sha1_to_hex(sha), path);
		if (strcmp(type, blob_type) != 0)
			die("blob expected for %s '%s'", sha1_to_hex(sha), path);

		if (S_ISREG(mode)) {
			int fd;
			if (mkdir_p(path, 0777))
				die("failed to create path %s: %s", path, strerror(errno));
			unlink(path);
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
			char *lnk = malloc(size + 1);
			memcpy(lnk, buf, size);
			lnk[size] = '\0';
			mkdir_p(path, 0777);
			unlink(lnk);
			symlink(lnk, path);
		} else
			die("do not know what to do with %06o %s '%s'",
			    mode, sha1_to_hex(sha), path);
	}
	if (update_cache)
		add_cacheinfo(mode, sha, path, 0, update_wd, ADD_CACHE_OK_TO_ADD);
}

void update_file(int clean,
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

static char *git_unpack_file(const unsigned char *sha1, char *path)
{
	void *buf;
	char type[20];
	unsigned long size;
	int fd;

	buf = read_sha1_file(sha1, type, &size);
	if (!buf || strcmp(type, blob_type))
		die("unable to read blob object %s", sha1_to_hex(sha1));

	strcpy(path, ".merge_file_XXXXXX");
	fd = mkstemp(path);
	if (fd < 0)
		die("unable to create temp-file");
	flush_buffer(fd, buf, size);
	close(fd);
	return path;
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
			memcpy(result.sha, a->sha1, 20);
		} else {
			result.mode = b->mode;
			memcpy(result.sha, b->sha1, 20);
		}
	} else {
		if (!sha_eq(a->sha1, o->sha1) && !sha_eq(b->sha1, o->sha1))
			result.merge = 1;

		result.mode = a->mode == o->mode ? b->mode: a->mode;

		if (sha_eq(a->sha1, o->sha1))
			memcpy(result.sha, b->sha1, 20);
		else if (sha_eq(b->sha1, o->sha1))
			memcpy(result.sha, a->sha1, 20);
		else if (S_ISREG(a->mode)) {
			int code = 1, fd;
			struct stat st;
			char orig[PATH_MAX];
			char src1[PATH_MAX];
			char src2[PATH_MAX];
			const char *argv[] = {
				"merge", "-L", NULL, "-L", NULL, "-L", NULL,
				NULL, NULL, NULL,
				NULL
			};
			char *la, *lb, *lo;

			git_unpack_file(o->sha1, orig);
			git_unpack_file(a->sha1, src1);
			git_unpack_file(b->sha1, src2);

			argv[2] = la = strdup(mkpath("%s/%s", branch1, a->path));
			argv[6] = lb = strdup(mkpath("%s/%s", branch2, b->path));
			argv[4] = lo = strdup(mkpath("orig/%s", o->path));
			argv[7] = src1;
			argv[8] = orig;
			argv[9] = src2,

			code = run_command_v(10, argv);

			free(la);
			free(lb);
			free(lo);
			if (code && code < -256) {
				die("Failed to execute 'merge'. merge(1) is used as the "
				    "file-level merge tool. Is 'merge' in your path?");
			}
			fd = open(src1, O_RDONLY);
			if (fd < 0 || fstat(fd, &st) < 0 ||
					index_fd(result.sha, fd, &st, 1,
						"blob"))
				die("Unable to add %s to database", src1);

			unlink(orig);
			unlink(src1);
			unlink(src2);

			result.clean = WEXITSTATUS(code) == 0;
		} else {
			if (!(S_ISLNK(a->mode) || S_ISLNK(b->mode)))
				die("cannot merge modes?");

			memcpy(result.sha, a->sha1, 20);

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
		output("%s is a directory in %s adding as %s instead",
		       ren1_dst, branch2, dst_name1);
		remove_file(0, ren1_dst);
	}
	if (path_list_has_path(&current_directory_set, ren2_dst)) {
		dst_name2 = del[delp++] = unique_path(ren2_dst, branch2);
		output("%s is a directory in %s adding as %s instead",
		       ren2_dst, branch1, dst_name2);
		remove_file(0, ren2_dst);
	}
	update_stages(dst_name1, NULL, ren1->pair->two, NULL, 1);
	update_stages(dst_name2, NULL, NULL, ren2->pair->two, 1);
	while (delp--)
		free(del[delp]);
}

static void conflict_rename_dir(struct rename *ren1,
				const char *branch1)
{
	char *new_path = unique_path(ren1->pair->two->path, branch1);
	output("Renaming %s to %s instead", ren1->pair->one->path, new_path);
	remove_file(0, ren1->pair->two->path);
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
	output("Renaming %s to %s and %s to %s instead",
	       ren1->pair->one->path, new_path1,
	       ren2->pair->one->path, new_path2);
	remove_file(0, ren1->pair->two->path);
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
				output("CONFLICT (rename/rename): "
				       "Rename %s->%s in branch %s "
				       "rename %s->%s in %s",
				       src, ren1_dst, branch1,
				       src, ren2_dst, branch2);
				conflict_rename_rename(ren1, branch1, ren2, branch2);
			} else {
				struct merge_file_info mfi;
				remove_file(1, ren1_src);
				mfi = merge_file(ren1->pair->one,
						 ren1->pair->two,
						 ren2->pair->two,
						 branch1,
						 branch2);
				if (mfi.merge || !mfi.clean)
					output("Renaming %s->%s", src, ren1_dst);

				if (mfi.merge)
					output("Auto-merging %s", ren1_dst);

				if (!mfi.clean) {
					output("CONFLICT (content): merge conflict in %s",
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

			remove_file(1, ren1_src);

			memcpy(src_other.sha1,
					ren1->src_entry->stages[stage].sha, 20);
			src_other.mode = ren1->src_entry->stages[stage].mode;
			memcpy(dst_other.sha1,
					ren1->dst_entry->stages[stage].sha, 20);
			dst_other.mode = ren1->dst_entry->stages[stage].mode;

			try_merge = 0;

			if (path_list_has_path(&current_directory_set, ren1_dst)) {
				clean_merge = 0;
				output("CONFLICT (rename/directory): Rename %s->%s in %s "
				       " directory %s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				conflict_rename_dir(ren1, branch1);
			} else if (sha_eq(src_other.sha1, null_sha1)) {
				clean_merge = 0;
				output("CONFLICT (rename/delete): Rename %s->%s in %s "
				       "and deleted in %s",
				       ren1_src, ren1_dst, branch1,
				       branch2);
				update_file(0, ren1->pair->two->sha1, ren1->pair->two->mode, ren1_dst);
			} else if (!sha_eq(dst_other.sha1, null_sha1)) {
				const char *new_path;
				clean_merge = 0;
				try_merge = 1;
				output("CONFLICT (rename/add): Rename %s->%s in %s. "
				       "%s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				new_path = unique_path(ren1_dst, branch2);
				output("Adding as %s instead", new_path);
				update_file(0, dst_other.sha1, dst_other.mode, new_path);
			} else if ((item = path_list_lookup(ren1_dst, renames2Dst))) {
				ren2 = item->util;
				clean_merge = 0;
				ren2->processed = 1;
				output("CONFLICT (rename/rename): Rename %s->%s in %s. "
				       "Rename %s->%s in %s",
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

				if (mfi.merge || !mfi.clean)
					output("Renaming %s => %s", ren1_src, ren1_dst);
				if (mfi.merge)
					output("Auto-merging %s", ren1_dst);
				if (!mfi.clean) {
					output("CONFLICT (rename/modify): Merge conflict in %s",
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
	path_list_clear(&a_by_dst, 0);
	path_list_clear(&b_by_dst, 0);

	if (cache_dirty)
		flush_cache();
	return clean_merge;
}

static unsigned char *has_sha(const unsigned char *sha)
{
	return memcmp(sha, null_sha1, 20) == 0 ? NULL: (unsigned char *)sha;
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
	unsigned char *o_sha = has_sha(entry->stages[1].sha);
	unsigned char *a_sha = has_sha(entry->stages[2].sha);
	unsigned char *b_sha = has_sha(entry->stages[3].sha);
	unsigned o_mode = entry->stages[1].mode;
	unsigned a_mode = entry->stages[2].mode;
	unsigned b_mode = entry->stages[3].mode;

	if (o_sha && (!a_sha || !b_sha)) {
		/* Case A: Deleted in one */
		if ((!a_sha && !b_sha) ||
		    (sha_eq(a_sha, o_sha) && !b_sha) ||
		    (!a_sha && sha_eq(b_sha, o_sha))) {
			/* Deleted in both or deleted in one and
			 * unchanged in the other */
			if (a_sha)
				output("Removing %s", path);
			remove_file(1, path);
		} else {
			/* Deleted in one and changed in the other */
			clean_merge = 0;
			if (!a_sha) {
				output("CONFLICT (delete/modify): %s deleted in %s "
				       "and modified in %s. Version %s of %s left in tree.",
				       path, branch1,
				       branch2, branch2, path);
				update_file(0, b_sha, b_mode, path);
			} else {
				output("CONFLICT (delete/modify): %s deleted in %s "
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
			output("CONFLICT (%s): There is a directory with name %s in %s. "
			       "Adding %s as %s",
			       conf, path, other_branch, path, new_path);
			remove_file(0, path);
			update_file(0, sha, mode, new_path);
		} else {
			output("Adding %s", path);
			update_file(1, sha, mode, path);
		}
	} else if (!o_sha && a_sha && b_sha) {
		/* Case C: Added in both (check for same permissions). */
		if (sha_eq(a_sha, b_sha)) {
			if (a_mode != b_mode) {
				clean_merge = 0;
				output("CONFLICT: File %s added identically in both branches, "
				       "but permissions conflict %06o->%06o",
				       path, a_mode, b_mode);
				output("CONFLICT: adding with permission: %06o", a_mode);
				update_file(0, a_sha, a_mode, path);
			} else {
				/* This case is handled by git-read-tree */
				assert(0 && "This case must be handled by git-read-tree");
			}
		} else {
			const char *new_path1, *new_path2;
			clean_merge = 0;
			new_path1 = unique_path(path, branch1);
			new_path2 = unique_path(path, branch2);
			output("CONFLICT (add/add): File %s added non-identically "
			       "in both branches. Adding as %s and %s instead.",
			       path, new_path1, new_path2);
			remove_file(0, path);
			update_file(0, a_sha, a_mode, new_path1);
			update_file(0, b_sha, b_mode, new_path2);
		}

	} else if (o_sha && a_sha && b_sha) {
		/* case D: Modified in both, but differently. */
		struct merge_file_info mfi;
		struct diff_filespec o, a, b;

		output("Auto-merging %s", path);
		o.path = a.path = b.path = (char *)path;
		memcpy(o.sha1, o_sha, 20);
		o.mode = o_mode;
		memcpy(a.sha1, a_sha, 20);
		a.mode = a_mode;
		memcpy(b.sha1, b_sha, 20);
		b.mode = b_mode;

		mfi = merge_file(&o, &a, &b,
				 branch1, branch2);

		if (mfi.clean)
			update_file(1, mfi.sha, mfi.mode, path);
		else {
			clean_merge = 0;
			output("CONFLICT (content): Merge conflict in %s", path);

			if (index_only)
				update_file(0, mfi.sha, mfi.mode, path);
			else
				update_file_flags(mfi.sha, mfi.mode, path,
					      0 /* update_cache */, 1 /* update_working_directory */);
		}
	} else
		die("Fatal merge failure, shouldn't happen.");

	if (cache_dirty)
		flush_cache();

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
	if (sha_eq(common->object.sha1, merge->object.sha1)) {
		output("Already uptodate!");
		*result = head;
		return 1;
	}

	code = git_merge_trees(index_only, common, head, merge);

	if (code != 0)
		die("merging of trees %s and %s failed",
		    sha1_to_hex(head->object.sha1),
		    sha1_to_hex(merge->object.sha1));

	*result = git_write_tree();

	if (!*result) {
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
			if (e->processed)
				continue;
			if (!process_entry(path, e, branch1, branch2))
				clean = 0;
		}

		path_list_clear(re_merge, 0);
		path_list_clear(re_head, 0);
		path_list_clear(entries, 1);

		if (clean || index_only)
			*result = git_write_tree();
		else
			*result = NULL;
	} else {
		clean = 1;
		printf("merging of trees %s and %s resulted in %s\n",
		       sha1_to_hex(head->object.sha1),
		       sha1_to_hex(merge->object.sha1),
		       sha1_to_hex((*result)->object.sha1));
	}

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
 * commit object and a flag indicating the cleaness of the merge.
 */
static
int merge(struct commit *h1,
			  struct commit *h2,
			  const char *branch1,
			  const char *branch2,
			  int call_depth /* =0 */,
			  struct commit *ancestor /* =None */,
			  struct commit **result)
{
	struct commit_list *ca = NULL, *iter;
	struct commit *merged_common_ancestors;
	struct tree *mrtree;
	int clean;

	output("Merging:");
	output_commit_title(h1);
	output_commit_title(h2);

	if (ancestor)
		commit_list_insert(ancestor, &ca);
	else
		ca = reverse_commit_list(get_merge_bases(h1, h2, 1));

	output("found %u common ancestor(s):", commit_list_count(ca));
	for (iter = ca; iter; iter = iter->next)
		output_commit_title(iter->item);

	merged_common_ancestors = pop_commit(&ca);
	if (merged_common_ancestors == NULL) {
		/* if there is no common ancestor, make an empty tree */
		struct tree *tree = xcalloc(1, sizeof(struct tree));
		unsigned char hdr[40];
		int hdrlen;

		tree->object.parsed = 1;
		tree->object.type = OBJ_TREE;
		write_sha1_file_prepare(NULL, 0, tree_type, tree->object.sha1,
					hdr, &hdrlen);
		merged_common_ancestors = make_virtual_commit(tree, "ancestor");
	}

	for (iter = ca; iter; iter = iter->next) {
		output_indent = call_depth + 1;
		/*
		 * When the merge fails, the result contains files
		 * with conflict markers. The cleanness flag is
		 * ignored, it was never acutally used, as result of
		 * merge_trees has always overwritten it: the commited
		 * "conflicts" were already resolved.
		 */
		merge(merged_common_ancestors, iter->item,
		      "Temporary merge branch 1",
		      "Temporary merge branch 2",
		      call_depth + 1,
		      NULL,
		      &merged_common_ancestors);
		output_indent = call_depth;

		if (!merged_common_ancestors)
			die("merge returned no commit");
	}

	if (call_depth == 0) {
		setup_index(0 /* $GIT_DIR/index */);
		index_only = 0;
	} else {
		setup_index(1 /* temporary index */);
		git_read_tree(h1->tree);
		index_only = 1;
	}

	clean = merge_trees(h1->tree, h2->tree, merged_common_ancestors->tree,
			    branch1, branch2, &mrtree);

	if (!ancestor && (clean || index_only)) {
		*result = make_virtual_commit(mrtree, "merged tree");
		commit_list_insert(h1, &(*result)->parents);
		commit_list_insert(h2, &(*result)->parents->next);
	} else
		*result = NULL;

	return clean;
}

static struct commit *get_ref(const char *ref)
{
	unsigned char sha1[20];
	struct object *object;

	if (get_sha1(ref, sha1))
		die("Could not resolve ref '%s'", ref);
	object = deref_tag(parse_object(sha1), ref, strlen(ref));
	if (object->type != OBJ_COMMIT)
		return NULL;
	if (parse_commit((struct commit *)object))
		die("Could not parse commit '%s'", sha1_to_hex(object->sha1));
	return (struct commit *)object;
}

int main(int argc, char *argv[])
{
	static const char *bases[2];
	static unsigned bases_count = 0;
	int i, clean;
	const char *branch1, *branch2;
	struct commit *result, *h1, *h2;

	original_index_file = getenv("GIT_INDEX_FILE");

	if (!original_index_file)
		original_index_file = strdup(git_path("index"));

	temporary_index_file = strdup(git_path("mrg-rcrsv-tmp-idx"));

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

	branch1 = argv[++i];
	branch2 = argv[++i];
	printf("Merging %s with %s\n", branch1, branch2);

	h1 = get_ref(branch1);
	h2 = get_ref(branch2);

	if (bases_count == 1) {
		struct commit *ancestor = get_ref(bases[0]);
		clean = merge(h1, h2, branch1, branch2, 0, ancestor, &result);
	} else
		clean = merge(h1, h2, branch1, branch2, 0, NULL, &result);

	if (cache_dirty)
		flush_cache();

	return clean ? 0: 1;
}

/*
vim: sw=8 noet
*/
