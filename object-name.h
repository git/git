#ifndef OBJECT_NAME_H
#define OBJECT_NAME_H

#include "object.h"
#include "strbuf.h"

struct object_id;
struct repository;

struct object_context {
	unsigned short mode;
	/*
	 * symlink_path is only used by get_tree_entry_follow_symlinks,
	 * and only for symlinks that point outside the repository.
	 */
	struct strbuf symlink_path;
	/*
	 * If GET_OID_RECORD_PATH is set, this will record path (if any)
	 * found when resolving the name. The caller is responsible for
	 * releasing the memory.
	 */
	char *path;
};

/*
 * Return an abbreviated sha1 unique within this repository's object database.
 * The result will be at least `len` characters long, and will be NUL
 * terminated.
 *
 * The non-`_r` version returns a static buffer which remains valid until 4
 * more calls to repo_find_unique_abbrev are made.
 *
 * The `_r` variant writes to a buffer supplied by the caller, which must be at
 * least `GIT_MAX_HEXSZ + 1` bytes. The return value is the number of bytes
 * written (excluding the NUL terminator).
 *
 * Note that while this version avoids the static buffer, it is not fully
 * reentrant, as it calls into other non-reentrant git code.
 */
const char *repo_find_unique_abbrev(struct repository *r, const struct object_id *oid, int len);
int repo_find_unique_abbrev_r(struct repository *r, char *hex, const struct object_id *oid, int len);

int repo_get_oid(struct repository *r, const char *str, struct object_id *oid);
__attribute__((format (printf, 2, 3)))
int get_oidf(struct object_id *oid, const char *fmt, ...);
int repo_get_oid_commit(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_committish(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_tree(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_treeish(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_blob(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_mb(struct repository *r, const char *str, struct object_id *oid);
void maybe_die_on_misspelt_object_name(struct repository *repo,
				       const char *name,
				       const char *prefix);
enum get_oid_result get_oid_with_context(struct repository *repo, const char *str,
					 unsigned flags, struct object_id *oid,
					 struct object_context *oc);


typedef int each_abbrev_fn(const struct object_id *oid, void *);
int repo_for_each_abbrev(struct repository *r, const char *prefix, each_abbrev_fn, void *);

int set_disambiguate_hint_config(const char *var, const char *value);

/*
 * This reads short-hand syntax that not only evaluates to a commit
 * object name, but also can act as if the end user spelled the name
 * of the branch from the command line.
 *
 * - "@{-N}" finds the name of the Nth previous branch we were on, and
 *   places the name of the branch in the given buf and returns the
 *   number of characters parsed if successful.
 *
 * - "<branch>@{upstream}" finds the name of the other ref that
 *   <branch> is configured to merge with (missing <branch> defaults
 *   to the current branch), and places the name of the branch in the
 *   given buf and returns the number of characters parsed if
 *   successful.
 *
 * If the input is not of the accepted format, it returns a negative
 * number to signal an error.
 *
 * If the input was ok but there are not N branch switches in the
 * reflog, it returns 0.
 */
#define INTERPRET_BRANCH_LOCAL (1<<0)
#define INTERPRET_BRANCH_REMOTE (1<<1)
#define INTERPRET_BRANCH_HEAD (1<<2)
struct interpret_branch_name_options {
	/*
	 * If "allowed" is non-zero, it is a treated as a bitfield of allowable
	 * expansions: local branches ("refs/heads/"), remote branches
	 * ("refs/remotes/"), or "HEAD". If no "allowed" bits are set, any expansion is
	 * allowed, even ones to refs outside of those namespaces.
	 */
	unsigned allowed;

	/*
	 * If ^{upstream} or ^{push} (or equivalent) is requested, and the
	 * branch in question does not have such a reference, return -1 instead
	 * of die()-ing.
	 */
	unsigned nonfatal_dangling_mark : 1;
};
int repo_interpret_branch_name(struct repository *r,
			       const char *str, int len,
			       struct strbuf *buf,
			       const struct interpret_branch_name_options *options);

struct object *repo_peel_to_type(struct repository *r,
				 const char *name, int namelen,
				 struct object *o, enum object_type);

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV minimum_abbrev
#define DEFAULT_ABBREV default_abbrev

/* used when the code does not know or care what the default abbrev is */
#define FALLBACK_DEFAULT_ABBREV 7

#endif /* OBJECT_NAME_H */
