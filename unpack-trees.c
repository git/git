#include "cache.h"
#include "dir.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "progress.h"

#define DBRT_DEBUG 1

struct tree_entry_list {
	struct tree_entry_list *next;
	unsigned directory : 1;
	unsigned executable : 1;
	unsigned symlink : 1;
	unsigned int mode;
	const char *name;
	const unsigned char *sha1;
};

static struct tree_entry_list *create_tree_entry_list(struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry one;
	struct tree_entry_list *ret = NULL;
	struct tree_entry_list **list_p = &ret;

	if (!tree->object.parsed)
		parse_tree(tree);

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &one)) {
		struct tree_entry_list *entry;

		entry = xmalloc(sizeof(struct tree_entry_list));
		entry->name = one.path;
		entry->sha1 = one.sha1;
		entry->mode = one.mode;
		entry->directory = S_ISDIR(one.mode) != 0;
		entry->executable = (one.mode & S_IXUSR) != 0;
		entry->symlink = S_ISLNK(one.mode) != 0;
		entry->next = NULL;

		*list_p = entry;
		list_p = &entry->next;
	}
	return ret;
}

static int entcmp(const char *name1, int dir1, const char *name2, int dir2)
{
	int len1 = strlen(name1);
	int len2 = strlen(name2);
	int len = len1 < len2 ? len1 : len2;
	int ret = memcmp(name1, name2, len);
	unsigned char c1, c2;
	if (ret)
		return ret;
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && dir1)
		c1 = '/';
	if (!c2 && dir2)
		c2 = '/';
	ret = (c1 < c2) ? -1 : (c1 > c2) ? 1 : 0;
	if (c1 && c2 && !ret)
		ret = len1 - len2;
	return ret;
}

static int unpack_trees_rec(struct tree_entry_list **posns, int len,
			    const char *base, struct unpack_trees_options *o,
			    struct tree_entry_list *df_conflict_list)
{
	int baselen = strlen(base);
	int src_size = len + 1;
	int i_stk = i_stk;
	int retval = 0;

	if (o->dir)
		i_stk = push_exclude_per_directory(o->dir, base, strlen(base));

	do {
		int i;
		const char *first;
		int firstdir = 0;
		int pathlen;
		unsigned ce_size;
		struct tree_entry_list **subposns;
		struct cache_entry **src;
		int any_files = 0;
		int any_dirs = 0;
		char *cache_name;
		int ce_stage;

		/* Find the first name in the input. */

		first = NULL;
		cache_name = NULL;

		/* Check the cache */
		if (o->merge && o->pos < active_nr) {
			/* This is a bit tricky: */
			/* If the index has a subdirectory (with
			 * contents) as the first name, it'll get a
			 * filename like "foo/bar". But that's after
			 * "foo", so the entry in trees will get
			 * handled first, at which point we'll go into
			 * "foo", and deal with "bar" from the index,
			 * because the base will be "foo/". The only
			 * way we can actually have "foo/bar" first of
			 * all the things is if the trees don't
			 * contain "foo" at all, in which case we'll
			 * handle "foo/bar" without going into the
			 * directory, but that's fine (and will return
			 * an error anyway, with the added unknown
			 * file case.
			 */

			cache_name = active_cache[o->pos]->name;
			if (strlen(cache_name) > baselen &&
			    !memcmp(cache_name, base, baselen)) {
				cache_name += baselen;
				first = cache_name;
			} else {
				cache_name = NULL;
			}
		}

#if DBRT_DEBUG > 1
		if (first)
			printf("index %s\n", first);
#endif
		for (i = 0; i < len; i++) {
			if (!posns[i] || posns[i] == df_conflict_list)
				continue;
#if DBRT_DEBUG > 1
			printf("%d %s\n", i + 1, posns[i]->name);
#endif
			if (!first || entcmp(first, firstdir,
					     posns[i]->name,
					     posns[i]->directory) > 0) {
				first = posns[i]->name;
				firstdir = posns[i]->directory;
			}
		}
		/* No name means we're done */
		if (!first)
			goto leave_directory;

		pathlen = strlen(first);
		ce_size = cache_entry_size(baselen + pathlen);

		src = xcalloc(src_size, sizeof(struct cache_entry *));

		subposns = xcalloc(len, sizeof(struct tree_list_entry *));

		if (cache_name && !strcmp(cache_name, first)) {
			any_files = 1;
			src[0] = active_cache[o->pos];
			remove_cache_entry_at(o->pos);
		}

		for (i = 0; i < len; i++) {
			struct cache_entry *ce;

			if (!posns[i] ||
			    (posns[i] != df_conflict_list &&
			     strcmp(first, posns[i]->name))) {
				continue;
			}

			if (posns[i] == df_conflict_list) {
				src[i + o->merge] = o->df_conflict_entry;
				continue;
			}

			if (posns[i]->directory) {
				struct tree *tree = lookup_tree(posns[i]->sha1);
				any_dirs = 1;
				parse_tree(tree);
				subposns[i] = create_tree_entry_list(tree);
				posns[i] = posns[i]->next;
				src[i + o->merge] = o->df_conflict_entry;
				continue;
			}

			if (!o->merge)
				ce_stage = 0;
			else if (i + 1 < o->head_idx)
				ce_stage = 1;
			else if (i + 1 > o->head_idx)
				ce_stage = 3;
			else
				ce_stage = 2;

			ce = xcalloc(1, ce_size);
			ce->ce_mode = create_ce_mode(posns[i]->mode);
			ce->ce_flags = create_ce_flags(baselen + pathlen,
						       ce_stage);
			memcpy(ce->name, base, baselen);
			memcpy(ce->name + baselen, first, pathlen + 1);

			any_files = 1;

			hashcpy(ce->sha1, posns[i]->sha1);
			src[i + o->merge] = ce;
			subposns[i] = df_conflict_list;
			posns[i] = posns[i]->next;
		}
		if (any_files) {
			if (o->merge) {
				int ret;

#if DBRT_DEBUG > 1
				printf("%s:\n", first);
				for (i = 0; i < src_size; i++) {
					printf(" %d ", i);
					if (src[i])
						printf("%s\n", sha1_to_hex(src[i]->sha1));
					else
						printf("\n");
				}
#endif
				ret = o->fn(src, o);

#if DBRT_DEBUG > 1
				printf("Added %d entries\n", ret);
#endif
				o->pos += ret;
			} else {
				for (i = 0; i < src_size; i++) {
					if (src[i]) {
						add_cache_entry(src[i], ADD_CACHE_OK_TO_ADD|ADD_CACHE_SKIP_DFCHECK);
					}
				}
			}
		}
		if (any_dirs) {
			char *newbase = xmalloc(baselen + 2 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, first, pathlen);
			newbase[baselen + pathlen] = '/';
			newbase[baselen + pathlen + 1] = '\0';
			if (unpack_trees_rec(subposns, len, newbase, o,
					     df_conflict_list)) {
				retval = -1;
				goto leave_directory;
			}
			free(newbase);
		}
		free(subposns);
		free(src);
	} while (1);

 leave_directory:
	if (o->dir)
		pop_exclude_per_directory(o->dir, i_stk);
	return retval;
}

/* Unlink the last component and attempt to remove leading
 * directories, in case this unlink is the removal of the
 * last entry in the directory -- empty directories are removed.
 */
static void unlink_entry(char *name)
{
	char *cp, *prev;

	if (unlink(name))
		return;
	prev = NULL;
	while (1) {
		int status;
		cp = strrchr(name, '/');
		if (prev)
			*prev = '/';
		if (!cp)
			break;

		*cp = 0;
		status = rmdir(name);
		if (status) {
			*cp = '/';
			break;
		}
		prev = cp;
	}
}

static struct checkout state;
static void check_updates(struct cache_entry **src, int nr,
		struct unpack_trees_options *o)
{
	unsigned short mask = htons(CE_UPDATE);
	unsigned cnt = 0, total = 0;
	struct progress progress;

	if (o->update && o->verbose_update) {
		for (total = cnt = 0; cnt < nr; cnt++) {
			struct cache_entry *ce = src[cnt];
			if (!ce->ce_mode || ce->ce_flags & mask)
				total++;
		}

		start_progress_delay(&progress, "Checking %u files out...",
				     "", total, 50, 2);
		cnt = 0;
	}

	while (nr--) {
		struct cache_entry *ce = *src++;

		if (total)
			if (!ce->ce_mode || ce->ce_flags & mask)
				display_progress(&progress, ++cnt);
		if (!ce->ce_mode) {
			if (o->update)
				unlink_entry(ce->name);
			continue;
		}
		if (ce->ce_flags & mask) {
			ce->ce_flags &= ~mask;
			if (o->update)
				checkout_entry(ce, &state, NULL);
		}
	}
	if (total)
		stop_progress(&progress);;
}

int unpack_trees(struct object_list *trees, struct unpack_trees_options *o)
{
	unsigned len = object_list_length(trees);
	struct tree_entry_list **posns;
	int i;
	struct object_list *posn = trees;
	struct tree_entry_list df_conflict_list;
	static struct cache_entry *dfc;

	memset(&df_conflict_list, 0, sizeof(df_conflict_list));
	df_conflict_list.next = &df_conflict_list;
	memset(&state, 0, sizeof(state));
	state.base_dir = "";
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;

	o->merge_size = len;

	if (!dfc)
		dfc = xcalloc(1, sizeof(struct cache_entry) + 1);
	o->df_conflict_entry = dfc;

	if (len) {
		posns = xmalloc(len * sizeof(struct tree_entry_list *));
		for (i = 0; i < len; i++) {
			posns[i] = create_tree_entry_list((struct tree *) posn->item);
			posn = posn->next;
		}
		if (unpack_trees_rec(posns, len, o->prefix ? o->prefix : "",
				     o, &df_conflict_list))
			return -1;
	}

	if (o->trivial_merges_only && o->nontrivial_merge)
		die("Merge requires file-level merging");

	check_updates(active_cache, active_nr, o);
	return 0;
}

/* Here come the merge functions */

static void reject_merge(struct cache_entry *ce)
{
	die("Entry '%s' would be overwritten by merge. Cannot merge.",
	    ce->name);
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
static void verify_uptodate(struct cache_entry *ce,
		struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset)
		return;

	if (!lstat(ce->name, &st)) {
		unsigned changed = ce_match_stat(ce, &st, 1);
		if (!changed)
			return;
		errno = 0;
	}
	if (o->reset) {
		ce->ce_flags |= htons(CE_UPDATE);
		return;
	}
	if (errno == ENOENT)
		return;
	die("Entry '%s' not uptodate. Cannot merge.", ce->name);
}

static void invalidate_ce_path(struct cache_entry *ce)
{
	if (ce)
		cache_tree_invalidate_path(active_cache_tree, ce->name);
}

static int verify_clean_subdirectory(const char *path, const char *action,
				      struct unpack_trees_options *o)
{
	/*
	 * we are about to extract "path"; we would not want to lose
	 * anything in the existing directory there.
	 */
	int namelen;
	int pos, i;
	struct dir_struct d;
	char *pathbuf;
	int cnt = 0;

	/*
	 * First let's make sure we do not have a local modification
	 * in that directory.
	 */
	namelen = strlen(path);
	pos = cache_name_pos(path, namelen);
	if (0 <= pos)
		return cnt; /* we have it as nondirectory */
	pos = -pos - 1;
	for (i = pos; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		int len = ce_namelen(ce);
		if (len < namelen ||
		    strncmp(path, ce->name, namelen) ||
		    ce->name[namelen] != '/')
			break;
		/*
		 * ce->name is an entry in the subdirectory.
		 */
		if (!ce_stage(ce)) {
			verify_uptodate(ce, o);
			ce->ce_mode = 0;
		}
		cnt++;
	}

	/*
	 * Then we need to make sure that we do not lose a locally
	 * present file that is not ignored.
	 */
	pathbuf = xmalloc(namelen + 2);
	memcpy(pathbuf, path, namelen);
	strcpy(pathbuf+namelen, "/");

	memset(&d, 0, sizeof(d));
	if (o->dir)
		d.exclude_per_dir = o->dir->exclude_per_dir;
	i = read_directory(&d, path, pathbuf, namelen+1, NULL);
	if (i)
		die("Updating '%s' would lose untracked files in it",
		    path);
	free(pathbuf);
	return cnt;
}

/*
 * We do not want to remove or overwrite a working tree file that
 * is not tracked, unless it is ignored.
 */
static void verify_absent(const char *path, const char *action,
		struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset || !o->update)
		return;

	if (!lstat(path, &st)) {
		int cnt;

		if (o->dir && excluded(o->dir, path))
			/*
			 * path is explicitly excluded, so it is Ok to
			 * overwrite it.
			 */
			return;
		if (S_ISDIR(st.st_mode)) {
			/*
			 * We are checking out path "foo" and
			 * found "foo/." in the working tree.
			 * This is tricky -- if we have modified
			 * files that are in "foo/" we would lose
			 * it.
			 */
			cnt = verify_clean_subdirectory(path, action, o);

			/*
			 * If this removed entries from the index,
			 * what that means is:
			 *
			 * (1) the caller unpack_trees_rec() saw path/foo
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
			o->pos += cnt;
			return;
		}

		/*
		 * The previous round may already have decided to
		 * delete this path, which is in a subdirectory that
		 * is being replaced with a blob.
		 */
		cnt = cache_name_pos(path, strlen(path));
		if (0 <= cnt) {
			struct cache_entry *ce = active_cache[cnt];
			if (!ce_stage(ce) && !ce->ce_mode)
				return;
		}

		die("Untracked working tree file '%s' "
		    "would be %s by merge.", path, action);
	}
}

static int merged_entry(struct cache_entry *merge, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	merge->ce_flags |= htons(CE_UPDATE);
	if (old) {
		/*
		 * See if we can re-use the old CE directly?
		 * That way we get the uptodate stat info.
		 *
		 * This also removes the UPDATE flag on
		 * a match.
		 */
		if (same(old, merge)) {
			*merge = *old;
		} else {
			verify_uptodate(old, o);
			invalidate_ce_path(old);
		}
	}
	else {
		verify_absent(merge->name, "overwritten", o);
		invalidate_ce_path(merge);
	}

	merge->ce_flags &= ~htons(CE_STAGEMASK);
	add_cache_entry(merge, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
	return 1;
}

static int deleted_entry(struct cache_entry *ce, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	if (old)
		verify_uptodate(old, o);
	else
		verify_absent(ce->name, "removed", o);
	ce->ce_mode = 0;
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
	invalidate_ce_path(ce);
	return 1;
}

static int keep_entry(struct cache_entry *ce, struct unpack_trees_options *o)
{
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD);
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
			ntohl(ce->ce_mode),
			sha1_to_hex(ce->sha1),
			ce_stage(ce),
			ce->name);
}
#endif

int threeway_merge(struct cache_entry **stages,
		struct unpack_trees_options *o)
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
			reject_merge(index);
		return merged_entry(remote, index, o);
	}
	/*
	 * If we have an entry in the index cache, then we want to
	 * make sure that it matches head.
	 */
	if (index && !same(index, head)) {
		reject_merge(index);
	}

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
		const char *path = NULL;

		if (index)
			path = index->name;
		else if (head)
			path = head->name;
		else if (remote)
			path = remote->name;
		else {
			for (i = 1; i < o->head_idx; i++) {
				if (stages[i] && stages[i] != o->df_conflict_entry) {
					path = stages[i]->name;
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
			else if (path && !head_deleted)
				verify_absent(path, "removed", o);
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
		verify_uptodate(index, o);
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
int twoway_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
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
				reject_merge(oldtree);
			if (current)
				reject_merge(current);
			if (newtree)
				reject_merge(newtree);
			return -1;
		}
	}
	else if (newtree)
		return merged_entry(newtree, current, o);
	else
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
		die("Entry '%s' overlaps.  Cannot bind.", a->name);
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
int oneway_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (o->merge_size != 1)
		return error("Cannot do a oneway merge of %d trees",
			     o->merge_size);

	if (!a)
		return deleted_entry(old, old, o);
	if (old && same(old, a)) {
		if (o->reset) {
			struct stat st;
			if (lstat(old->name, &st) ||
			    ce_match_stat(old, &st, 1))
				old->ce_flags |= htons(CE_UPDATE);
		}
		return keep_entry(old, o);
	}
	return merged_entry(a, old, o);
}
