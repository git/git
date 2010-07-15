/*
 * Recursive Merge algorithm stolen from git-merge-recursive.py by
 * Fredrik Kuivinen.
 * The thieves were Alex Riesen and Johannes Schindelin, in June/July 2006
 */
#include "advice.h"
#include "cache.h"
#include "cache-tree.h"
#include "commit.h"
#include "blob.h"
#include "builtin.h"
#include "tree-walk.h"
#include "diff.h"
#include "diffcore.h"
#include "tag.h"
#include "unpack-trees.h"
#include "string-list.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "attr.h"
#include "merge-recursive.h"
#include "dir.h"

static struct tree *shift_tree_object(struct tree *one, struct tree *two,
				      const char *subtree_shift)
{
	unsigned char shifted[20];

	if (!*subtree_shift) {
		shift_tree(one->object.sha1, two->object.sha1, shifted, 0);
	} else {
		shift_tree_by(one->object.sha1, two->object.sha1, shifted,
			      subtree_shift);
	}
	if (!hashcmp(two->object.sha1, shifted))
		return two;
	return lookup_tree(shifted);
}

/*
 * A virtual commit has (const char *)commit->util set to the name.
 */

static struct commit *make_virtual_commit(struct tree *tree, const char *comment)
{
	struct commit *commit = xcalloc(1, sizeof(struct commit));
	commit->tree = tree;
	commit->util = (void*)comment;
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

static int show(struct merge_options *o, int v)
{
	return (!o->call_depth && o->verbosity >= v) || o->verbosity >= 5;
}

static void flush_output(struct merge_options *o)
{
	if (o->obuf.len) {
		fputs(o->obuf.buf, stdout);
		strbuf_reset(&o->obuf);
	}
}

__attribute__((format (printf, 3, 4)))
static void output(struct merge_options *o, int v, const char *fmt, ...)
{
	int len;
	va_list ap;

	if (!show(o, v))
		return;

	strbuf_grow(&o->obuf, o->call_depth * 2 + 2);
	memset(o->obuf.buf + o->obuf.len, ' ', o->call_depth * 2);
	strbuf_setlen(&o->obuf, o->obuf.len + o->call_depth * 2);

	va_start(ap, fmt);
	len = vsnprintf(o->obuf.buf + o->obuf.len, strbuf_avail(&o->obuf), fmt, ap);
	va_end(ap);

	if (len < 0)
		len = 0;
	if (len >= strbuf_avail(&o->obuf)) {
		strbuf_grow(&o->obuf, len + 2);
		va_start(ap, fmt);
		len = vsnprintf(o->obuf.buf + o->obuf.len, strbuf_avail(&o->obuf), fmt, ap);
		va_end(ap);
		if (len >= strbuf_avail(&o->obuf)) {
			die("this should not happen, your snprintf is broken");
		}
	}
	strbuf_setlen(&o->obuf, o->obuf.len + len);
	strbuf_add(&o->obuf, "\n", 1);
	if (!o->buffer_output)
		flush_output(o);
}

static void output_commit_title(struct merge_options *o, struct commit *commit)
{
	int i;
	flush_output(o);
	for (i = o->call_depth; i--;)
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

static int add_cacheinfo(unsigned int mode, const unsigned char *sha1,
		const char *path, int stage, int refresh, int options)
{
	struct cache_entry *ce;
	ce = make_cache_entry(mode, sha1 ? sha1 : null_sha1, path, stage, refresh);
	if (!ce)
		return error("addinfo_cache failed for path '%s'", path);
	return add_cache_entry(ce, options);
}

static void init_tree_desc_from_tree(struct tree_desc *desc, struct tree *tree)
{
	parse_tree(tree);
	init_tree_desc(desc, tree->buffer, tree->size);
}

static int git_merge_trees(int index_only,
			   struct tree *common,
			   struct tree *head,
			   struct tree *merge)
{
	int rc;
	struct tree_desc t[3];
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	if (index_only)
		opts.index_only = 1;
	else
		opts.update = 1;
	opts.merge = 1;
	opts.head_idx = 2;
	opts.fn = threeway_merge;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.msgs = get_porcelain_error_msgs();

	init_tree_desc_from_tree(t+0, common);
	init_tree_desc_from_tree(t+1, head);
	init_tree_desc_from_tree(t+2, merge);

	rc = unpack_trees(3, t, &opts);
	cache_tree_free(&active_cache_tree);
	return rc;
}

struct tree *write_tree_from_memory(struct merge_options *o)
{
	struct tree *result = NULL;

	if (unmerged_cache()) {
		int i;
		fprintf(stderr, "BUG: There are unmerged index entries:\n");
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (ce_stage(ce))
				fprintf(stderr, "BUG: %d %.*s", ce_stage(ce),
					(int)ce_namelen(ce), ce->name);
		}
		die("Bug in merge-recursive.c");
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
		unsigned int mode, int stage, void *context)
{
	int len = strlen(path);
	char *newpath = xmalloc(baselen + len + 1);
	struct merge_options *o = context;

	memcpy(newpath, base, baselen);
	memcpy(newpath + baselen, path, len);
	newpath[baselen + len] = '\0';

	if (S_ISDIR(mode))
		string_list_insert(&o->current_directory_set, newpath);
	else
		string_list_insert(&o->current_file_set, newpath);
	free(newpath);

	return (S_ISDIR(mode) ? READ_TREE_RECURSIVE : 0);
}

static int get_files_dirs(struct merge_options *o, struct tree *tree)
{
	int n;
	if (read_tree_recursive(tree, "", 0, 0, NULL, save_files_dirs, o))
		return 0;
	n = o->current_file_set.nr + o->current_directory_set.nr;
	return n;
}

/*
 * Returns an index_entry instance which doesn't have to correspond to
 * a real cache entry in Git's index.
 */
static struct stage_data *insert_stage_data(const char *path,
		struct tree *o, struct tree *a, struct tree *b,
		struct string_list *entries)
{
	struct string_list_item *item;
	struct stage_data *e = xcalloc(1, sizeof(struct stage_data));
	get_tree_entry(o->object.sha1, path,
			e->stages[1].sha, &e->stages[1].mode);
	get_tree_entry(a->object.sha1, path,
			e->stages[2].sha, &e->stages[2].mode);
	get_tree_entry(b->object.sha1, path,
			e->stages[3].sha, &e->stages[3].mode);
	item = string_list_insert(entries, path);
	item->util = e;
	return e;
}

/*
 * Create a dictionary mapping file names to stage_data objects. The
 * dictionary contains one entry for every path with a non-zero stage entry.
 */
static struct string_list *get_unmerged(void)
{
	struct string_list *unmerged = xcalloc(1, sizeof(struct string_list));
	int i;

	unmerged->strdup_strings = 1;

	for (i = 0; i < active_nr; i++) {
		struct string_list_item *item;
		struct stage_data *e;
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;

		item = string_list_lookup(unmerged, ce->name);
		if (!item) {
			item = string_list_insert(unmerged, ce->name);
			item->util = xcalloc(1, sizeof(struct stage_data));
		}
		e = item->util;
		e->stages[ce_stage(ce)].mode = ce->ce_mode;
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
static struct string_list *get_renames(struct merge_options *o,
				       struct tree *tree,
				       struct tree *o_tree,
				       struct tree *a_tree,
				       struct tree *b_tree,
				       struct string_list *entries)
{
	int i;
	struct string_list *renames;
	struct diff_options opts;

	renames = xcalloc(1, sizeof(struct string_list));
	diff_setup(&opts);
	DIFF_OPT_SET(&opts, RECURSIVE);
	opts.detect_rename = DIFF_DETECT_RENAME;
	opts.rename_limit = o->merge_rename_limit >= 0 ? o->merge_rename_limit :
			    o->diff_rename_limit >= 0 ? o->diff_rename_limit :
			    500;
	opts.warn_on_too_large_rename = 1;
	opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	if (diff_setup_done(&opts) < 0)
		die("diff setup failed");
	diff_tree_sha1(o_tree->object.sha1, tree->object.sha1, "", &opts);
	diffcore_std(&opts);
	for (i = 0; i < diff_queued_diff.nr; ++i) {
		struct string_list_item *item;
		struct rename *re;
		struct diff_filepair *pair = diff_queued_diff.queue[i];
		if (pair->status != 'R') {
			diff_free_filepair(pair);
			continue;
		}
		re = xmalloc(sizeof(*re));
		re->processed = 0;
		re->pair = pair;
		item = string_list_lookup(entries, re->pair->one->path);
		if (!item)
			re->src_entry = insert_stage_data(re->pair->one->path,
					o_tree, a_tree, b_tree, entries);
		else
			re->src_entry = item->util;

		item = string_list_lookup(entries, re->pair->two->path);
		if (!item)
			re->dst_entry = insert_stage_data(re->pair->two->path,
					o_tree, a_tree, b_tree, entries);
		else
			re->dst_entry = item->util;
		item = string_list_insert(renames, pair->one->path);
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

static int remove_file(struct merge_options *o, int clean,
		       const char *path, int no_wd)
{
	int update_cache = o->call_depth || clean;
	int update_working_directory = !o->call_depth && !no_wd;

	if (update_cache) {
		if (remove_file_from_cache(path))
			return -1;
	}
	if (update_working_directory) {
		if (remove_path(path))
			return -1;
	}
	return 0;
}

static char *unique_path(struct merge_options *o, const char *path, const char *branch)
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
	while (string_list_has_string(&o->current_file_set, newpath) ||
	       string_list_has_string(&o->current_directory_set, newpath) ||
	       lstat(newpath, &st) == 0)
		sprintf(p, "_%d", suffix++);

	string_list_insert(&o->current_file_set, newpath);
	return newpath;
}

static void flush_buffer(int fd, const char *buf, unsigned long size)
{
	while (size > 0) {
		long ret = write_in_full(fd, buf, size);
		if (ret < 0) {
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die_errno("merge-recursive");
		} else if (!ret) {
			die("merge-recursive: disk full?");
		}
		size -= ret;
		buf += ret;
	}
}

static int would_lose_untracked(const char *path)
{
	int pos = cache_name_pos(path, strlen(path));

	if (pos < 0)
		pos = -1 - pos;
	while (pos < active_nr &&
	       !strcmp(path, active_cache[pos]->name)) {
		/*
		 * If stage #0, it is definitely tracked.
		 * If it has stage #2 then it was tracked
		 * before this merge started.  All other
		 * cases the path was not tracked.
		 */
		switch (ce_stage(active_cache[pos])) {
		case 0:
		case 2:
			return 0;
		}
		pos++;
	}
	return file_exists(path);
}

static int make_room_for_path(const char *path)
{
	int status;
	const char *msg = "failed to create path '%s'%s";

	status = safe_create_leading_directories_const(path);
	if (status) {
		if (status == -3) {
			/* something else exists */
			error(msg, path, ": perhaps a D/F conflict?");
			return -1;
		}
		die(msg, path, "");
	}

	/*
	 * Do not unlink a file in the work tree if we are not
	 * tracking it.
	 */
	if (would_lose_untracked(path))
		return error("refusing to lose untracked file at '%s'",
			     path);

	/* Successful unlink is good.. */
	if (!unlink(path))
		return 0;
	/* .. and so is no existing file */
	if (errno == ENOENT)
		return 0;
	/* .. but not some other error (who really cares what?) */
	return error(msg, path, ": perhaps a D/F conflict?");
}

static void update_file_flags(struct merge_options *o,
			      const unsigned char *sha,
			      unsigned mode,
			      const char *path,
			      int update_cache,
			      int update_wd)
{
	if (o->call_depth)
		update_wd = 0;

	if (update_wd) {
		enum object_type type;
		void *buf;
		unsigned long size;

		if (S_ISGITLINK(mode))
			/*
			 * We may later decide to recursively descend into
			 * the submodule directory and update its index
			 * and/or work tree, but we do not do that now.
			 */
			goto update_index;

		buf = read_sha1_file(sha, &type, &size);
		if (!buf)
			die("cannot read object %s '%s'", sha1_to_hex(sha), path);
		if (type != OBJ_BLOB)
			die("blob expected for %s '%s'", sha1_to_hex(sha), path);
		if (S_ISREG(mode)) {
			struct strbuf strbuf = STRBUF_INIT;
			if (convert_to_working_tree(path, buf, size, &strbuf)) {
				free(buf);
				size = strbuf.len;
				buf = strbuf_detach(&strbuf, NULL);
			}
		}

		if (make_room_for_path(path) < 0) {
			update_wd = 0;
			free(buf);
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
				die_errno("failed to open '%s'", path);
			flush_buffer(fd, buf, size);
			close(fd);
		} else if (S_ISLNK(mode)) {
			char *lnk = xmemdupz(buf, size);
			safe_create_leading_directories_const(path);
			unlink(path);
			if (symlink(lnk, path))
				die_errno("failed to symlink '%s'", path);
			free(lnk);
		} else
			die("do not know what to do with %06o %s '%s'",
			    mode, sha1_to_hex(sha), path);
		free(buf);
	}
 update_index:
	if (update_cache)
		add_cacheinfo(mode, sha, path, 0, update_wd, ADD_CACHE_OK_TO_ADD);
}

static void update_file(struct merge_options *o,
			int clean,
			const unsigned char *sha,
			unsigned mode,
			const char *path)
{
	update_file_flags(o, sha, mode, path, o->call_depth || clean, !o->call_depth);
}

/* Low level file merging, update and removal */

struct merge_file_info
{
	unsigned char sha[20];
	unsigned mode;
	unsigned clean:1,
		 merge:1;
};

static int merge_3way(struct merge_options *o,
		      mmbuffer_t *result_buf,
		      struct diff_filespec *one,
		      struct diff_filespec *a,
		      struct diff_filespec *b,
		      const char *branch1,
		      const char *branch2)
{
	mmfile_t orig, src1, src2;
	char *base_name, *name1, *name2;
	int merge_status;
	int favor;

	if (o->call_depth)
		favor = 0;
	else {
		switch (o->recursive_variant) {
		case MERGE_RECURSIVE_OURS:
			favor = XDL_MERGE_FAVOR_OURS;
			break;
		case MERGE_RECURSIVE_THEIRS:
			favor = XDL_MERGE_FAVOR_THEIRS;
			break;
		default:
			favor = 0;
			break;
		}
	}

	if (strcmp(a->path, b->path) ||
	    (o->ancestor != NULL && strcmp(a->path, one->path) != 0)) {
		base_name = o->ancestor == NULL ? NULL :
			xstrdup(mkpath("%s:%s", o->ancestor, one->path));
		name1 = xstrdup(mkpath("%s:%s", branch1, a->path));
		name2 = xstrdup(mkpath("%s:%s", branch2, b->path));
	} else {
		base_name = o->ancestor == NULL ? NULL :
			xstrdup(mkpath("%s", o->ancestor));
		name1 = xstrdup(mkpath("%s", branch1));
		name2 = xstrdup(mkpath("%s", branch2));
	}

	read_mmblob(&orig, one->sha1);
	read_mmblob(&src1, a->sha1);
	read_mmblob(&src2, b->sha1);

	merge_status = ll_merge(result_buf, a->path, &orig, base_name,
				&src1, name1, &src2, name2,
				(!!o->call_depth) | (favor << 1));

	free(name1);
	free(name2);
	free(orig.ptr);
	free(src1.ptr);
	free(src2.ptr);
	return merge_status;
}

static struct merge_file_info merge_file(struct merge_options *o,
				         struct diff_filespec *one,
					 struct diff_filespec *a,
					 struct diff_filespec *b,
					 const char *branch1,
					 const char *branch2)
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
		if (!sha_eq(a->sha1, one->sha1) && !sha_eq(b->sha1, one->sha1))
			result.merge = 1;

		/*
		 * Merge modes
		 */
		if (a->mode == b->mode || a->mode == one->mode)
			result.mode = b->mode;
		else {
			result.mode = a->mode;
			if (b->mode != one->mode) {
				result.clean = 0;
				result.merge = 1;
			}
		}

		if (sha_eq(a->sha1, b->sha1) || sha_eq(a->sha1, one->sha1))
			hashcpy(result.sha, b->sha1);
		else if (sha_eq(b->sha1, one->sha1))
			hashcpy(result.sha, a->sha1);
		else if (S_ISREG(a->mode)) {
			mmbuffer_t result_buf;
			int merge_status;

			merge_status = merge_3way(o, &result_buf, one, a, b,
						  branch1, branch2);

			if ((merge_status < 0) || !result_buf.ptr)
				die("Failed to execute internal merge");

			if (write_sha1_file(result_buf.ptr, result_buf.size,
					    blob_type, result.sha))
				die("Unable to add %s to database",
				    a->path);

			free(result_buf.ptr);
			result.clean = (merge_status == 0);
		} else if (S_ISGITLINK(a->mode)) {
			result.clean = 0;
			hashcpy(result.sha, a->sha1);
		} else if (S_ISLNK(a->mode)) {
			hashcpy(result.sha, a->sha1);

			if (!sha_eq(a->sha1, b->sha1))
				result.clean = 0;
		} else {
			die("unsupported object type in the tree");
		}
	}

	return result;
}

static void conflict_rename_rename(struct merge_options *o,
				   struct rename *ren1,
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
	if (string_list_has_string(&o->current_directory_set, ren1_dst)) {
		dst_name1 = del[delp++] = unique_path(o, ren1_dst, branch1);
		output(o, 1, "%s is a directory in %s adding as %s instead",
		       ren1_dst, branch2, dst_name1);
		remove_file(o, 0, ren1_dst, 0);
	}
	if (string_list_has_string(&o->current_directory_set, ren2_dst)) {
		dst_name2 = del[delp++] = unique_path(o, ren2_dst, branch2);
		output(o, 1, "%s is a directory in %s adding as %s instead",
		       ren2_dst, branch1, dst_name2);
		remove_file(o, 0, ren2_dst, 0);
	}
	if (o->call_depth) {
		remove_file_from_cache(dst_name1);
		remove_file_from_cache(dst_name2);
		/*
		 * Uncomment to leave the conflicting names in the resulting tree
		 *
		 * update_file(o, 0, ren1->pair->two->sha1, ren1->pair->two->mode, dst_name1);
		 * update_file(o, 0, ren2->pair->two->sha1, ren2->pair->two->mode, dst_name2);
		 */
	} else {
		update_stages(dst_name1, NULL, ren1->pair->two, NULL, 1);
		update_stages(dst_name2, NULL, NULL, ren2->pair->two, 1);
	}
	while (delp--)
		free(del[delp]);
}

static void conflict_rename_dir(struct merge_options *o,
				struct rename *ren1,
				const char *branch1)
{
	char *new_path = unique_path(o, ren1->pair->two->path, branch1);
	output(o, 1, "Renaming %s to %s instead", ren1->pair->one->path, new_path);
	remove_file(o, 0, ren1->pair->two->path, 0);
	update_file(o, 0, ren1->pair->two->sha1, ren1->pair->two->mode, new_path);
	free(new_path);
}

static void conflict_rename_rename_2(struct merge_options *o,
				     struct rename *ren1,
				     const char *branch1,
				     struct rename *ren2,
				     const char *branch2)
{
	char *new_path1 = unique_path(o, ren1->pair->two->path, branch1);
	char *new_path2 = unique_path(o, ren2->pair->two->path, branch2);
	output(o, 1, "Renaming %s to %s and %s to %s instead",
	       ren1->pair->one->path, new_path1,
	       ren2->pair->one->path, new_path2);
	remove_file(o, 0, ren1->pair->two->path, 0);
	update_file(o, 0, ren1->pair->two->sha1, ren1->pair->two->mode, new_path1);
	update_file(o, 0, ren2->pair->two->sha1, ren2->pair->two->mode, new_path2);
	free(new_path2);
	free(new_path1);
}

static int process_renames(struct merge_options *o,
			   struct string_list *a_renames,
			   struct string_list *b_renames)
{
	int clean_merge = 1, i, j;
	struct string_list a_by_dst = {NULL, 0, 0, 0}, b_by_dst = {NULL, 0, 0, 0};
	const struct rename *sre;

	for (i = 0; i < a_renames->nr; i++) {
		sre = a_renames->items[i].util;
		string_list_insert(&a_by_dst, sre->pair->two->path)->util
			= sre->dst_entry;
	}
	for (i = 0; i < b_renames->nr; i++) {
		sre = b_renames->items[i].util;
		string_list_insert(&b_by_dst, sre->pair->two->path)->util
			= sre->dst_entry;
	}

	for (i = 0, j = 0; i < a_renames->nr || j < b_renames->nr;) {
		char *src;
		struct string_list *renames1, *renames2Dst;
		struct rename *ren1 = NULL, *ren2 = NULL;
		const char *branch1, *branch2;
		const char *ren1_src, *ren1_dst;

		if (i >= a_renames->nr) {
			ren2 = b_renames->items[j++].util;
		} else if (j >= b_renames->nr) {
			ren1 = a_renames->items[i++].util;
		} else {
			int compare = strcmp(a_renames->items[i].string,
					     b_renames->items[j].string);
			if (compare <= 0)
				ren1 = a_renames->items[i++].util;
			if (compare >= 0)
				ren2 = b_renames->items[j++].util;
		}

		/* TODO: refactor, so that 1/2 are not needed */
		if (ren1) {
			renames1 = a_renames;
			renames2Dst = &b_by_dst;
			branch1 = o->branch1;
			branch2 = o->branch2;
		} else {
			struct rename *tmp;
			renames1 = b_renames;
			renames2Dst = &a_by_dst;
			branch1 = o->branch2;
			branch2 = o->branch1;
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
				output(o, 1, "CONFLICT (rename/rename): "
				       "Rename \"%s\"->\"%s\" in branch \"%s\" "
				       "rename \"%s\"->\"%s\" in \"%s\"%s",
				       src, ren1_dst, branch1,
				       src, ren2_dst, branch2,
				       o->call_depth ? " (left unresolved)": "");
				if (o->call_depth) {
					remove_file_from_cache(src);
					update_file(o, 0, ren1->pair->one->sha1,
						    ren1->pair->one->mode, src);
				}
				conflict_rename_rename(o, ren1, branch1, ren2, branch2);
			} else {
				struct merge_file_info mfi;
				remove_file(o, 1, ren1_src, 1);
				mfi = merge_file(o,
						 ren1->pair->one,
						 ren1->pair->two,
						 ren2->pair->two,
						 branch1,
						 branch2);
				if (mfi.merge || !mfi.clean)
					output(o, 1, "Renaming %s->%s", src, ren1_dst);

				if (mfi.merge)
					output(o, 2, "Auto-merging %s", ren1_dst);

				if (!mfi.clean) {
					output(o, 1, "CONFLICT (content): merge conflict in %s",
					       ren1_dst);
					clean_merge = 0;

					if (!o->call_depth)
						update_stages(ren1_dst,
							      ren1->pair->one,
							      ren1->pair->two,
							      ren2->pair->two,
							      1 /* clear */);
				}
				update_file(o, mfi.clean, mfi.sha, mfi.mode, ren1_dst);
			}
		} else {
			/* Renamed in 1, maybe changed in 2 */
			struct string_list_item *item;
			/* we only use sha1 and mode of these */
			struct diff_filespec src_other, dst_other;
			int try_merge, stage = a_renames == renames1 ? 3: 2;

			remove_file(o, 1, ren1_src, o->call_depth || stage == 3);

			hashcpy(src_other.sha1, ren1->src_entry->stages[stage].sha);
			src_other.mode = ren1->src_entry->stages[stage].mode;
			hashcpy(dst_other.sha1, ren1->dst_entry->stages[stage].sha);
			dst_other.mode = ren1->dst_entry->stages[stage].mode;

			try_merge = 0;

			if (string_list_has_string(&o->current_directory_set, ren1_dst)) {
				clean_merge = 0;
				output(o, 1, "CONFLICT (rename/directory): Rename %s->%s in %s "
				       " directory %s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				conflict_rename_dir(o, ren1, branch1);
			} else if (sha_eq(src_other.sha1, null_sha1)) {
				clean_merge = 0;
				output(o, 1, "CONFLICT (rename/delete): Rename %s->%s in %s "
				       "and deleted in %s",
				       ren1_src, ren1_dst, branch1,
				       branch2);
				update_file(o, 0, ren1->pair->two->sha1, ren1->pair->two->mode, ren1_dst);
				if (!o->call_depth)
					update_stages(ren1_dst, NULL,
							branch1 == o->branch1 ?
							ren1->pair->two : NULL,
							branch1 == o->branch1 ?
							NULL : ren1->pair->two, 1);
			} else if (!sha_eq(dst_other.sha1, null_sha1)) {
				const char *new_path;
				clean_merge = 0;
				try_merge = 1;
				output(o, 1, "CONFLICT (rename/add): Rename %s->%s in %s. "
				       "%s added in %s",
				       ren1_src, ren1_dst, branch1,
				       ren1_dst, branch2);
				if (o->call_depth) {
					struct merge_file_info mfi;
					struct diff_filespec one, a, b;

					one.path = a.path = b.path =
						(char *)ren1_dst;
					hashcpy(one.sha1, null_sha1);
					one.mode = 0;
					hashcpy(a.sha1, ren1->pair->two->sha1);
					a.mode = ren1->pair->two->mode;
					hashcpy(b.sha1, dst_other.sha1);
					b.mode = dst_other.mode;
					mfi = merge_file(o, &one, &a, &b,
							 branch1,
							 branch2);
					output(o, 1, "Adding merged %s", ren1_dst);
					update_file(o, 0,
						    mfi.sha,
						    mfi.mode,
						    ren1_dst);
				} else {
					new_path = unique_path(o, ren1_dst, branch2);
					output(o, 1, "Adding as %s instead", new_path);
					update_file(o, 0, dst_other.sha1, dst_other.mode, new_path);
				}
			} else if ((item = string_list_lookup(renames2Dst, ren1_dst))) {
				ren2 = item->util;
				clean_merge = 0;
				ren2->processed = 1;
				output(o, 1, "CONFLICT (rename/rename): "
				       "Rename %s->%s in %s. "
				       "Rename %s->%s in %s",
				       ren1_src, ren1_dst, branch1,
				       ren2->pair->one->path, ren2->pair->two->path, branch2);
				conflict_rename_rename_2(o, ren1, branch1, ren2, branch2);
			} else
				try_merge = 1;

			if (try_merge) {
				struct diff_filespec *one, *a, *b;
				struct merge_file_info mfi;
				src_other.path = (char *)ren1_src;

				one = ren1->pair->one;
				if (a_renames == renames1) {
					a = ren1->pair->two;
					b = &src_other;
				} else {
					b = ren1->pair->two;
					a = &src_other;
				}
				mfi = merge_file(o, one, a, b,
						o->branch1, o->branch2);

				if (mfi.clean &&
				    sha_eq(mfi.sha, ren1->pair->two->sha1) &&
				    mfi.mode == ren1->pair->two->mode)
					/*
					 * This messaged is part of
					 * t6022 test. If you change
					 * it update the test too.
					 */
					output(o, 3, "Skipped %s (merged same as existing)", ren1_dst);
				else {
					if (mfi.merge || !mfi.clean)
						output(o, 1, "Renaming %s => %s", ren1_src, ren1_dst);
					if (mfi.merge)
						output(o, 2, "Auto-merging %s", ren1_dst);
					if (!mfi.clean) {
						output(o, 1, "CONFLICT (rename/modify): Merge conflict in %s",
						       ren1_dst);
						clean_merge = 0;

						if (!o->call_depth)
							update_stages(ren1_dst,
								      one, a, b, 1);
					}
					update_file(o, mfi.clean, mfi.sha, mfi.mode, ren1_dst);
				}
			}
		}
	}
	string_list_clear(&a_by_dst, 0);
	string_list_clear(&b_by_dst, 0);

	return clean_merge;
}

static unsigned char *stage_sha(const unsigned char *sha, unsigned mode)
{
	return (is_null_sha1(sha) || mode == 0) ? NULL: (unsigned char *)sha;
}

/* Per entry merge function */
static int process_entry(struct merge_options *o,
			 const char *path, struct stage_data *entry)
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
				output(o, 2, "Removing %s", path);
			/* do not touch working file if it did not exist */
			remove_file(o, 1, path, !a_sha);
		} else {
			/* Deleted in one and changed in the other */
			clean_merge = 0;
			if (!a_sha) {
				output(o, 1, "CONFLICT (delete/modify): %s deleted in %s "
				       "and modified in %s. Version %s of %s left in tree.",
				       path, o->branch1,
				       o->branch2, o->branch2, path);
				update_file(o, 0, b_sha, b_mode, path);
			} else {
				output(o, 1, "CONFLICT (delete/modify): %s deleted in %s "
				       "and modified in %s. Version %s of %s left in tree.",
				       path, o->branch2,
				       o->branch1, o->branch1, path);
				update_file(o, 0, a_sha, a_mode, path);
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
			add_branch = o->branch1;
			other_branch = o->branch2;
			mode = a_mode;
			sha = a_sha;
			conf = "file/directory";
		} else {
			add_branch = o->branch2;
			other_branch = o->branch1;
			mode = b_mode;
			sha = b_sha;
			conf = "directory/file";
		}
		if (string_list_has_string(&o->current_directory_set, path)) {
			const char *new_path = unique_path(o, path, add_branch);
			clean_merge = 0;
			output(o, 1, "CONFLICT (%s): There is a directory with name %s in %s. "
			       "Adding %s as %s",
			       conf, path, other_branch, path, new_path);
			remove_file(o, 0, path, 0);
			update_file(o, 0, sha, mode, new_path);
		} else {
			output(o, 2, "Adding %s", path);
			update_file(o, 1, sha, mode, path);
		}
	} else if (a_sha && b_sha) {
		/* Case C: Added in both (check for same permissions) and */
		/* case D: Modified in both, but differently. */
		const char *reason = "content";
		struct merge_file_info mfi;
		struct diff_filespec one, a, b;

		if (!o_sha) {
			reason = "add/add";
			o_sha = (unsigned char *)null_sha1;
		}
		output(o, 2, "Auto-merging %s", path);
		one.path = a.path = b.path = (char *)path;
		hashcpy(one.sha1, o_sha);
		one.mode = o_mode;
		hashcpy(a.sha1, a_sha);
		a.mode = a_mode;
		hashcpy(b.sha1, b_sha);
		b.mode = b_mode;

		mfi = merge_file(o, &one, &a, &b,
				 o->branch1, o->branch2);

		clean_merge = mfi.clean;
		if (!mfi.clean) {
			if (S_ISGITLINK(mfi.mode))
				reason = "submodule";
			output(o, 1, "CONFLICT (%s): Merge conflict in %s",
					reason, path);
		}
		update_file(o, mfi.clean, mfi.sha, mfi.mode, path);
	} else if (!o_sha && !a_sha && !b_sha) {
		/*
		 * this entry was deleted altogether. a_mode == 0 means
		 * we had that path and want to actively remove it.
		 */
		remove_file(o, 1, path, !a_mode);
	} else
		die("Fatal merge failure, shouldn't happen.");

	return clean_merge;
}

struct unpack_trees_error_msgs get_porcelain_error_msgs(void)
{
	struct unpack_trees_error_msgs msgs = {
		/* would_overwrite */
		"Your local changes to '%s' would be overwritten by merge.  Aborting.",
		/* not_uptodate_file */
		"Your local changes to '%s' would be overwritten by merge.  Aborting.",
		/* not_uptodate_dir */
		"Updating '%s' would lose untracked files in it.  Aborting.",
		/* would_lose_untracked */
		"Untracked working tree file '%s' would be %s by merge.  Aborting",
		/* bind_overlap -- will not happen here */
		NULL,
	};
	if (advice_commit_before_merge) {
		msgs.would_overwrite = msgs.not_uptodate_file =
			"Your local changes to '%s' would be overwritten by merge.  Aborting.\n"
			"Please, commit your changes or stash them before you can merge.";
	}
	return msgs;
}

int merge_trees(struct merge_options *o,
		struct tree *head,
		struct tree *merge,
		struct tree *common,
		struct tree **result)
{
	int code, clean;

	if (o->subtree_shift) {
		merge = shift_tree_object(head, merge, o->subtree_shift);
		common = shift_tree_object(head, common, o->subtree_shift);
	}

	if (sha_eq(common->object.sha1, merge->object.sha1)) {
		output(o, 0, "Already up-to-date!");
		*result = head;
		return 1;
	}

	code = git_merge_trees(o->call_depth, common, head, merge);

	if (code != 0) {
		if (show(o, 4) || o->call_depth)
			die("merging of trees %s and %s failed",
			    sha1_to_hex(head->object.sha1),
			    sha1_to_hex(merge->object.sha1));
		else
			exit(128);
	}

	if (unmerged_cache()) {
		struct string_list *entries, *re_head, *re_merge;
		int i;
		string_list_clear(&o->current_file_set, 1);
		string_list_clear(&o->current_directory_set, 1);
		get_files_dirs(o, head);
		get_files_dirs(o, merge);

		entries = get_unmerged();
		re_head  = get_renames(o, head, common, head, merge, entries);
		re_merge = get_renames(o, merge, common, head, merge, entries);
		clean = process_renames(o, re_head, re_merge);
		for (i = 0; i < entries->nr; i++) {
			const char *path = entries->items[i].string;
			struct stage_data *e = entries->items[i].util;
			if (!e->processed
				&& !process_entry(o, path, e))
				clean = 0;
		}

		string_list_clear(re_merge, 0);
		string_list_clear(re_head, 0);
		string_list_clear(entries, 1);

	}
	else
		clean = 1;

	if (o->call_depth)
		*result = write_tree_from_memory(o);

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
int merge_recursive(struct merge_options *o,
		    struct commit *h1,
		    struct commit *h2,
		    struct commit_list *ca,
		    struct commit **result)
{
	struct commit_list *iter;
	struct commit *merged_common_ancestors;
	struct tree *mrtree = mrtree;
	int clean;

	if (show(o, 4)) {
		output(o, 4, "Merging:");
		output_commit_title(o, h1);
		output_commit_title(o, h2);
	}

	if (!ca) {
		ca = get_merge_bases(h1, h2, 1);
		ca = reverse_commit_list(ca);
	}

	if (show(o, 5)) {
		output(o, 5, "found %u common ancestor(s):", commit_list_count(ca));
		for (iter = ca; iter; iter = iter->next)
			output_commit_title(o, iter->item);
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
		const char *saved_b1, *saved_b2;
		o->call_depth++;
		/*
		 * When the merge fails, the result contains files
		 * with conflict markers. The cleanness flag is
		 * ignored, it was never actually used, as result of
		 * merge_trees has always overwritten it: the committed
		 * "conflicts" were already resolved.
		 */
		discard_cache();
		saved_b1 = o->branch1;
		saved_b2 = o->branch2;
		o->branch1 = "Temporary merge branch 1";
		o->branch2 = "Temporary merge branch 2";
		merge_recursive(o, merged_common_ancestors, iter->item,
				NULL, &merged_common_ancestors);
		o->branch1 = saved_b1;
		o->branch2 = saved_b2;
		o->call_depth--;

		if (!merged_common_ancestors)
			die("merge returned no commit");
	}

	discard_cache();
	if (!o->call_depth)
		read_cache();

	o->ancestor = "merged common ancestors";
	clean = merge_trees(o, h1->tree, h2->tree, merged_common_ancestors->tree,
			    &mrtree);

	if (o->call_depth) {
		*result = make_virtual_commit(mrtree, "merged tree");
		commit_list_insert(h1, &(*result)->parents);
		commit_list_insert(h2, &(*result)->parents->next);
	}
	flush_output(o);
	return clean;
}

static struct commit *get_ref(const unsigned char *sha1, const char *name)
{
	struct object *object;

	object = deref_tag(parse_object(sha1), name, strlen(name));
	if (!object)
		return NULL;
	if (object->type == OBJ_TREE)
		return make_virtual_commit((struct tree*)object, name);
	if (object->type != OBJ_COMMIT)
		return NULL;
	if (parse_commit((struct commit *)object))
		return NULL;
	return (struct commit *)object;
}

int merge_recursive_generic(struct merge_options *o,
			    const unsigned char *head,
			    const unsigned char *merge,
			    int num_base_list,
			    const unsigned char **base_list,
			    struct commit **result)
{
	int clean, index_fd;
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	struct commit *head_commit = get_ref(head, o->branch1);
	struct commit *next_commit = get_ref(merge, o->branch2);
	struct commit_list *ca = NULL;

	if (base_list) {
		int i;
		for (i = 0; i < num_base_list; ++i) {
			struct commit *base;
			if (!(base = get_ref(base_list[i], sha1_to_hex(base_list[i]))))
				return error("Could not parse object '%s'",
					sha1_to_hex(base_list[i]));
			commit_list_insert(base, &ca);
		}
	}

	index_fd = hold_locked_index(lock, 1);
	clean = merge_recursive(o, head_commit, next_commit, ca,
			result);
	if (active_cache_changed &&
			(write_cache(index_fd, active_cache, active_nr) ||
			 commit_locked_index(lock)))
		return error("Unable to write index.");

	return clean ? 0 : 1;
}

static int merge_recursive_config(const char *var, const char *value, void *cb)
{
	struct merge_options *o = cb;
	if (!strcasecmp(var, "merge.verbosity")) {
		o->verbosity = git_config_int(var, value);
		return 0;
	}
	if (!strcasecmp(var, "diff.renamelimit")) {
		o->diff_rename_limit = git_config_int(var, value);
		return 0;
	}
	if (!strcasecmp(var, "merge.renamelimit")) {
		o->merge_rename_limit = git_config_int(var, value);
		return 0;
	}
	return git_xmerge_config(var, value, cb);
}

void init_merge_options(struct merge_options *o)
{
	memset(o, 0, sizeof(struct merge_options));
	o->verbosity = 2;
	o->buffer_output = 1;
	o->diff_rename_limit = -1;
	o->merge_rename_limit = -1;
	git_config(merge_recursive_config, o);
	if (getenv("GIT_MERGE_VERBOSITY"))
		o->verbosity =
			strtol(getenv("GIT_MERGE_VERBOSITY"), NULL, 10);
	if (o->verbosity >= 5)
		o->buffer_output = 0;
	strbuf_init(&o->obuf, 0);
	memset(&o->current_file_set, 0, sizeof(struct string_list));
	o->current_file_set.strdup_strings = 1;
	memset(&o->current_directory_set, 0, sizeof(struct string_list));
	o->current_directory_set.strdup_strings = 1;
}
