#include "git-compat-util.h"
#include "advice.h"
#include "commit.h"
#include "gettext.h"
#include "refs.h"
#include "object-file.h"
#include "object-name.h"
#include "object-store.h"
#include "repository.h"
#include "diff.h"
#include "diffcore.h"
#include "hex.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "dir.h"
#include "notes.h"
#include "notes-merge.h"
#include "strbuf.h"
#include "trace.h"
#include "notes-utils.h"
#include "commit-reach.h"
#include "wrapper.h"

struct notes_merge_pair {
	struct object_id obj, base, local, remote;
};

void init_notes_merge_options(struct repository *r,
			      struct notes_merge_options *o)
{
	memset(o, 0, sizeof(struct notes_merge_options));
	strbuf_init(&(o->commit_msg), 0);
	o->verbosity = NOTES_MERGE_VERBOSITY_DEFAULT;
	o->repo = r;
}

static int path_to_oid(const char *path, struct object_id *oid)
{
	char hex_oid[GIT_MAX_HEXSZ];
	int i = 0;
	while (*path && i < the_hash_algo->hexsz) {
		if (*path != '/')
			hex_oid[i++] = *path;
		path++;
	}
	if (*path || i != the_hash_algo->hexsz)
		return -1;
	return get_oid_hex(hex_oid, oid);
}

static int verify_notes_filepair(struct diff_filepair *p, struct object_id *oid)
{
	switch (p->status) {
	case DIFF_STATUS_MODIFIED:
		assert(p->one->mode == p->two->mode);
		assert(!is_null_oid(&p->one->oid));
		assert(!is_null_oid(&p->two->oid));
		break;
	case DIFF_STATUS_ADDED:
		assert(is_null_oid(&p->one->oid));
		break;
	case DIFF_STATUS_DELETED:
		assert(is_null_oid(&p->two->oid));
		break;
	default:
		return -1;
	}
	assert(!strcmp(p->one->path, p->two->path));
	return path_to_oid(p->one->path, oid);
}

static struct notes_merge_pair *find_notes_merge_pair_pos(
		struct notes_merge_pair *list, int len, struct object_id *obj,
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
		cmp = oidcmp(obj, &list[i].obj);
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
			MOVE_ARRAY(list + i + 1, list + i, len - i);
			memset(list + i, 0, sizeof(struct notes_merge_pair));
		}
	}
	last_index = i;
	return list + i;
}

static struct object_id uninitialized = {
	.hash =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" \
	"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
};

static struct notes_merge_pair *diff_tree_remote(struct notes_merge_options *o,
						 const struct object_id *base,
						 const struct object_id *remote,
						 int *num_changes)
{
	struct diff_options opt;
	struct notes_merge_pair *changes;
	int i, len = 0;

	trace_printf("\tdiff_tree_remote(base = %.7s, remote = %.7s)\n",
	       oid_to_hex(base), oid_to_hex(remote));

	repo_diff_setup(o->repo, &opt);
	opt.flags.recursive = 1;
	opt.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_setup_done(&opt);
	diff_tree_oid(base, remote, "", &opt);
	diffcore_std(&opt);

	CALLOC_ARRAY(changes, diff_queued_diff.nr);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		struct notes_merge_pair *mp;
		int occupied;
		struct object_id obj;

		if (verify_notes_filepair(p, &obj)) {
			trace_printf("\t\tCannot merge entry '%s' (%c): "
			       "%.7s -> %.7s. Skipping!\n", p->one->path,
			       p->status, oid_to_hex(&p->one->oid),
			       oid_to_hex(&p->two->oid));
			continue;
		}
		mp = find_notes_merge_pair_pos(changes, len, &obj, 1, &occupied);
		if (occupied) {
			/* We've found an addition/deletion pair */
			assert(oideq(&mp->obj, &obj));
			if (is_null_oid(&p->one->oid)) { /* addition */
				assert(is_null_oid(&mp->remote));
				oidcpy(&mp->remote, &p->two->oid);
			} else if (is_null_oid(&p->two->oid)) { /* deletion */
				assert(is_null_oid(&mp->base));
				oidcpy(&mp->base, &p->one->oid);
			} else
				assert(!"Invalid existing change recorded");
		} else {
			oidcpy(&mp->obj, &obj);
			oidcpy(&mp->base, &p->one->oid);
			oidcpy(&mp->local, &uninitialized);
			oidcpy(&mp->remote, &p->two->oid);
			len++;
		}
		trace_printf("\t\tStored remote change for %s: %.7s -> %.7s\n",
		       oid_to_hex(&mp->obj), oid_to_hex(&mp->base),
		       oid_to_hex(&mp->remote));
	}
	diff_flush(&opt);

	*num_changes = len;
	return changes;
}

static void diff_tree_local(struct notes_merge_options *o,
			    struct notes_merge_pair *changes, int len,
			    const struct object_id *base,
			    const struct object_id *local)
{
	struct diff_options opt;
	int i;

	trace_printf("\tdiff_tree_local(len = %i, base = %.7s, local = %.7s)\n",
	       len, oid_to_hex(base), oid_to_hex(local));

	repo_diff_setup(o->repo, &opt);
	opt.flags.recursive = 1;
	opt.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_setup_done(&opt);
	diff_tree_oid(base, local, "", &opt);
	diffcore_std(&opt);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		struct notes_merge_pair *mp;
		int match;
		struct object_id obj;

		if (verify_notes_filepair(p, &obj)) {
			trace_printf("\t\tCannot merge entry '%s' (%c): "
			       "%.7s -> %.7s. Skipping!\n", p->one->path,
			       p->status, oid_to_hex(&p->one->oid),
			       oid_to_hex(&p->two->oid));
			continue;
		}
		mp = find_notes_merge_pair_pos(changes, len, &obj, 0, &match);
		if (!match) {
			trace_printf("\t\tIgnoring local-only change for %s: "
			       "%.7s -> %.7s\n", oid_to_hex(&obj),
			       oid_to_hex(&p->one->oid),
			       oid_to_hex(&p->two->oid));
			continue;
		}

		assert(oideq(&mp->obj, &obj));
		if (is_null_oid(&p->two->oid)) { /* deletion */
			/*
			 * Either this is a true deletion (1), or it is part
			 * of an A/D pair (2), or D/A pair (3):
			 *
			 * (1) mp->local is uninitialized; set it to null_sha1
			 * (2) mp->local is not uninitialized; don't touch it
			 * (3) mp->local is uninitialized; set it to null_sha1
			 *     (will be overwritten by following addition)
			 */
			if (oideq(&mp->local, &uninitialized))
				oidclr(&mp->local);
		} else if (is_null_oid(&p->one->oid)) { /* addition */
			/*
			 * Either this is a true addition (1), or it is part
			 * of an A/D pair (2), or D/A pair (3):
			 *
			 * (1) mp->local is uninitialized; set to p->two->sha1
			 * (2) mp->local is uninitialized; set to p->two->sha1
			 * (3) mp->local is null_sha1;     set to p->two->sha1
			 */
			assert(is_null_oid(&mp->local) ||
			       oideq(&mp->local, &uninitialized));
			oidcpy(&mp->local, &p->two->oid);
		} else { /* modification */
			/*
			 * This is a true modification. p->one->sha1 shall
			 * match mp->base, and mp->local shall be uninitialized.
			 * Set mp->local to p->two->sha1.
			 */
			assert(oideq(&p->one->oid, &mp->base));
			assert(oideq(&mp->local, &uninitialized));
			oidcpy(&mp->local, &p->two->oid);
		}
		trace_printf("\t\tStored local change for %s: %.7s -> %.7s\n",
		       oid_to_hex(&mp->obj), oid_to_hex(&mp->base),
		       oid_to_hex(&mp->local));
	}
	diff_flush(&opt);
}

static void check_notes_merge_worktree(struct notes_merge_options *o)
{
	if (!o->has_worktree) {
		/*
		 * Must establish NOTES_MERGE_WORKTREE.
		 * Abort if NOTES_MERGE_WORKTREE already exists
		 */
		if (file_exists(git_path(NOTES_MERGE_WORKTREE)) &&
		    !is_empty_dir(git_path(NOTES_MERGE_WORKTREE))) {
			if (advice_enabled(ADVICE_RESOLVE_CONFLICT))
				die(_("You have not concluded your previous "
				    "notes merge (%s exists).\nPlease, use "
				    "'git notes merge --commit' or 'git notes "
				    "merge --abort' to commit/abort the "
				    "previous merge before you start a new "
				    "notes merge."), git_path("NOTES_MERGE_*"));
			else
				die(_("You have not concluded your notes merge "
				    "(%s exists)."), git_path("NOTES_MERGE_*"));
		}

		if (safe_create_leading_directories_const(git_path(
				NOTES_MERGE_WORKTREE "/.test")))
			die_errno("unable to create directory %s",
				  git_path(NOTES_MERGE_WORKTREE));
		o->has_worktree = 1;
	} else if (!file_exists(git_path(NOTES_MERGE_WORKTREE)))
		/* NOTES_MERGE_WORKTREE should already be established */
		die("missing '%s'. This should not happen",
		    git_path(NOTES_MERGE_WORKTREE));
}

static void write_buf_to_worktree(const struct object_id *obj,
				  const char *buf, unsigned long size)
{
	int fd;
	char *path = git_pathdup(NOTES_MERGE_WORKTREE "/%s", oid_to_hex(obj));
	if (safe_create_leading_directories_const(path))
		die_errno("unable to create directory for '%s'", path);

	fd = xopen(path, O_WRONLY | O_EXCL | O_CREAT, 0666);

	while (size > 0) {
		ssize_t ret = write_in_full(fd, buf, size);
		if (ret < 0) {
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die_errno("notes-merge");
		}
		size -= ret;
		buf += ret;
	}

	close(fd);
	free(path);
}

static void write_note_to_worktree(const struct object_id *obj,
				   const struct object_id *note)
{
	enum object_type type;
	unsigned long size;
	void *buf = repo_read_object_file(the_repository, note, &type, &size);

	if (!buf)
		die("cannot read note %s for object %s",
		    oid_to_hex(note), oid_to_hex(obj));
	if (type != OBJ_BLOB)
		die("blob expected in note %s for object %s",
		    oid_to_hex(note), oid_to_hex(obj));
	write_buf_to_worktree(obj, buf, size);
	free(buf);
}

static int ll_merge_in_worktree(struct notes_merge_options *o,
				struct notes_merge_pair *p)
{
	mmbuffer_t result_buf;
	mmfile_t base, local, remote;
	enum ll_merge_result status;

	read_mmblob(&base, &p->base);
	read_mmblob(&local, &p->local);
	read_mmblob(&remote, &p->remote);

	status = ll_merge(&result_buf, oid_to_hex(&p->obj), &base, NULL,
			  &local, o->local_ref, &remote, o->remote_ref,
			  o->repo->index, NULL);

	free(base.ptr);
	free(local.ptr);
	free(remote.ptr);

	if (status == LL_MERGE_BINARY_CONFLICT)
		warning("Cannot merge binary files: %s (%s vs. %s)",
			oid_to_hex(&p->obj), o->local_ref, o->remote_ref);
	if ((status < 0) || !result_buf.ptr)
		die("Failed to execute internal merge");

	write_buf_to_worktree(&p->obj, result_buf.ptr, result_buf.size);
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
	       oid_to_hex(&p->obj), oid_to_hex(&p->base),
	       oid_to_hex(&p->local), oid_to_hex(&p->remote));

	/* add "Conflicts:" section to commit message first time through */
	if (!o->has_worktree)
		strbuf_addstr(&(o->commit_msg), "\n\nConflicts:\n");

	strbuf_addf(&(o->commit_msg), "\t%s\n", oid_to_hex(&p->obj));

	if (o->verbosity >= 2)
		printf("Auto-merging notes for %s\n", oid_to_hex(&p->obj));
	check_notes_merge_worktree(o);
	if (is_null_oid(&p->local)) {
		/* D/F conflict, checkout p->remote */
		assert(!is_null_oid(&p->remote));
		if (o->verbosity >= 1)
			printf("CONFLICT (delete/modify): Notes for object %s "
				"deleted in %s and modified in %s. Version from %s "
				"left in tree.\n",
				oid_to_hex(&p->obj), lref, rref, rref);
		write_note_to_worktree(&p->obj, &p->remote);
	} else if (is_null_oid(&p->remote)) {
		/* D/F conflict, checkout p->local */
		assert(!is_null_oid(&p->local));
		if (o->verbosity >= 1)
			printf("CONFLICT (delete/modify): Notes for object %s "
				"deleted in %s and modified in %s. Version from %s "
				"left in tree.\n",
				oid_to_hex(&p->obj), rref, lref, lref);
		write_note_to_worktree(&p->obj, &p->local);
	} else {
		/* "regular" conflict, checkout result of ll_merge() */
		const char *reason = "content";
		if (is_null_oid(&p->base))
			reason = "add/add";
		assert(!is_null_oid(&p->local));
		assert(!is_null_oid(&p->remote));
		if (o->verbosity >= 1)
			printf("CONFLICT (%s): Merge conflict in notes for "
				"object %s\n", reason,
				oid_to_hex(&p->obj));
		ll_merge_in_worktree(o, p);
	}

	trace_printf("\t\t\tremoving from partial merge result\n");
	remove_note(t, p->obj.hash);

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
		if (o->verbosity >= 2)
			printf("Using local notes for %s\n",
						oid_to_hex(&p->obj));
		/* nothing to do */
		return 0;
	case NOTES_MERGE_RESOLVE_THEIRS:
		if (o->verbosity >= 2)
			printf("Using remote notes for %s\n",
						oid_to_hex(&p->obj));
		if (add_note(t, &p->obj, &p->remote, combine_notes_overwrite))
			BUG("combine_notes_overwrite failed");
		return 0;
	case NOTES_MERGE_RESOLVE_UNION:
		if (o->verbosity >= 2)
			printf("Concatenating local and remote notes for %s\n",
							oid_to_hex(&p->obj));
		if (add_note(t, &p->obj, &p->remote, combine_notes_concatenate))
			die("failed to concatenate notes "
			    "(combine_notes_concatenate)");
		return 0;
	case NOTES_MERGE_RESOLVE_CAT_SORT_UNIQ:
		if (o->verbosity >= 2)
			printf("Concatenating unique lines in local and remote "
				"notes for %s\n", oid_to_hex(&p->obj));
		if (add_note(t, &p->obj, &p->remote, combine_notes_cat_sort_uniq))
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
		       oid_to_hex(&p->obj), oid_to_hex(&p->base),
		       oid_to_hex(&p->local),
		       oid_to_hex(&p->remote));

		if (oideq(&p->base, &p->remote)) {
			/* no remote change; nothing to do */
			trace_printf("\t\t\tskipping (no remote change)\n");
		} else if (oideq(&p->local, &p->remote)) {
			/* same change in local and remote; nothing to do */
			trace_printf("\t\t\tskipping (local == remote)\n");
		} else if (oideq(&p->local, &uninitialized) ||
			   oideq(&p->local, &p->base)) {
			/* no local change; adopt remote change */
			trace_printf("\t\t\tno local change, adopted remote\n");
			if (add_note(t, &p->obj, &p->remote,
				     combine_notes_overwrite))
				BUG("combine_notes_overwrite failed");
		} else {
			/* need file-level merge between local and remote */
			trace_printf("\t\t\tneed content-level merge\n");
			conflicts += merge_one_change(o, p, t);
		}
	}

	return conflicts;
}

static int merge_from_diffs(struct notes_merge_options *o,
			    const struct object_id *base,
			    const struct object_id *local,
			    const struct object_id *remote,
			    struct notes_tree *t)
{
	struct notes_merge_pair *changes;
	int num_changes, conflicts;

	trace_printf("\tmerge_from_diffs(base = %.7s, local = %.7s, "
	       "remote = %.7s)\n", oid_to_hex(base), oid_to_hex(local),
	       oid_to_hex(remote));

	changes = diff_tree_remote(o, base, remote, &num_changes);
	diff_tree_local(o, changes, num_changes, base, local);

	conflicts = merge_changes(o, changes, &num_changes, t);
	free(changes);

	if (o->verbosity >= 4)
		printf(t->dirty ?
		       "Merge result: %i unmerged notes and a dirty notes tree\n" :
		       "Merge result: %i unmerged notes and a clean notes tree\n",
		       conflicts);

	return conflicts ? -1 : 1;
}

int notes_merge(struct notes_merge_options *o,
		struct notes_tree *local_tree,
		struct object_id *result_oid)
{
	struct object_id local_oid, remote_oid;
	struct commit *local, *remote;
	struct commit_list *bases = NULL;
	const struct object_id *base_oid, *base_tree_oid;
	int result = 0;

	assert(o->local_ref && o->remote_ref);
	assert(!strcmp(o->local_ref, local_tree->ref));
	oidclr(result_oid);

	trace_printf("notes_merge(o->local_ref = %s, o->remote_ref = %s)\n",
	       o->local_ref, o->remote_ref);

	/* Dereference o->local_ref into local_sha1 */
	if (read_ref_full(o->local_ref, 0, &local_oid, NULL))
		die("Failed to resolve local notes ref '%s'", o->local_ref);
	else if (!check_refname_format(o->local_ref, 0) &&
		is_null_oid(&local_oid))
		local = NULL; /* local_oid == null_oid indicates unborn ref */
	else if (!(local = lookup_commit_reference(o->repo, &local_oid)))
		die("Could not parse local commit %s (%s)",
		    oid_to_hex(&local_oid), o->local_ref);
	trace_printf("\tlocal commit: %.7s\n", oid_to_hex(&local_oid));

	/* Dereference o->remote_ref into remote_oid */
	if (repo_get_oid(the_repository, o->remote_ref, &remote_oid)) {
		/*
		 * Failed to get remote_oid. If o->remote_ref looks like an
		 * unborn ref, perform the merge using an empty notes tree.
		 */
		if (!check_refname_format(o->remote_ref, 0)) {
			oidclr(&remote_oid);
			remote = NULL;
		} else {
			die("Failed to resolve remote notes ref '%s'",
			    o->remote_ref);
		}
	} else if (!(remote = lookup_commit_reference(o->repo, &remote_oid))) {
		die("Could not parse remote commit %s (%s)",
		    oid_to_hex(&remote_oid), o->remote_ref);
	}
	trace_printf("\tremote commit: %.7s\n", oid_to_hex(&remote_oid));

	if (!local && !remote)
		die("Cannot merge empty notes ref (%s) into empty notes ref "
		    "(%s)", o->remote_ref, o->local_ref);
	if (!local) {
		/* result == remote commit */
		oidcpy(result_oid, &remote_oid);
		goto found_result;
	}
	if (!remote) {
		/* result == local commit */
		oidcpy(result_oid, &local_oid);
		goto found_result;
	}
	assert(local && remote);

	/* Find merge bases */
	bases = repo_get_merge_bases(the_repository, local, remote);
	if (!bases) {
		base_oid = null_oid();
		base_tree_oid = the_hash_algo->empty_tree;
		if (o->verbosity >= 4)
			printf("No merge base found; doing history-less merge\n");
	} else if (!bases->next) {
		base_oid = &bases->item->object.oid;
		base_tree_oid = get_commit_tree_oid(bases->item);
		if (o->verbosity >= 4)
			printf("One merge base found (%.7s)\n",
			       oid_to_hex(base_oid));
	} else {
		/* TODO: How to handle multiple merge-bases? */
		base_oid = &bases->item->object.oid;
		base_tree_oid = get_commit_tree_oid(bases->item);
		if (o->verbosity >= 3)
			printf("Multiple merge bases found. Using the first "
				"(%.7s)\n", oid_to_hex(base_oid));
	}

	if (o->verbosity >= 4)
		printf("Merging remote commit %.7s into local commit %.7s with "
			"merge-base %.7s\n", oid_to_hex(&remote->object.oid),
			oid_to_hex(&local->object.oid),
			oid_to_hex(base_oid));

	if (oideq(&remote->object.oid, base_oid)) {
		/* Already merged; result == local commit */
		if (o->verbosity >= 2)
			printf_ln("Already up to date.");
		oidcpy(result_oid, &local->object.oid);
		goto found_result;
	}
	if (oideq(&local->object.oid, base_oid)) {
		/* Fast-forward; result == remote commit */
		if (o->verbosity >= 2)
			printf("Fast-forward\n");
		oidcpy(result_oid, &remote->object.oid);
		goto found_result;
	}

	result = merge_from_diffs(o, base_tree_oid,
				  get_commit_tree_oid(local),
				  get_commit_tree_oid(remote), local_tree);

	if (result != 0) { /* non-trivial merge (with or without conflicts) */
		/* Commit (partial) result */
		struct commit_list *parents = NULL;
		commit_list_insert(remote, &parents); /* LIFO order */
		commit_list_insert(local, &parents);
		create_notes_commit(o->repo, local_tree, parents, o->commit_msg.buf,
				    o->commit_msg.len, result_oid);
	}

found_result:
	free_commit_list(bases);
	strbuf_release(&(o->commit_msg));
	trace_printf("notes_merge(): result = %i, result_oid = %.7s\n",
	       result, oid_to_hex(result_oid));
	return result;
}

int notes_merge_commit(struct notes_merge_options *o,
		       struct notes_tree *partial_tree,
		       struct commit *partial_commit,
		       struct object_id *result_oid)
{
	/*
	 * Iterate through files in .git/NOTES_MERGE_WORKTREE and add all
	 * found notes to 'partial_tree'. Write the updated notes tree to
	 * the DB, and commit the resulting tree object while reusing the
	 * commit message and parents from 'partial_commit'.
	 * Finally store the new commit object OID into 'result_oid'.
	 */
	DIR *dir;
	struct dirent *e;
	struct strbuf path = STRBUF_INIT;
	const char *buffer = repo_get_commit_buffer(the_repository,
						    partial_commit, NULL);
	const char *msg = strstr(buffer, "\n\n");
	int baselen;

	git_path_buf(&path, NOTES_MERGE_WORKTREE);
	if (o->verbosity >= 3)
		printf("Committing notes in notes merge worktree at %s\n",
			path.buf);

	if (!msg || msg[2] == '\0')
		die("partial notes commit has empty message");
	msg += 2;

	dir = opendir(path.buf);
	if (!dir)
		die_errno("could not open %s", path.buf);

	strbuf_addch(&path, '/');
	baselen = path.len;
	while ((e = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		struct stat st;
		struct object_id obj_oid, blob_oid;

		if (get_oid_hex(e->d_name, &obj_oid)) {
			if (o->verbosity >= 3)
				printf("Skipping non-SHA1 entry '%s%s'\n",
					path.buf, e->d_name);
			continue;
		}

		strbuf_addstr(&path, e->d_name);
		/* write file as blob, and add to partial_tree */
		if (stat(path.buf, &st))
			die_errno("Failed to stat '%s'", path.buf);
		if (index_path(o->repo->index, &blob_oid, path.buf, &st, HASH_WRITE_OBJECT))
			die("Failed to write blob object from '%s'", path.buf);
		if (add_note(partial_tree, &obj_oid, &blob_oid, NULL))
			die("Failed to add resolved note '%s' to notes tree",
			    path.buf);
		if (o->verbosity >= 4)
			printf("Added resolved note for object %s: %s\n",
				oid_to_hex(&obj_oid), oid_to_hex(&blob_oid));
		strbuf_setlen(&path, baselen);
	}

	create_notes_commit(o->repo, partial_tree, partial_commit->parents, msg,
			    strlen(msg), result_oid);
	repo_unuse_commit_buffer(the_repository, partial_commit, buffer);
	if (o->verbosity >= 4)
		printf("Finalized notes merge commit: %s\n",
			oid_to_hex(result_oid));
	strbuf_release(&path);
	closedir(dir);
	return 0;
}

int notes_merge_abort(struct notes_merge_options *o)
{
	/*
	 * Remove all files within .git/NOTES_MERGE_WORKTREE. We do not remove
	 * the .git/NOTES_MERGE_WORKTREE directory itself, since it might be
	 * the current working directory of the user.
	 */
	struct strbuf buf = STRBUF_INIT;
	int ret;

	git_path_buf(&buf, NOTES_MERGE_WORKTREE);
	if (o->verbosity >= 3)
		printf("Removing notes merge worktree at %s/*\n", buf.buf);
	ret = remove_dir_recursively(&buf, REMOVE_DIR_KEEP_TOPLEVEL);
	strbuf_release(&buf);
	return ret;
}
