#define NO_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "dir.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "progress.h"
#include "refs.h"
#include "attr.h"

/*
 * Error messages expected by scripts out of plumbing commands such as
 * read-tree.  Non-scripted Porcelain is not required to use these messages
 * and in fact are encouraged to reword them to better suit their particular
 * situation better.  See how "git checkout" replaces not_uptodate_file to
 * explain why it does not allow switching between branches when you have
 * local changes, for example.
 */
static struct unpack_trees_error_msgs unpack_plumbing_errors = {
	/* would_overwrite */
	"Entry '%s' would be overwritten by merge. Cannot merge.",

	/* not_uptodate_file */
	"Entry '%s' not uptodate. Cannot merge.",

	/* not_uptodate_dir */
	"Updating '%s' would lose untracked files in it",

	/* would_lose_untracked */
	"Untracked working tree file '%s' would be %s by merge.",

	/* bind_overlap */
	"Entry '%s' overlaps with '%s'.  Cannot bind.",
};

#define ERRORMSG(o,fld) \
	( ((o) && (o)->msgs.fld) \
	? ((o)->msgs.fld) \
	: (unpack_plumbing_errors.fld) )

static void add_entry(struct unpack_trees_options *o, struct cache_entry *ce,
	unsigned int set, unsigned int clear)
{
	unsigned int size = ce_size(ce);
	struct cache_entry *new = xmalloc(size);

	clear |= CE_HASHED | CE_UNHASHED;

	memcpy(new, ce, size);
	new->next = NULL;
	new->ce_flags = (new->ce_flags & ~clear) | set;
	add_index_entry(&o->result, new, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
}

/*
 * Unlink the last component and schedule the leading directories for
 * removal, such that empty directories get removed.
 */
static void unlink_entry(struct cache_entry *ce)
{
	if (has_symlink_or_noent_leading_path(ce->name, ce_namelen(ce)))
		return;
	if (unlink(ce->name))
		return;
	schedule_dir_for_removal(ce->name, ce_namelen(ce));
}

static struct checkout state;
static int check_updates(struct unpack_trees_options *o)
{
	unsigned cnt = 0, total = 0;
	struct progress *progress = NULL;
	struct index_state *index = &o->result;
	int i;
	int errs = 0;

	if (o->update && o->verbose_update) {
		for (total = cnt = 0; cnt < index->cache_nr; cnt++) {
			struct cache_entry *ce = index->cache[cnt];
			if (ce->ce_flags & (CE_UPDATE | CE_REMOVE))
				total++;
		}

		progress = start_progress_delay("Checking out files",
						total, 50, 1);
		cnt = 0;
	}

	if (o->update)
		git_attr_set_direction(GIT_ATTR_CHECKOUT, &o->result);
	for (i = 0; i < index->cache_nr; i++) {
		struct cache_entry *ce = index->cache[i];

		if (ce->ce_flags & CE_REMOVE) {
			display_progress(progress, ++cnt);
			if (o->update)
				unlink_entry(ce);
		}
	}
	remove_marked_cache_entries(&o->result);
	remove_scheduled_dirs();

	for (i = 0; i < index->cache_nr; i++) {
		struct cache_entry *ce = index->cache[i];

		if (ce->ce_flags & CE_UPDATE) {
			display_progress(progress, ++cnt);
			ce->ce_flags &= ~CE_UPDATE;
			if (o->update) {
				errs |= checkout_entry(ce, &state, NULL);
			}
		}
	}
	stop_progress(&progress);
	if (o->update)
		git_attr_set_direction(GIT_ATTR_CHECKIN, NULL);
	return errs != 0;
}

static inline int call_unpack_fn(struct cache_entry **src, struct unpack_trees_options *o)
{
	int ret = o->fn(src, o);
	if (ret > 0)
		ret = 0;
	return ret;
}

static int unpack_index_entry(struct cache_entry *ce, struct unpack_trees_options *o)
{
	struct cache_entry *src[5] = { ce, };

	o->pos++;
	if (ce_stage(ce)) {
		if (o->skip_unmerged) {
			add_entry(o, ce, 0, 0);
			return 0;
		}
	}
	return call_unpack_fn(src, o);
}

int traverse_trees_recursive(int n, unsigned long dirmask, unsigned long df_conflicts, struct name_entry *names, struct traverse_info *info)
{
	int i;
	struct tree_desc t[MAX_UNPACK_TREES];
	struct traverse_info newinfo;
	struct name_entry *p;

	p = names;
	while (!p->mode)
		p++;

	newinfo = *info;
	newinfo.prev = info;
	newinfo.name = *p;
	newinfo.pathlen += tree_entry_len(p->path, p->sha1) + 1;
	newinfo.conflicts |= df_conflicts;

	for (i = 0; i < n; i++, dirmask >>= 1) {
		const unsigned char *sha1 = NULL;
		if (dirmask & 1)
			sha1 = names[i].sha1;
		fill_tree_descriptor(t+i, sha1);
	}
	return traverse_trees(n, t, &newinfo);
}

/*
 * Compare the traverse-path to the cache entry without actually
 * having to generate the textual representation of the traverse
 * path.
 *
 * NOTE! This *only* compares up to the size of the traverse path
 * itself - the caller needs to do the final check for the cache
 * entry having more data at the end!
 */
static int do_compare_entry(const struct cache_entry *ce, const struct traverse_info *info, const struct name_entry *n)
{
	int len, pathlen, ce_len;
	const char *ce_name;

	if (info->prev) {
		int cmp = do_compare_entry(ce, info->prev, &info->name);
		if (cmp)
			return cmp;
	}
	pathlen = info->pathlen;
	ce_len = ce_namelen(ce);

	/* If ce_len < pathlen then we must have previously hit "name == directory" entry */
	if (ce_len < pathlen)
		return -1;

	ce_len -= pathlen;
	ce_name = ce->name + pathlen;

	len = tree_entry_len(n->path, n->sha1);
	return df_name_compare(ce_name, ce_len, S_IFREG, n->path, len, n->mode);
}

static int compare_entry(const struct cache_entry *ce, const struct traverse_info *info, const struct name_entry *n)
{
	int cmp = do_compare_entry(ce, info, n);
	if (cmp)
		return cmp;

	/*
	 * Even if the beginning compared identically, the ce should
	 * compare as bigger than a directory leading up to it!
	 */
	return ce_namelen(ce) > traverse_path_len(info, n);
}

static struct cache_entry *create_ce_entry(const struct traverse_info *info, const struct name_entry *n, int stage)
{
	int len = traverse_path_len(info, n);
	struct cache_entry *ce = xcalloc(1, cache_entry_size(len));

	ce->ce_mode = create_ce_mode(n->mode);
	ce->ce_flags = create_ce_flags(len, stage);
	hashcpy(ce->sha1, n->sha1);
	make_traverse_path(ce->name, info, n);

	return ce;
}

static int unpack_nondirectories(int n, unsigned long mask,
				 unsigned long dirmask,
				 struct cache_entry **src,
				 const struct name_entry *names,
				 const struct traverse_info *info)
{
	int i;
	struct unpack_trees_options *o = info->data;
	unsigned long conflicts;

	/* Do we have *only* directories? Nothing to do */
	if (mask == dirmask && !src[0])
		return 0;

	conflicts = info->conflicts;
	if (o->merge)
		conflicts >>= 1;
	conflicts |= dirmask;

	/*
	 * Ok, we've filled in up to any potential index entry in src[0],
	 * now do the rest.
	 */
	for (i = 0; i < n; i++) {
		int stage;
		unsigned int bit = 1ul << i;
		if (conflicts & bit) {
			src[i + o->merge] = o->df_conflict_entry;
			continue;
		}
		if (!(mask & bit))
			continue;
		if (!o->merge)
			stage = 0;
		else if (i + 1 < o->head_idx)
			stage = 1;
		else if (i + 1 > o->head_idx)
			stage = 3;
		else
			stage = 2;
		src[i + o->merge] = create_ce_entry(info, names + i, stage);
	}

	if (o->merge)
		return call_unpack_fn(src, o);

	for (i = 0; i < n; i++)
		if (src[i] && src[i] != o->df_conflict_entry)
			add_entry(o, src[i], 0, 0);
	return 0;
}

static int unpack_callback(int n, unsigned long mask, unsigned long dirmask, struct name_entry *names, struct traverse_info *info)
{
	struct cache_entry *src[MAX_UNPACK_TREES + 1] = { NULL, };
	struct unpack_trees_options *o = info->data;
	const struct name_entry *p = names;

	/* Find first entry with a real name (we could use "mask" too) */
	while (!p->mode)
		p++;

	/* Are we supposed to look at the index too? */
	if (o->merge) {
		while (o->pos < o->src_index->cache_nr) {
			struct cache_entry *ce = o->src_index->cache[o->pos];
			int cmp = compare_entry(ce, info, p);
			if (cmp < 0) {
				if (unpack_index_entry(ce, o) < 0)
					return -1;
				continue;
			}
			if (!cmp) {
				o->pos++;
				if (ce_stage(ce)) {
					/*
					 * If we skip unmerged index entries, we'll skip this
					 * entry *and* the tree entries associated with it!
					 */
					if (o->skip_unmerged) {
						add_entry(o, ce, 0, 0);
						return mask;
					}
				}
				src[0] = ce;
			}
			break;
		}
	}

	if (unpack_nondirectories(n, mask, dirmask, src, names, info) < 0)
		return -1;

	/* Now handle any directories.. */
	if (dirmask) {
		unsigned long conflicts = mask & ~dirmask;
		if (o->merge) {
			conflicts <<= 1;
			if (src[0])
				conflicts |= 1;
		}
		if (traverse_trees_recursive(n, dirmask, conflicts,
					     names, info) < 0)
			return -1;
		return mask;
	}

	return mask;
}

static int unpack_failed(struct unpack_trees_options *o, const char *message)
{
	discard_index(&o->result);
	if (!o->gently) {
		if (message)
			return error("%s", message);
		return -1;
	}
	return -1;
}

/*
 * N-way merge "len" trees.  Returns 0 on success, -1 on failure to manipulate the
 * resulting index, -2 on failure to reflect the changes to the work tree.
 */
int unpack_trees(unsigned len, struct tree_desc *t, struct unpack_trees_options *o)
{
	int ret;
	static struct cache_entry *dfc;

	if (len > MAX_UNPACK_TREES)
		die("unpack_trees takes at most %d trees", MAX_UNPACK_TREES);
	memset(&state, 0, sizeof(state));
	state.base_dir = "";
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;

	memset(&o->result, 0, sizeof(o->result));
	o->result.initialized = 1;
	if (o->src_index) {
		o->result.timestamp.sec = o->src_index->timestamp.sec;
		o->result.timestamp.nsec = o->src_index->timestamp.nsec;
	}
	o->merge_size = len;

	if (!dfc)
		dfc = xcalloc(1, cache_entry_size(0));
	o->df_conflict_entry = dfc;

	if (len) {
		const char *prefix = o->prefix ? o->prefix : "";
		struct traverse_info info;

		setup_traverse_info(&info, prefix);
		info.fn = unpack_callback;
		info.data = o;

		if (traverse_trees(len, t, &info) < 0)
			return unpack_failed(o, NULL);
	}

	/* Any left-over entries in the index? */
	if (o->merge) {
		while (o->pos < o->src_index->cache_nr) {
			struct cache_entry *ce = o->src_index->cache[o->pos];
			if (unpack_index_entry(ce, o) < 0)
				return unpack_failed(o, NULL);
		}
	}

	if (o->trivial_merges_only && o->nontrivial_merge)
		return unpack_failed(o, "Merge requires file-level merging");

	o->src_index = NULL;
	ret = check_updates(o) ? (-2) : 0;
	if (o->dst_index)
		*o->dst_index = o->result;
	return ret;
}

/* Here come the merge functions */

static int reject_merge(struct cache_entry *ce, struct unpack_trees_options *o)
{
	return error(ERRORMSG(o, would_overwrite), ce->name);
}

static int same(struct cache_entry *a, struct cache_entry *b)
{
	if (!!a != !!b)
		return 0;
	if (!a && !b)
		return 1;
	return a->ce_mode == b->ce_mode &&
	       !hashcmp(a->sha1, b->sha1);
}


/*
 * When a CE gets turned into an unmerged entry, we
 * want it to be up-to-date
 */
static int verify_uptodate(struct cache_entry *ce,
		struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset || ce_uptodate(ce))
		return 0;

	if (!lstat(ce->name, &st)) {
		unsigned changed = ie_match_stat(o->src_index, ce, &st, CE_MATCH_IGNORE_VALID);
		if (!changed)
			return 0;
		/*
		 * NEEDSWORK: the current default policy is to allow
		 * submodule to be out of sync wrt the supermodule
		 * index.  This needs to be tightened later for
		 * submodules that are marked to be automatically
		 * checked out.
		 */
		if (S_ISGITLINK(ce->ce_mode))
			return 0;
		errno = 0;
	}
	if (errno == ENOENT)
		return 0;
	return o->gently ? -1 :
		error(ERRORMSG(o, not_uptodate_file), ce->name);
}

static void invalidate_ce_path(struct cache_entry *ce, struct unpack_trees_options *o)
{
	if (ce)
		cache_tree_invalidate_path(o->src_index->cache_tree, ce->name);
}

/*
 * Check that checking out ce->sha1 in subdir ce->name is not
 * going to overwrite any working files.
 *
 * Currently, git does not checkout subprojects during a superproject
 * checkout, so it is not going to overwrite anything.
 */
static int verify_clean_submodule(struct cache_entry *ce, const char *action,
				      struct unpack_trees_options *o)
{
	return 0;
}

static int verify_clean_subdirectory(struct cache_entry *ce, const char *action,
				      struct unpack_trees_options *o)
{
	/*
	 * we are about to extract "ce->name"; we would not want to lose
	 * anything in the existing directory there.
	 */
	int namelen;
	int i;
	struct dir_struct d;
	char *pathbuf;
	int cnt = 0;
	unsigned char sha1[20];

	if (S_ISGITLINK(ce->ce_mode) &&
	    resolve_gitlink_ref(ce->name, "HEAD", sha1) == 0) {
		/* If we are not going to update the submodule, then
		 * we don't care.
		 */
		if (!hashcmp(sha1, ce->sha1))
			return 0;
		return verify_clean_submodule(ce, action, o);
	}

	/*
	 * First let's make sure we do not have a local modification
	 * in that directory.
	 */
	namelen = strlen(ce->name);
	for (i = o->pos; i < o->src_index->cache_nr; i++) {
		struct cache_entry *ce2 = o->src_index->cache[i];
		int len = ce_namelen(ce2);
		if (len < namelen ||
		    strncmp(ce->name, ce2->name, namelen) ||
		    ce2->name[namelen] != '/')
			break;
		/*
		 * ce2->name is an entry in the subdirectory.
		 */
		if (!ce_stage(ce2)) {
			if (verify_uptodate(ce2, o))
				return -1;
			add_entry(o, ce2, CE_REMOVE, 0);
		}
		cnt++;
	}

	/*
	 * Then we need to make sure that we do not lose a locally
	 * present file that is not ignored.
	 */
	pathbuf = xmalloc(namelen + 2);
	memcpy(pathbuf, ce->name, namelen);
	strcpy(pathbuf+namelen, "/");

	memset(&d, 0, sizeof(d));
	if (o->dir)
		d.exclude_per_dir = o->dir->exclude_per_dir;
	i = read_directory(&d, ce->name, pathbuf, namelen+1, NULL);
	if (i)
		return o->gently ? -1 :
			error(ERRORMSG(o, not_uptodate_dir), ce->name);
	free(pathbuf);
	return cnt;
}

/*
 * This gets called when there was no index entry for the tree entry 'dst',
 * but we found a file in the working tree that 'lstat()' said was fine,
 * and we're on a case-insensitive filesystem.
 *
 * See if we can find a case-insensitive match in the index that also
 * matches the stat information, and assume it's that other file!
 */
static int icase_exists(struct unpack_trees_options *o, struct cache_entry *dst, struct stat *st)
{
	struct cache_entry *src;

	src = index_name_exists(o->src_index, dst->name, ce_namelen(dst), 1);
	return src && !ie_match_stat(o->src_index, src, st, CE_MATCH_IGNORE_VALID);
}

/*
 * We do not want to remove or overwrite a working tree file that
 * is not tracked, unless it is ignored.
 */
static int verify_absent(struct cache_entry *ce, const char *action,
			 struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset || !o->update)
		return 0;

	if (has_symlink_or_noent_leading_path(ce->name, ce_namelen(ce)))
		return 0;

	if (!lstat(ce->name, &st)) {
		int ret;
		int dtype = ce_to_dtype(ce);
		struct cache_entry *result;

		/*
		 * It may be that the 'lstat()' succeeded even though
		 * target 'ce' was absent, because there is an old
		 * entry that is different only in case..
		 *
		 * Ignore that lstat() if it matches.
		 */
		if (ignore_case && icase_exists(o, ce, &st))
			return 0;

		if (o->dir && excluded(o->dir, ce->name, &dtype))
			/*
			 * ce->name is explicitly excluded, so it is Ok to
			 * overwrite it.
			 */
			return 0;
		if (S_ISDIR(st.st_mode)) {
			/*
			 * We are checking out path "foo" and
			 * found "foo/." in the working tree.
			 * This is tricky -- if we have modified
			 * files that are in "foo/" we would lose
			 * it.
			 */
			ret = verify_clean_subdirectory(ce, action, o);
			if (ret < 0)
				return ret;

			/*
			 * If this removed entries from the index,
			 * what that means is:
			 *
			 * (1) the caller unpack_callback() saw path/foo
			 * in the index, and it has not removed it because
			 * it thinks it is handling 'path' as blob with
			 * D/F conflict;
			 * (2) we will return "ok, we placed a merged entry
			 * in the index" which would cause o->pos to be
			 * incremented by one;
			 * (3) however, original o->pos now has 'path/foo'
			 * marked with "to be removed".
			 *
			 * We need to increment it by the number of
			 * deleted entries here.
			 */
			o->pos += ret;
			return 0;
		}

		/*
		 * The previous round may already have decided to
		 * delete this path, which is in a subdirectory that
		 * is being replaced with a blob.
		 */
		result = index_name_exists(&o->result, ce->name, ce_namelen(ce), 0);
		if (result) {
			if (result->ce_flags & CE_REMOVE)
				return 0;
		}

		return o->gently ? -1 :
			error(ERRORMSG(o, would_lose_untracked), ce->name, action);
	}
	return 0;
}

static int merged_entry(struct cache_entry *merge, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	int update = CE_UPDATE;

	if (old) {
		/*
		 * See if we can re-use the old CE directly?
		 * That way we get the uptodate stat info.
		 *
		 * This also removes the UPDATE flag on a match; otherwise
		 * we will end up overwriting local changes in the work tree.
		 */
		if (same(old, merge)) {
			copy_cache_entry(merge, old);
			update = 0;
		} else {
			if (verify_uptodate(old, o))
				return -1;
			invalidate_ce_path(old, o);
		}
	}
	else {
		if (verify_absent(merge, "overwritten", o))
			return -1;
		invalidate_ce_path(merge, o);
	}

	add_entry(o, merge, update, CE_STAGEMASK);
	return 1;
}

static int deleted_entry(struct cache_entry *ce, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	/* Did it exist in the index? */
	if (!old) {
		if (verify_absent(ce, "removed", o))
			return -1;
		return 0;
	}
	if (verify_uptodate(old, o))
		return -1;
	add_entry(o, ce, CE_REMOVE, 0);
	invalidate_ce_path(ce, o);
	return 1;
}

static int keep_entry(struct cache_entry *ce, struct unpack_trees_options *o)
{
	add_entry(o, ce, 0, 0);
	return 1;
}

#if DBRT_DEBUG
static void show_stage_entry(FILE *o,
			     const char *label, const struct cache_entry *ce)
{
	if (!ce)
		fprintf(o, "%s (missing)\n", label);
	else
		fprintf(o, "%s%06o %s %d\t%s\n",
			label,
			ce->ce_mode,
			sha1_to_hex(ce->sha1),
			ce_stage(ce),
			ce->name);
}
#endif

int threeway_merge(struct cache_entry **stages, struct unpack_trees_options *o)
{
	struct cache_entry *index;
	struct cache_entry *head;
	struct cache_entry *remote = stages[o->head_idx + 1];
	int count;
	int head_match = 0;
	int remote_match = 0;

	int df_conflict_head = 0;
	int df_conflict_remote = 0;

	int any_anc_missing = 0;
	int no_anc_exists = 1;
	int i;

	for (i = 1; i < o->head_idx; i++) {
		if (!stages[i] || stages[i] == o->df_conflict_entry)
			any_anc_missing = 1;
		else
			no_anc_exists = 0;
	}

	index = stages[0];
	head = stages[o->head_idx];

	if (head == o->df_conflict_entry) {
		df_conflict_head = 1;
		head = NULL;
	}

	if (remote == o->df_conflict_entry) {
		df_conflict_remote = 1;
		remote = NULL;
	}

	/* First, if there's a #16 situation, note that to prevent #13
	 * and #14.
	 */
	if (!same(remote, head)) {
		for (i = 1; i < o->head_idx; i++) {
			if (same(stages[i], head)) {
				head_match = i;
			}
			if (same(stages[i], remote)) {
				remote_match = i;
			}
		}
	}

	/* We start with cases where the index is allowed to match
	 * something other than the head: #14(ALT) and #2ALT, where it
	 * is permitted to match the result instead.
	 */
	/* #14, #14ALT, #2ALT */
	if (remote && !df_conflict_head && head_match && !remote_match) {
		if (index && !same(index, remote) && !same(index, head))
			return o->gently ? -1 : reject_merge(index, o);
		return merged_entry(remote, index, o);
	}
	/*
	 * If we have an entry in the index cache, then we want to
	 * make sure that it matches head.
	 */
	if (index && !same(index, head))
		return o->gently ? -1 : reject_merge(index, o);

	if (head) {
		/* #5ALT, #15 */
		if (same(head, remote))
			return merged_entry(head, index, o);
		/* #13, #3ALT */
		if (!df_conflict_remote && remote_match && !head_match)
			return merged_entry(head, index, o);
	}

	/* #1 */
	if (!head && !remote && any_anc_missing)
		return 0;

	/* Under the new "aggressive" rule, we resolve mostly trivial
	 * cases that we historically had git-merge-one-file resolve.
	 */
	if (o->aggressive) {
		int head_deleted = !head && !df_conflict_head;
		int remote_deleted = !remote && !df_conflict_remote;
		struct cache_entry *ce = NULL;

		if (index)
			ce = index;
		else if (head)
			ce = head;
		else if (remote)
			ce = remote;
		else {
			for (i = 1; i < o->head_idx; i++) {
				if (stages[i] && stages[i] != o->df_conflict_entry) {
					ce = stages[i];
					break;
				}
			}
		}

		/*
		 * Deleted in both.
		 * Deleted in one and unchanged in the other.
		 */
		if ((head_deleted && remote_deleted) ||
		    (head_deleted && remote && remote_match) ||
		    (remote_deleted && head && head_match)) {
			if (index)
				return deleted_entry(index, index, o);
			if (ce && !head_deleted) {
				if (verify_absent(ce, "removed", o))
					return -1;
			}
			return 0;
		}
		/*
		 * Added in both, identically.
		 */
		if (no_anc_exists && head && remote && same(head, remote))
			return merged_entry(head, index, o);

	}

	/* Below are "no merge" cases, which require that the index be
	 * up-to-date to avoid the files getting overwritten with
	 * conflict resolution files.
	 */
	if (index) {
		if (verify_uptodate(index, o))
			return -1;
	}

	o->nontrivial_merge = 1;

	/* #2, #3, #4, #6, #7, #9, #10, #11. */
	count = 0;
	if (!head_match || !remote_match) {
		for (i = 1; i < o->head_idx; i++) {
			if (stages[i] && stages[i] != o->df_conflict_entry) {
				keep_entry(stages[i], o);
				count++;
				break;
			}
		}
	}
#if DBRT_DEBUG
	else {
		fprintf(stderr, "read-tree: warning #16 detected\n");
		show_stage_entry(stderr, "head   ", stages[head_match]);
		show_stage_entry(stderr, "remote ", stages[remote_match]);
	}
#endif
	if (head) { count += keep_entry(head, o); }
	if (remote) { count += keep_entry(remote, o); }
	return count;
}

/*
 * Two-way merge.
 *
 * The rule is to "carry forward" what is in the index without losing
 * information across a "fast forward", favoring a successful merge
 * over a merge failure when it makes sense.  For details of the
 * "carry forward" rule, please see <Documentation/git-read-tree.txt>.
 *
 */
int twoway_merge(struct cache_entry **src, struct unpack_trees_options *o)
{
	struct cache_entry *current = src[0];
	struct cache_entry *oldtree = src[1];
	struct cache_entry *newtree = src[2];

	if (o->merge_size != 2)
		return error("Cannot do a twoway merge of %d trees",
			     o->merge_size);

	if (oldtree == o->df_conflict_entry)
		oldtree = NULL;
	if (newtree == o->df_conflict_entry)
		newtree = NULL;

	if (current) {
		if ((!oldtree && !newtree) || /* 4 and 5 */
		    (!oldtree && newtree &&
		     same(current, newtree)) || /* 6 and 7 */
		    (oldtree && newtree &&
		     same(oldtree, newtree)) || /* 14 and 15 */
		    (oldtree && newtree &&
		     !same(oldtree, newtree) && /* 18 and 19 */
		     same(current, newtree))) {
			return keep_entry(current, o);
		}
		else if (oldtree && !newtree && same(current, oldtree)) {
			/* 10 or 11 */
			return deleted_entry(oldtree, current, o);
		}
		else if (oldtree && newtree &&
			 same(current, oldtree) && !same(current, newtree)) {
			/* 20 or 21 */
			return merged_entry(newtree, current, o);
		}
		else {
			/* all other failures */
			if (oldtree)
				return o->gently ? -1 : reject_merge(oldtree, o);
			if (current)
				return o->gently ? -1 : reject_merge(current, o);
			if (newtree)
				return o->gently ? -1 : reject_merge(newtree, o);
			return -1;
		}
	}
	else if (newtree) {
		if (oldtree && !o->initial_checkout) {
			/*
			 * deletion of the path was staged;
			 */
			if (same(oldtree, newtree))
				return 1;
			return reject_merge(oldtree, o);
		}
		return merged_entry(newtree, current, o);
	}
	return deleted_entry(oldtree, current, o);
}

/*
 * Bind merge.
 *
 * Keep the index entries at stage0, collapse stage1 but make sure
 * stage0 does not have anything there.
 */
int bind_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (o->merge_size != 1)
		return error("Cannot do a bind merge of %d trees\n",
			     o->merge_size);
	if (a && old)
		return o->gently ? -1 :
			error(ERRORMSG(o, bind_overlap), a->name, old->name);
	if (!a)
		return keep_entry(old, o);
	else
		return merged_entry(a, NULL, o);
}

/*
 * One-way merge.
 *
 * The rule is:
 * - take the stat information from stage0, take the data from stage1
 */
int oneway_merge(struct cache_entry **src, struct unpack_trees_options *o)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (o->merge_size != 1)
		return error("Cannot do a oneway merge of %d trees",
			     o->merge_size);

	if (!a)
		return deleted_entry(old, old, o);

	if (old && same(old, a)) {
		int update = 0;
		if (o->reset) {
			struct stat st;
			if (lstat(old->name, &st) ||
			    ie_match_stat(o->src_index, old, &st, CE_MATCH_IGNORE_VALID))
				update |= CE_UPDATE;
		}
		add_entry(o, old, update, 0);
		return 0;
	}
	return merged_entry(a, old, o);
}
