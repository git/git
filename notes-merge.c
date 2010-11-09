#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "notes-merge.h"

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

int notes_merge(struct notes_merge_options *o,
		unsigned char *result_sha1)
{
	unsigned char local_sha1[20], remote_sha1[20];
	struct commit *local, *remote;
	struct commit_list *bases = NULL;
	const unsigned char *base_sha1;
	int result = 0;

	assert(o->local_ref && o->remote_ref);
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
		OUTPUT(o, 4, "No merge base found; doing history-less merge");
	} else if (!bases->next) {
		base_sha1 = bases->item->object.sha1;
		OUTPUT(o, 4, "One merge base found (%.7s)",
		       sha1_to_hex(base_sha1));
	} else {
		/* TODO: How to handle multiple merge-bases? */
		base_sha1 = bases->item->object.sha1;
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

	/* TODO: */
	result = error("notes_merge() cannot yet handle real merges.");

found_result:
	free_commit_list(bases);
	trace_printf("notes_merge(): result = %i, result_sha1 = %.7s\n",
	       result, sha1_to_hex(result_sha1));
	return result;
}
