#ifndef NOTES_MERGE_H
#define NOTES_MERGE_H

enum notes_merge_verbosity {
	NOTES_MERGE_VERBOSITY_DEFAULT = 2,
	NOTES_MERGE_VERBOSITY_MAX = 5
};

struct notes_merge_options {
	const char *local_ref;
	const char *remote_ref;
	int verbosity;
};

void init_notes_merge_options(struct notes_merge_options *o);

/*
 * Merge notes from o->remote_ref into o->local_ref
 *
 * The commits given by the two refs are merged, producing one of the following
 * outcomes:
 *
 * 1. The merge trivially results in an existing commit (e.g. fast-forward or
 *    already-up-to-date). The SHA1 of the result is written into 'result_sha1'
 *    and 0 is returned.
 * 2. The merge fails. result_sha1 is set to null_sha1, and non-zero returned.
 *
 * Both o->local_ref and o->remote_ref must be given (non-NULL), but either ref
 * (although not both) may refer to a non-existing notes ref, in which case
 * that notes ref is interpreted as an empty notes tree, and the merge
 * trivially results in what the other ref points to.
 */
int notes_merge(struct notes_merge_options *o,
		unsigned char *result_sha1);

#endif
