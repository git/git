#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "diff.h"
#include "diffcore.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "dir.h"
#include "notes.h"
#include "notes-merge.h"
#include "strbuf.h"

struct notes_merge_pair {
	unsigned char obj[20], base[20], local[20], remote[20];
};

void init_notes_merge_options(struct notes_merge_options *o)
{
	memset(o, 0, sizeof(struct notes_merge_options));
	strbuf_init(&(o->commit_msg), 0);
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

static void check_notes_merge_worktree(struct notes_merge_options *o)
{
	if (!o->has_worktree) {
		/*
		 * Must establish NOTES_MERGE_WORKTREE.
		 * Abort if NOTES_MERGE_WORKTREE already exists
		 */
		if (file_exists(git_path(NOTES_MERGE_WORKTREE))) {
			if (advice_resolve_conflict)
				die("You have not concluded your previous "
				    "notes merge (%s exists).\nPlease, use "
				    "'git notes merge --commit' or 'git notes "
				    "merge --abort' to commit/abort the "
				    "previous merge before you start a new "
				    "notes merge.", git_path("NOTES_MERGE_*"));
			else
				die("You have not concluded your notes merge "
				    "(%s exists).", git_path("NOTES_MERGE_*"));
		}

		if (safe_create_leading_directories(git_path(
				NOTES_MERGE_WORKTREE "/.test")))
			die_errno("unable to create directory %s",
				  git_path(NOTES_MERGE_WORKTREE));
		o->has_worktree = 1;
	} else if (!file_exists(git_path(NOTES_MERGE_WORKTREE)))
		/* NOTES_MERGE_WORKTREE should already be established */
		die("missing '%s'. This should not happen",
		    git_path(NOTES_MERGE_WORKTREE));
}

static void write_buf_to_worktree(const unsigned char *obj,
				  const char *buf, unsigned long size)
{
	int fd;
	char *path = git_path(NOTES_MERGE_WORKTREE "/%s", sha1_to_hex(obj));
	if (safe_create_leading_directories(path))
		die_errno("unable to create directory for '%s'", path);
	if (file_exists(path))
		die("found existing file at '%s'", path);

	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if (fd < 0)
		die_errno("failed to open '%s'", path);

	while (size > 0) {
		long ret = write_in_full(fd, buf, size);
		if (ret < 0) {
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die_errno("notes-merge");
		} else if (!ret) {
			die("notes-merge: disk full?");
		}
		size -= ret;
		buf += ret;
	}

	close(fd);
}

static void write_note_to_worktree(const unsigned char *obj,
				   const unsigned char *note)
{
	enum object_type type;
	unsigned long size;
	void *buf = read_sha1_file(note, &type, &size);

	if (!buf)
		die("cannot read note %s for object %s",
		    sha1_to_hex(note), sha1_to_hex(obj));
	if (type != OBJ_BLOB)
		die("blob expected in note %s for object %s",
		    sha1_to_hex(note), sha1_to_hex(obj));
	write_buf_to_worktree(obj, buf, size);
	free(buf);
}

static int ll_merge_in_worktree(struct notes_merge_options *o,
				struct notes_merge_pair *p)
{
	mmbuffer_t result_buf;
	mmfile_t base, local, remote;
	int status;

	read_mmblob(&base, p->base);
	read_mmblob(&local, p->local);
	read_mmblob(&remote, p->remote);

	status = ll_merge(&result_buf, sha1_to_hex(p->obj), &base, NULL,
			  &local, o->local_ref, &remote, o->remote_ref, NULL);

	free(base.ptr);
	free(local.ptr);
	free(remote.ptr);

	if ((status < 0) || !result_buf.ptr)
		die("Failed to execute internal merge");

	write_buf_to_worktree(p->obj, result_buf.ptr, result_buf.size);
	free(result_buf.ptr);

	return status;
}

static int merge_one_change_manual(struct notes_merge_options *o,
				   struct notes_merge_pair *p,
				   struct notes_tree *t)
{
	const char *lref = o->local_ref ? o->local_ref : "local version";
	const char *rref = o->remote_ref ? o->remote_ref : "remote version";

	trace_printf("\t\t\tmerge_one_change_manual(obj = %.7s, base = %.7s, "
	       "local = %.7s, remote = %.7s)\n",
	       sha1_to_hex(p->obj), sha1_to_hex(p->base),
	       sha1_to_hex(p->local), sha1_to_hex(p->remote));

	/* add "Conflicts:" section to commit message first time through */
	if (!o->has_worktree)
		strbuf_addstr(&(o->commit_msg), "\n\nConflicts:\n");

	strbuf_addf(&(o->commit_msg), "\t%s\n", sha1_to_hex(p->obj));

	OUTPUT(o, 2, "Auto-merging notes for %s", sha1_to_hex(p->obj));
	check_notes_merge_worktree(o);
	if (is_null_sha1(p->local)) {
		/* D/F conflict, checkout p->remote */
		assert(!is_null_sha1(p->remote));
		OUTPUT(o, 1, "CONFLICT (delete/modify): Notes for object %s "
		       "deleted in %s and modified in %s. Version from %s "
		       "left in tree.", sha1_to_hex(p->obj), lref, rref, rref);
		write_note_to_worktree(p->obj, p->remote);
	} else if (is_null_sha1(p->remote)) {
		/* D/F conflict, checkout p->local */
		assert(!is_null_sha1(p->local));
		OUTPUT(o, 1, "CONFLICT (delete/modify): Notes for object %s "
		       "deleted in %s and modified in %s. Version from %s "
		       "left in tree.", sha1_to_hex(p->obj), rref, lref, lref);
		write_note_to_worktree(p->obj, p->local);
	} else {
		/* "regular" conflict, checkout result of ll_merge() */
		const char *reason = "content";
		if (is_null_sha1(p->base))
			reason = "add/add";
		assert(!is_null_sha1(p->local));
		assert(!is_null_sha1(p->remote));
		OUTPUT(o, 1, "CONFLICT (%s): Merge conflict in notes for "
		       "object %s", reason, sha1_to_hex(p->obj));
		ll_merge_in_worktree(o, p);
	}

	trace_printf("\t\t\tremoving from partial merge result\n");
	remove_note(t, p->obj);

	return 1;
}

static int merge_one_change(struct notes_merge_options *o,
			    struct notes_merge_pair *p, struct notes_tree *t)
{
	/*
	 * Return 0 if change is successfully resolved (stored in notes_tree).
	 * Return 1 is change results in a conflict (NOT stored in notes_tree,
	 * but instead written to NOTES_MERGE_WORKTREE with conflict markers).
	 */
	switch (o->strategy) {
	case NOTES_MERGE_RESOLVE_MANUAL:
		return merge_one_change_manual(o, p, t);
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
	case NOTES_MERGE_RESOLVE_CAT_SORT_UNIQ:
		OUTPUT(o, 2, "Concatenating unique lines in local and remote "
		       "notes for %s", sha1_to_hex(p->obj));
		if (add_note(t, p->obj, p->remote, combine_notes_cat_sort_uniq))
			die("failed to concatenate notes "
			    "(combine_notes_cat_sort_uniq)");
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

	if (commit_tree(msg, tree_sha1, parents, result_sha1, NULL, NULL))
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
	else if (!check_refname_format(o->local_ref, 0) &&
		is_null_sha1(local_sha1))
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
		if (!check_refname_format(o->remote_ref, 0)) {
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
		base_tree_sha1 = EMPTY_TREE_SHA1_BIN;
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

	if (result != 0) { /* non-trivial merge (with or without conflicts) */
		/* Commit (partial) result */
		struct commit_list *parents = NULL;
		commit_list_insert(remote, &parents); /* LIFO order */
		commit_list_insert(local, &parents);
		create_notes_commit(local_tree, parents, o->commit_msg.buf,
				    result_sha1);
	}

found_result:
	free_commit_list(bases);
	strbuf_release(&(o->commit_msg));
	trace_printf("notes_merge(): result = %i, result_sha1 = %.7s\n",
	       result, sha1_to_hex(result_sha1));
	return result;
}

int notes_merge_commit(struct notes_merge_options *o,
		       struct notes_tree *partial_tree,
		       struct commit *partial_commit,
		       unsigned char *result_sha1)
{
	/*
	 * Iterate through files in .git/NOTES_MERGE_WORKTREE and add all
	 * found notes to 'partial_tree'. Write the updates notes tree to
	 * the DB, and commit the resulting tree object while reusing the
	 * commit message and parents from 'partial_commit'.
	 * Finally store the new commit object SHA1 into 'result_sha1'.
	 */
	struct dir_struct dir;
	char *path = xstrdup(git_path(NOTES_MERGE_WORKTREE "/"));
	int path_len = strlen(path), i;
	const char *msg = strstr(partial_commit->buffer, "\n\n");

	OUTPUT(o, 3, "Committing notes in notes merge worktree at %.*s",
	       path_len - 1, path);

	if (!msg || msg[2] == '\0')
		die("partial notes commit has empty message");
	msg += 2;

	memset(&dir, 0, sizeof(dir));
	read_directory(&dir, path, path_len, NULL);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		struct stat st;
		const char *relpath = ent->name + path_len;
		unsigned char obj_sha1[20], blob_sha1[20];

		if (ent->len - path_len != 40 || get_sha1_hex(relpath, obj_sha1)) {
			OUTPUT(o, 3, "Skipping non-SHA1 entry '%s'", ent->name);
			continue;
		}

		/* write file as blob, and add to partial_tree */
		if (stat(ent->name, &st))
			die_errno("Failed to stat '%s'", ent->name);
		if (index_path(blob_sha1, ent->name, &st, HASH_WRITE_OBJECT))
			die("Failed to write blob object from '%s'", ent->name);
		if (add_note(partial_tree, obj_sha1, blob_sha1, NULL))
			die("Failed to add resolved note '%s' to notes tree",
			    ent->name);
		OUTPUT(o, 4, "Added resolved note for object %s: %s",
		       sha1_to_hex(obj_sha1), sha1_to_hex(blob_sha1));
	}

	create_notes_commit(partial_tree, partial_commit->parents, msg,
			    result_sha1);
	OUTPUT(o, 4, "Finalized notes merge commit: %s",
	       sha1_to_hex(result_sha1));
	free(path);
	return 0;
}

int notes_merge_abort(struct notes_merge_options *o)
{
	/* Remove .git/NOTES_MERGE_WORKTREE directory and all files within */
	struct strbuf buf = STRBUF_INIT;
	int ret;

	strbuf_addstr(&buf, git_path(NOTES_MERGE_WORKTREE));
	OUTPUT(o, 3, "Removing notes merge worktree at %s", buf.buf);
	ret = remove_dir_recursively(&buf, 0);
	strbuf_release(&buf);
	return ret;
}
