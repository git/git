#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "diff.h"
#include "diffcore.h"
#include "notes.h"
#include "notes-merge.h"

struct notes_merge_pair {
	unsigned char obj[20], base[20], local[20], remote[20];
};

void init_notes_merge_options(struct notes_merge_options *o)
{
	memset(o, 0, sizeof(struct notes_merge_options));
	o->verbosity = NOTES_MERGE_VERBOSITY_DEFAULT;
}

#define OUTPUT(o, v, ...) \
	do { \
		if ((o)->verbosity >= (v)) { \
			printf(__VA_ARGS__); \
			puts(""); \
		} \
	} while (0)

static int path_to_sha1(const char *path, unsigned char *sha1)
{
	char hex_sha1[40];
	int i = 0;
	while (*path && i < 40) {
		if (*path != '/')
			hex_sha1[i++] = *path;
		path++;
	}
	if (*path || i != 40)
		return -1;
	return get_sha1_hex(hex_sha1, sha1);
}

static int verify_notes_filepair(struct diff_filepair *p, unsigned char *sha1)
{
	switch (p->status) {
	case DIFF_STATUS_MODIFIED:
		assert(p->one->mode == p->two->mode);
		assert(!is_null_sha1(p->one->sha1));
		assert(!is_null_sha1(p->two->sha1));
		break;
	case DIFF_STATUS_ADDED:
		assert(is_null_sha1(p->one->sha1));
		break;
	case DIFF_STATUS_DELETED:
		assert(is_null_sha1(p->two->sha1));
		break;
	default:
		return -1;
	}
	assert(!strcmp(p->one->path, p->two->path));
	return path_to_sha1(p->one->path, sha1);
}

static struct notes_merge_pair *find_notes_merge_pair_pos(
		struct notes_merge_pair *list, int len, unsigned char *obj,
		int insert_new, int *occupied)
{
	/*
	 * Both diff_tree_remote() and diff_tree_local() tend to process
	 * merge_pairs in ascending order. Therefore, cache last returned
	 * index, and search sequentially from there until the appropriate
	 * position is found.
	 *
	 * Since inserts only happen from diff_tree_remote() (which mainly
	 * _appends_), we don't care that inserting into the middle of the
	 * list is expensive (using memmove()).
	 */
	static int last_index;
	int i = last_index < len ? last_index : len - 1;
	int prev_cmp = 0, cmp = -1;
	while (i >= 0 && i < len) {
		cmp = hashcmp(obj, list[i].obj);
		if (!cmp) /* obj belongs @ i */
			break;
		else if (cmp < 0 && prev_cmp <= 0) /* obj belongs < i */
			i--;
		else if (cmp < 0) /* obj belongs between i-1 and i */
			break;
		else if (cmp > 0 && prev_cmp >= 0) /* obj belongs > i */
			i++;
		else /* if (cmp > 0) */ { /* obj belongs between i and i+1 */
			i++;
			break;
		}
		prev_cmp = cmp;
	}
	if (i < 0)
		i = 0;
	/* obj belongs at, or immediately preceding, index i (0 <= i <= len) */

	if (!cmp)
		*occupied = 1;
	else {
		*occupied = 0;
		if (insert_new && i < len) {
			memmove(list + i + 1, list + i,
				(len - i) * sizeof(struct notes_merge_pair));
			memset(list + i, 0, sizeof(struct notes_merge_pair));
		}
	}
	last_index = i;
	return list + i;
}

static unsigned char uninitialized[20] =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" \
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";

static struct notes_merge_pair *diff_tree_remote(struct notes_merge_options *o,
						 const unsigned char *base,
						 const unsigned char *remote,
						 int *num_changes)
{
	struct diff_options opt;
	struct notes_merge_pair *changes;
	int i, len = 0;

	trace_printf("\tdiff_tree_remote(base = %.7s, remote = %.7s)\n",
	       sha1_to_hex(base), sha1_to_hex(remote));

	diff_setup(&opt);
	DIFF_OPT_SET(&opt, RECURSIVE);
	opt.output_format = DIFF_FORMAT_NO_OUTPUT;
	if (diff_setup_done(&opt) < 0)
		die("diff_setup_done failed");
	diff_tree_sha1(base, remote, "", &opt);
	diffcore_std(&opt);

	changes = xcalloc(diff_queued_diff.nr, sizeof(struct notes_merge_pair));

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		struct notes_merge_pair *mp;
		int occupied;
		unsigned char obj[20];

		if (verify_notes_filepair(p, obj)) {
			trace_printf("\t\tCannot merge entry '%s' (%c): "
			       "%.7s -> %.7s. Skipping!\n", p->one->path,
			       p->status, sha1_to_hex(p->one->sha1),
			       sha1_to_hex(p->two->sha1));
			continue;
		}
		mp = find_notes_merge_pair_pos(changes, len, obj, 1, &occupied);
		if (occupied) {
			/* We've found an addition/deletion pair */
			assert(!hashcmp(mp->obj, obj));
			if (is_null_sha1(p->one->sha1)) { /* addition */
				assert(is_null_sha1(mp->remote));
				hashcpy(mp->remote, p->two->sha1);
			} else if (is_null_sha1(p->two->sha1)) { /* deletion */
				assert(is_null_sha1(mp->base));
				hashcpy(mp->base, p->one->sha1);
			} else
				assert(!"Invalid existing change recorded");
		} else {
			hashcpy(mp->obj, obj);
			hashcpy(mp->base, p->one->sha1);
			hashcpy(mp->local, uninitialized);
			hashcpy(mp->remote, p->two->sha1);
			len++;
		}
		trace_printf("\t\tStored remote change for %s: %.7s -> %.7s\n",
		       sha1_to_hex(mp->obj), sha1_to_hex(mp->base),
		       sha1_to_hex(mp->remote));
	}
	diff_flush(&opt);
	diff_tree_release_paths(&opt);

	*num_changes = len;
	return changes;
}

static void diff_tree_local(struct notes_merge_options *o,
			    struct notes_merge_pair *changes, int len,
			    const unsigned char *base,
			    const unsigned char *local)
{
	struct diff_options opt;
	int i;

	trace_printf("\tdiff_tree_local(len = %i, base = %.7s, local = %.7s)\n",
	       len, sha1_to_hex(base), sha1_to_hex(local));

	diff_setup(&opt);
	DIFF_OPT_SET(&opt, RECURSIVE);
	opt.output_format = DIFF_FORMAT_NO_OUTPUT;
	if (diff_setup_done(&opt) < 0)
		die("diff_setup_done failed");
	diff_tree_sha1(base, local, "", &opt);
	diffcore_std(&opt);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		struct notes_merge_pair *mp;
		int match;
		unsigned char obj[20];

		if (verify_notes_filepair(p, obj)) {
			trace_printf("\t\tCannot merge entry '%s' (%c): "
			       "%.7s -> %.7s. Skipping!\n", p->one->path,
			       p->status, sha1_to_hex(p->one->sha1),
			       sha1_to_hex(p->two->sha1));
			continue;
		}
		mp = find_notes_merge_pair_pos(changes, len, obj, 0, &match);
		if (!match) {
			trace_printf("\t\tIgnoring local-only change for %s: "
			       "%.7s -> %.7s\n", sha1_to_hex(obj),
			       sha1_to_hex(p->one->sha1),
			       sha1_to_hex(p->two->sha1));
			continue;
		}

		assert(!hashcmp(mp->obj, obj));
		if (is_null_sha1(p->two->sha1)) { /* deletion */
			/*
			 * Either this is a true deletion (1), or it is part
			 * of an A/D pair (2), or D/A pair (3):
			 *
			 * (1) mp->local is uninitialized; set it to null_sha1
			 * (2) mp->local is not uninitialized; don't touch it
			 * (3) mp->local is uninitialized; set it to null_sha1
			 *     (will be overwritten by following addition)
			 */
			if (!hashcmp(mp->local, uninitialized))
				hashclr(mp->local);
		} else if (is_null_sha1(p->one->sha1)) { /* addition */
			/*
			 * Either this is a true addition (1), or it is part
			 * of an A/D pair (2), or D/A pair (3):
			 *
			 * (1) mp->local is uninitialized; set to p->two->sha1
			 * (2) mp->local is uninitialized; set to p->two->sha1
			 * (3) mp->local is null_sha1;     set to p->two->sha1
			 */
			assert(is_null_sha1(mp->local) ||
			       !hashcmp(mp->local, uninitialized));
			hashcpy(mp->local, p->two->sha1);
		} else { /* modification */
			/*
			 * This is a true modification. p->one->sha1 shall
			 * match mp->base, and mp->local shall be uninitialized.
			 * Set mp->local to p->two->sha1.
			 */
			assert(!hashcmp(p->one->sha1, mp->base));
			assert(!hashcmp(mp->local, uninitialized));
			hashcpy(mp->local, p->two->sha1);
		}
		trace_printf("\t\tStored local change for %s: %.7s -> %.7s\n",
		       sha1_to_hex(mp->obj), sha1_to_hex(mp->base),
		       sha1_to_hex(mp->local));
	}
	diff_flush(&opt);
	diff_tree_release_paths(&opt);
}

static int merge_one_change(struct notes_merge_options *o,
			    struct notes_merge_pair *p, struct notes_tree *t)
{
	/*
	 * Return 0 if change was resolved (and added to notes_tree),
	 * 1 if conflict
	 */
	switch (o->strategy) {
	case NOTES_MERGE_RESOLVE_MANUAL:
		return 1;
	case NOTES_MERGE_RESOLVE_OURS:
		OUTPUT(o, 2, "Using local notes for %s", sha1_to_hex(p->obj));
		/* nothing to do */
		return 0;
	case NOTES_MERGE_RESOLVE_THEIRS:
		OUTPUT(o, 2, "Using remote notes for %s", sha1_to_hex(p->obj));
		if (add_note(t, p->obj, p->remote, combine_notes_overwrite))
			die("BUG: combine_notes_overwrite failed");
		return 0;
	case NOTES_MERGE_RESOLVE_UNION:
		OUTPUT(o, 2, "Concatenating local and remote notes for %s",
		       sha1_to_hex(p->obj));
		if (add_note(t, p->obj, p->remote, combine_notes_concatenate))
			die("failed to concatenate notes "
			    "(combine_notes_concatenate)");
		return 0;
	}
	die("Unknown strategy (%i).", o->strategy);
}

static int merge_changes(struct notes_merge_options *o,
			 struct notes_merge_pair *changes, int *num_changes,
			 struct notes_tree *t)
{
	int i, conflicts = 0;

	trace_printf("\tmerge_changes(num_changes = %i)\n", *num_changes);
	for (i = 0; i < *num_changes; i++) {
		struct notes_merge_pair *p = changes + i;
		trace_printf("\t\t%.7s: %.7s -> %.7s/%.7s\n",
		       sha1_to_hex(p->obj), sha1_to_hex(p->base),
		       sha1_to_hex(p->local), sha1_to_hex(p->remote));

		if (!hashcmp(p->base, p->remote)) {
			/* no remote change; nothing to do */
			trace_printf("\t\t\tskipping (no remote change)\n");
		} else if (!hashcmp(p->local, p->remote)) {
			/* same change in local and remote; nothing to do */
			trace_printf("\t\t\tskipping (local == remote)\n");
		} else if (!hashcmp(p->local, uninitialized) ||
			   !hashcmp(p->local, p->base)) {
			/* no local change; adopt remote change */
			trace_printf("\t\t\tno local change, adopted remote\n");
			if (add_note(t, p->obj, p->remote,
				     combine_notes_overwrite))
				die("BUG: combine_notes_overwrite failed");
		} else {
			/* need file-level merge between local and remote */
			trace_printf("\t\t\tneed content-level merge\n");
			conflicts += merge_one_change(o, p, t);
		}
	}

	return conflicts;
}

static int merge_from_diffs(struct notes_merge_options *o,
			    const unsigned char *base,
			    const unsigned char *local,
			    const unsigned char *remote, struct notes_tree *t)
{
	struct notes_merge_pair *changes;
	int num_changes, conflicts;

	trace_printf("\tmerge_from_diffs(base = %.7s, local = %.7s, "
	       "remote = %.7s)\n", sha1_to_hex(base), sha1_to_hex(local),
	       sha1_to_hex(remote));

	changes = diff_tree_remote(o, base, remote, &num_changes);
	diff_tree_local(o, changes, num_changes, base, local);

	conflicts = merge_changes(o, changes, &num_changes, t);
	free(changes);

	OUTPUT(o, 4, "Merge result: %i unmerged notes and a %s notes tree",
	       conflicts, t->dirty ? "dirty" : "clean");

	return conflicts ? -1 : 1;
}

void create_notes_commit(struct notes_tree *t, struct commit_list *parents,
			 const char *msg, unsigned char *result_sha1)
{
	unsigned char tree_sha1[20];

	assert(t->initialized);

	if (write_notes_tree(t, tree_sha1))
		die("Failed to write notes tree to database");

	if (!parents) {
		/* Deduce parent commit from t->ref */
		unsigned char parent_sha1[20];
		if (!read_ref(t->ref, parent_sha1)) {
			struct commit *parent = lookup_commit(parent_sha1);
			if (!parent || parse_commit(parent))
				die("Failed to find/parse commit %s", t->ref);
			commit_list_insert(parent, &parents);
		}
		/* else: t->ref points to nothing, assume root/orphan commit */
	}

	if (commit_tree(msg, tree_sha1, parents, result_sha1, NULL))
		die("Failed to commit notes tree to database");
}

int notes_merge(struct notes_merge_options *o,
		struct notes_tree *local_tree,
		unsigned char *result_sha1)
{
	unsigned char local_sha1[20], remote_sha1[20];
	struct commit *local, *remote;
	struct commit_list *bases = NULL;
	const unsigned char *base_sha1, *base_tree_sha1;
	int result = 0;

	assert(o->local_ref && o->remote_ref);
	assert(!strcmp(o->local_ref, local_tree->ref));
	hashclr(result_sha1);

	trace_printf("notes_merge(o->local_ref = %s, o->remote_ref = %s)\n",
	       o->local_ref, o->remote_ref);

	/* Dereference o->local_ref into local_sha1 */
	if (!resolve_ref(o->local_ref, local_sha1, 0, NULL))
		die("Failed to resolve local notes ref '%s'", o->local_ref);
	else if (!check_ref_format(o->local_ref) && is_null_sha1(local_sha1))
		local = NULL; /* local_sha1 == null_sha1 indicates unborn ref */
	else if (!(local = lookup_commit_reference(local_sha1)))
		die("Could not parse local commit %s (%s)",
		    sha1_to_hex(local_sha1), o->local_ref);
	trace_printf("\tlocal commit: %.7s\n", sha1_to_hex(local_sha1));

	/* Dereference o->remote_ref into remote_sha1 */
	if (get_sha1(o->remote_ref, remote_sha1)) {
		/*
		 * Failed to get remote_sha1. If o->remote_ref looks like an
		 * unborn ref, perform the merge using an empty notes tree.
		 */
		if (!check_ref_format(o->remote_ref)) {
			hashclr(remote_sha1);
			remote = NULL;
		} else {
			die("Failed to resolve remote notes ref '%s'",
			    o->remote_ref);
		}
	} else if (!(remote = lookup_commit_reference(remote_sha1))) {
		die("Could not parse remote commit %s (%s)",
		    sha1_to_hex(remote_sha1), o->remote_ref);
	}
	trace_printf("\tremote commit: %.7s\n", sha1_to_hex(remote_sha1));

	if (!local && !remote)
		die("Cannot merge empty notes ref (%s) into empty notes ref "
		    "(%s)", o->remote_ref, o->local_ref);
	if (!local) {
		/* result == remote commit */
		hashcpy(result_sha1, remote_sha1);
		goto found_result;
	}
	if (!remote) {
		/* result == local commit */
		hashcpy(result_sha1, local_sha1);
		goto found_result;
	}
	assert(local && remote);

	/* Find merge bases */
	bases = get_merge_bases(local, remote, 1);
	if (!bases) {
		base_sha1 = null_sha1;
		base_tree_sha1 = (unsigned char *)EMPTY_TREE_SHA1_BIN;
		OUTPUT(o, 4, "No merge base found; doing history-less merge");
	} else if (!bases->next) {
		base_sha1 = bases->item->object.sha1;
		base_tree_sha1 = bases->item->tree->object.sha1;
		OUTPUT(o, 4, "One merge base found (%.7s)",
		       sha1_to_hex(base_sha1));
	} else {
		/* TODO: How to handle multiple merge-bases? */
		base_sha1 = bases->item->object.sha1;
		base_tree_sha1 = bases->item->tree->object.sha1;
		OUTPUT(o, 3, "Multiple merge bases found. Using the first "
		       "(%.7s)", sha1_to_hex(base_sha1));
	}

	OUTPUT(o, 4, "Merging remote commit %.7s into local commit %.7s with "
	       "merge-base %.7s", sha1_to_hex(remote->object.sha1),
	       sha1_to_hex(local->object.sha1), sha1_to_hex(base_sha1));

	if (!hashcmp(remote->object.sha1, base_sha1)) {
		/* Already merged; result == local commit */
		OUTPUT(o, 2, "Already up-to-date!");
		hashcpy(result_sha1, local->object.sha1);
		goto found_result;
	}
	if (!hashcmp(local->object.sha1, base_sha1)) {
		/* Fast-forward; result == remote commit */
		OUTPUT(o, 2, "Fast-forward");
		hashcpy(result_sha1, remote->object.sha1);
		goto found_result;
	}

	result = merge_from_diffs(o, base_tree_sha1, local->tree->object.sha1,
				  remote->tree->object.sha1, local_tree);

	if (result > 0) { /* successful non-trivial merge */
		/* Commit result */
		struct commit_list *parents = NULL;
		commit_list_insert(remote, &parents); /* LIFO order */
		commit_list_insert(local, &parents);
		create_notes_commit(local_tree, parents, o->commit_msg,
				    result_sha1);
	}

found_result:
	free_commit_list(bases);
	trace_printf("notes_merge(): result = %i, result_sha1 = %.7s\n",
	       result, sha1_to_hex(result_sha1));
	return result;
}
