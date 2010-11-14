#ifndef NOTES_MERGE_H
#define NOTES_MERGE_H

enum notes_merge_verbosity {
	NOTES_MERGE_VERBOSITY_DEFAULT = 2,
	NOTES_MERGE_VERBOSITY_MAX = 5
};

struct notes_merge_options {
	const char *local_ref;
	const char *remote_ref;
	const char *commit_msg;
	int verbosity;
};

void init_notes_merge_options(struct notes_merge_options *o);

/*
 * Create new notes commit from the given notes tree
 *
 * Properties of the created commit:
 * - tree: the result of converting t to a tree object with write_notes_tree().
 * - parents: the given parents OR (if NULL) the commit referenced by t->ref.
 * - author/committer: the default determined by commmit_tree().
 * - commit message: msg
 *
 * The resulting commit SHA1 is stored in result_sha1.
 */
void create_notes_commit(struct notes_tree *t, struct commit_list *parents,
			 const char *msg, unsigned char *result_sha1);

/*
 * Merge notes from o->remote_ref into o->local_ref
 *
 * The given notes_tree 'local_tree' must be the notes_tree referenced by the
 * o->local_ref. This is the notes_tree in which the object-level merge is
 * performed.
 *
 * The commits given by the two refs are merged, producing one of the following
 * outcomes:
 *
 * 1. The merge trivially results in an existing commit (e.g. fast-forward or
 *    already-up-to-date). 'local_tree' is untouched, the SHA1 of the result
 *    is written into 'result_sha1' and 0 is returned.
 * 2. The merge successfully completes, producing a merge commit. local_tree
 *    contains the updated notes tree, the SHA1 of the resulting commit is
 *    written into 'result_sha1', and 1 is returned.
 * 3. The merge fails. result_sha1 is set to null_sha1, and -1 is returned.
 *
 * Both o->local_ref and o->remote_ref must be given (non-NULL), but either ref
 * (although not both) may refer to a non-existing notes ref, in which case
 * that notes ref is interpreted as an empty notes tree, and the merge
 * trivially results in what the other ref points to.
 */
int notes_merge(struct notes_merge_options *o,
		struct notes_tree *local_tree,
		unsigned char *result_sha1);

#endif
