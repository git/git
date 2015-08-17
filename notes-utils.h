#ifndef NOTES_UTILS_H
#define NOTES_UTILS_H

#include "notes.h"

/*
 * Create new notes commit from the given notes tree
 *
 * Properties of the created commit:
 * - tree: the result of converting t to a tree object with write_notes_tree().
 * - parents: the given parents OR (if NULL) the commit referenced by t->ref.
 * - author/committer: the default determined by commit_tree().
 * - commit message: msg
 *
 * The resulting commit SHA1 is stored in result_sha1.
 */
void create_notes_commit(struct notes_tree *t, struct commit_list *parents,
			 const char *msg, size_t msg_len, unsigned char *result_sha1);

void commit_notes(struct notes_tree *t, const char *msg);

enum notes_merge_strategy {
		NOTES_MERGE_RESOLVE_MANUAL = 0,
		NOTES_MERGE_RESOLVE_OURS,
		NOTES_MERGE_RESOLVE_THEIRS,
		NOTES_MERGE_RESOLVE_UNION,
		NOTES_MERGE_RESOLVE_CAT_SORT_UNIQ
};

struct notes_rewrite_cfg {
	struct notes_tree **trees;
	const char *cmd;
	int enabled;
	combine_notes_fn combine;
	struct string_list *refs;
	int refs_from_env;
	int mode_from_env;
};

int parse_notes_merge_strategy(const char *v, enum notes_merge_strategy *s);
struct notes_rewrite_cfg *init_copy_notes_for_rewrite(const char *cmd);
int copy_note_for_rewrite(struct notes_rewrite_cfg *c,
			  const unsigned char *from_obj, const unsigned char *to_obj);
void finish_copy_notes_for_rewrite(struct notes_rewrite_cfg *c, const char *msg);

#endif
