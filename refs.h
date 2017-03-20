#ifndef REFS_H
#define REFS_H

struct object_id;
struct ref_store;
struct strbuf;
struct string_list;
struct worktree;

/*
 * Resolve a reference, recursively following symbolic refererences.
 *
 * Store the referred-to object's name in sha1 and return the name of
 * the non-symbolic reference that ultimately pointed at it.  The
 * return value, if not NULL, is a pointer into either a static buffer
 * or the input ref.
 *
 * If the reference cannot be resolved to an object, the behavior
 * depends on the RESOLVE_REF_READING flag:
 *
 * - If RESOLVE_REF_READING is set, return NULL.
 *
 * - If RESOLVE_REF_READING is not set, clear sha1 and return the name of
 *   the last reference name in the chain, which will either be a non-symbolic
 *   reference or an undefined reference.  If this is a prelude to
 *   "writing" to the ref, the return value is the name of the ref
 *   that will actually be created or changed.
 *
 * If the RESOLVE_REF_NO_RECURSE flag is passed, only resolves one
 * level of symbolic reference.  The value stored in sha1 for a symbolic
 * reference will always be null_sha1 in this case, and the return
 * value is the reference that the symref refers to directly.
 *
 * If flags is non-NULL, set the value that it points to the
 * combination of REF_ISPACKED (if the reference was found among the
 * packed references), REF_ISSYMREF (if the initial reference was a
 * symbolic reference), REF_BAD_NAME (if the reference name is ill
 * formed --- see RESOLVE_REF_ALLOW_BAD_NAME below), and REF_ISBROKEN
 * (if the ref is malformed or has a bad name). See refs.h for more detail
 * on each flag.
 *
 * If ref is not a properly-formatted, normalized reference, return
 * NULL.  If more than MAXDEPTH recursive symbolic lookups are needed,
 * give up and return NULL.
 *
 * RESOLVE_REF_ALLOW_BAD_NAME allows resolving refs even when their
 * name is invalid according to git-check-ref-format(1).  If the name
 * is bad then the value stored in sha1 will be null_sha1 and the two
 * flags REF_ISBROKEN and REF_BAD_NAME will be set.
 *
 * Even with RESOLVE_REF_ALLOW_BAD_NAME, names that escape the refs/
 * directory and do not consist of all caps and underscores cannot be
 * resolved. The function returns NULL for such ref names.
 * Caps and underscores refers to the special refs, such as HEAD,
 * FETCH_HEAD and friends, that all live outside of the refs/ directory.
 */
#define RESOLVE_REF_READING 0x01
#define RESOLVE_REF_NO_RECURSE 0x02
#define RESOLVE_REF_ALLOW_BAD_NAME 0x04

const char *refs_resolve_ref_unsafe(struct ref_store *refs,
				    const char *refname,
				    int resolve_flags,
				    unsigned char *sha1,
				    int *flags);
const char *resolve_ref_unsafe(const char *refname, int resolve_flags,
			       unsigned char *sha1, int *flags);

char *refs_resolve_refdup(struct ref_store *refs,
			  const char *refname, int resolve_flags,
			  unsigned char *sha1, int *flags);
char *resolve_refdup(const char *refname, int resolve_flags,
		     unsigned char *sha1, int *flags);

int refs_read_ref_full(struct ref_store *refs, const char *refname,
		       int resolve_flags, unsigned char *sha1, int *flags);
int read_ref_full(const char *refname, int resolve_flags,
		  unsigned char *sha1, int *flags);
int read_ref(const char *refname, unsigned char *sha1);

/*
 * Return 0 if a reference named refname could be created without
 * conflicting with the name of an existing reference. Otherwise,
 * return a negative value and write an explanation to err. If extras
 * is non-NULL, it is a list of additional refnames with which refname
 * is not allowed to conflict. If skip is non-NULL, ignore potential
 * conflicts with refs in skip (e.g., because they are scheduled for
 * deletion in the same operation). Behavior is undefined if the same
 * name is listed in both extras and skip.
 *
 * Two reference names conflict if one of them exactly matches the
 * leading components of the other; e.g., "foo/bar" conflicts with
 * both "foo" and with "foo/bar/baz" but not with "foo/bar" or
 * "foo/barbados".
 *
 * extras and skip must be sorted.
 */

int refs_verify_refname_available(struct ref_store *refs,
				  const char *refname,
				  const struct string_list *extra,
				  const struct string_list *skip,
				  struct strbuf *err);

int ref_exists(const char *refname);

int should_autocreate_reflog(const char *refname);

int is_branch(const char *refname);

extern int refs_init_db(struct strbuf *err);

/*
 * If refname is a non-symbolic reference that refers to a tag object,
 * and the tag can be (recursively) dereferenced to a non-tag object,
 * store the SHA1 of the referred-to object to sha1 and return 0.  If
 * any of these conditions are not met, return a non-zero value.
 * Symbolic references are considered unpeelable, even if they
 * ultimately resolve to a peelable tag.
 */
int refs_peel_ref(struct ref_store *refs, const char *refname,
		  unsigned char *sha1);
int peel_ref(const char *refname, unsigned char *sha1);

/**
 * Resolve refname in the nested "gitlink" repository in the specified
 * submodule (which must be non-NULL). If the resolution is
 * successful, return 0 and set sha1 to the name of the object;
 * otherwise, return a non-zero value.
 */
int resolve_gitlink_ref(const char *submodule, const char *refname,
			unsigned char *sha1);

/*
 * Return true iff abbrev_name is a possible abbreviation for
 * full_name according to the rules defined by ref_rev_parse_rules in
 * refs.c.
 */
int refname_match(const char *abbrev_name, const char *full_name);

int expand_ref(const char *str, int len, unsigned char *sha1, char **ref);
int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref);
int dwim_log(const char *str, int len, unsigned char *sha1, char **ref);

/*
 * A ref_transaction represents a collection of ref updates
 * that should succeed or fail together.
 *
 * Calling sequence
 * ----------------
 * - Allocate and initialize a `struct ref_transaction` by calling
 *   `ref_transaction_begin()`.
 *
 * - List intended ref updates by calling functions like
 *   `ref_transaction_update()` and `ref_transaction_create()`.
 *
 * - Call `ref_transaction_commit()` to execute the transaction.
 *   If this succeeds, the ref updates will have taken place and
 *   the transaction cannot be rolled back.
 *
 * - Instead of `ref_transaction_commit`, use
 *   `initial_ref_transaction_commit()` if the ref database is known
 *   to be empty (e.g. during clone).  This is likely to be much
 *   faster.
 *
 * - At any time call `ref_transaction_free()` to discard the
 *   transaction and free associated resources.  In particular,
 *   this rolls back the transaction if it has not been
 *   successfully committed.
 *
 * Error handling
 * --------------
 *
 * On error, transaction functions append a message about what
 * went wrong to the 'err' argument.  The message mentions what
 * ref was being updated (if any) when the error occurred so it
 * can be passed to 'die' or 'error' as-is.
 *
 * The message is appended to err without first clearing err.
 * err will not be '\n' terminated.
 *
 * Caveats
 * -------
 *
 * Note that no locks are taken, and no refs are read, until
 * `ref_transaction_commit` is called.  So `ref_transaction_verify`
 * won't report a verification failure until the commit is attempted.
 */
struct ref_transaction;

/*
 * Bit values set in the flags argument passed to each_ref_fn() and
 * stored in ref_iterator::flags. Other bits are for internal use
 * only:
 */

/* Reference is a symbolic reference. */
#define REF_ISSYMREF 0x01

/* Reference is a packed reference. */
#define REF_ISPACKED 0x02

/*
 * Reference cannot be resolved to an object name: dangling symbolic
 * reference (directly or indirectly), corrupt reference file,
 * reference exists but name is bad, or symbolic reference refers to
 * ill-formatted reference name.
 */
#define REF_ISBROKEN 0x04

/*
 * Reference name is not well formed.
 *
 * See git-check-ref-format(1) for the definition of well formed ref names.
 */
#define REF_BAD_NAME 0x08

/*
 * The signature for the callback function for the for_each_*()
 * functions below.  The memory pointed to by the refname and sha1
 * arguments is only guaranteed to be valid for the duration of a
 * single callback invocation.
 */
typedef int each_ref_fn(const char *refname,
			const struct object_id *oid, int flags, void *cb_data);

/*
 * The following functions invoke the specified callback function for
 * each reference indicated.  If the function ever returns a nonzero
 * value, stop the iteration and return that value.  Please note that
 * it is not safe to modify references while an iteration is in
 * progress, unless the same callback function invocation that
 * modifies the reference also returns a nonzero value to immediately
 * stop the iteration. Returned references are sorted.
 */
int refs_head_ref(struct ref_store *refs,
		  each_ref_fn fn, void *cb_data);
int refs_for_each_ref(struct ref_store *refs,
		      each_ref_fn fn, void *cb_data);
int refs_for_each_ref_in(struct ref_store *refs, const char *prefix,
			 each_ref_fn fn, void *cb_data);
int refs_for_each_tag_ref(struct ref_store *refs,
			  each_ref_fn fn, void *cb_data);
int refs_for_each_branch_ref(struct ref_store *refs,
			     each_ref_fn fn, void *cb_data);
int refs_for_each_remote_ref(struct ref_store *refs,
			     each_ref_fn fn, void *cb_data);

int head_ref(each_ref_fn fn, void *cb_data);
int other_head_refs(each_ref_fn fn, void *cb_data);
int for_each_ref(each_ref_fn fn, void *cb_data);
int for_each_ref_in(const char *prefix, each_ref_fn fn, void *cb_data);
int for_each_fullref_in(const char *prefix, each_ref_fn fn, void *cb_data,
			unsigned int broken);
int for_each_tag_ref(each_ref_fn fn, void *cb_data);
int for_each_branch_ref(each_ref_fn fn, void *cb_data);
int for_each_remote_ref(each_ref_fn fn, void *cb_data);
int for_each_replace_ref(each_ref_fn fn, void *cb_data);
int for_each_glob_ref(each_ref_fn fn, const char *pattern, void *cb_data);
int for_each_glob_ref_in(each_ref_fn fn, const char *pattern,
			 const char *prefix, void *cb_data);

int for_each_remote_ref_submodule(const char *submodule,
				  each_ref_fn fn, void *cb_data);

int head_ref_namespaced(each_ref_fn fn, void *cb_data);
int for_each_namespaced_ref(each_ref_fn fn, void *cb_data);

/* can be used to learn about broken ref and symref */
int refs_for_each_rawref(struct ref_store *refs, each_ref_fn fn, void *cb_data);
int for_each_rawref(each_ref_fn fn, void *cb_data);

static inline const char *has_glob_specials(const char *pattern)
{
	return strpbrk(pattern, "?*[");
}

void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname);
void warn_dangling_symrefs(FILE *fp, const char *msg_fmt,
			   const struct string_list *refnames);

/*
 * Flags for controlling behaviour of pack_refs()
 * PACK_REFS_PRUNE: Prune loose refs after packing
 * PACK_REFS_ALL:   Pack _all_ refs, not just tags and already packed refs
 */
#define PACK_REFS_PRUNE 0x0001
#define PACK_REFS_ALL   0x0002

/*
 * Write a packed-refs file for the current repository.
 * flags: Combination of the above PACK_REFS_* flags.
 */
int refs_pack_refs(struct ref_store *refs, unsigned int flags);

/*
 * Flags controlling ref_transaction_update(), ref_transaction_create(), etc.
 * REF_NODEREF: act on the ref directly, instead of dereferencing
 *              symbolic references.
 *
 * Other flags are reserved for internal use.
 */
#define REF_NODEREF	0x01
#define REF_FORCE_CREATE_REFLOG 0x40

/*
 * Setup reflog before using. Fill in err and return -1 on failure.
 */
int refs_create_reflog(struct ref_store *refs, const char *refname,
		       int force_create, struct strbuf *err);
int safe_create_reflog(const char *refname, int force_create, struct strbuf *err);

/** Reads log for the value of ref during at_time. **/
int read_ref_at(const char *refname, unsigned int flags,
		unsigned long at_time, int cnt,
		unsigned char *sha1, char **msg,
		unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt);

/** Check if a particular reflog exists */
int refs_reflog_exists(struct ref_store *refs, const char *refname);
int reflog_exists(const char *refname);

/*
 * Delete the specified reference. If old_sha1 is non-NULL, then
 * verify that the current value of the reference is old_sha1 before
 * deleting it. If old_sha1 is NULL, delete the reference if it
 * exists, regardless of its old value. It is an error for old_sha1 to
 * be NULL_SHA1. flags is passed through to ref_transaction_delete().
 */
int refs_delete_ref(struct ref_store *refs, const char *msg,
		    const char *refname,
		    const unsigned char *old_sha1,
		    unsigned int flags);
int delete_ref(const char *msg, const char *refname,
	       const unsigned char *old_sha1, unsigned int flags);

/*
 * Delete the specified references. If there are any problems, emit
 * errors but attempt to keep going (i.e., the deletes are not done in
 * an all-or-nothing transaction). flags is passed through to
 * ref_transaction_delete().
 */
int refs_delete_refs(struct ref_store *refs, struct string_list *refnames,
		     unsigned int flags);
int delete_refs(struct string_list *refnames, unsigned int flags);

/** Delete a reflog */
int refs_delete_reflog(struct ref_store *refs, const char *refname);
int delete_reflog(const char *refname);

/* iterate over reflog entries */
typedef int each_reflog_ent_fn(
		struct object_id *old_oid, struct object_id *new_oid,
		const char *committer, unsigned long timestamp,
		int tz, const char *msg, void *cb_data);

int refs_for_each_reflog_ent(struct ref_store *refs, const char *refname,
			     each_reflog_ent_fn fn, void *cb_data);
int refs_for_each_reflog_ent_reverse(struct ref_store *refs,
				     const char *refname,
				     each_reflog_ent_fn fn,
				     void *cb_data);
int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn, void *cb_data);
int for_each_reflog_ent_reverse(const char *refname, each_reflog_ent_fn fn, void *cb_data);

/*
 * Calls the specified function for each reflog file until it returns nonzero,
 * and returns the value. Reflog file order is unspecified.
 */
int refs_for_each_reflog(struct ref_store *refs, each_ref_fn fn, void *cb_data);
int for_each_reflog(each_ref_fn fn, void *cb_data);

#define REFNAME_ALLOW_ONELEVEL 1
#define REFNAME_REFSPEC_PATTERN 2

/*
 * Return 0 iff refname has the correct format for a refname according
 * to the rules described in Documentation/git-check-ref-format.txt.
 * If REFNAME_ALLOW_ONELEVEL is set in flags, then accept one-level
 * reference names.  If REFNAME_REFSPEC_PATTERN is set in flags, then
 * allow a single "*" wildcard character in the refspec. No leading or
 * repeated slashes are accepted.
 */
int check_refname_format(const char *refname, int flags);

const char *prettify_refname(const char *refname);

char *shorten_unambiguous_ref(const char *refname, int strict);

/** rename ref, return 0 on success **/
int refs_rename_ref(struct ref_store *refs, const char *oldref,
		    const char *newref, const char *logmsg);
int rename_ref(const char *oldref, const char *newref, const char *logmsg);

int refs_create_symref(struct ref_store *refs, const char *refname,
		       const char *target, const char *logmsg);
int create_symref(const char *refname, const char *target, const char *logmsg);

enum action_on_err {
	UPDATE_REFS_MSG_ON_ERR,
	UPDATE_REFS_DIE_ON_ERR,
	UPDATE_REFS_QUIET_ON_ERR
};

/*
 * Begin a reference transaction.  The reference transaction must
 * be freed by calling ref_transaction_free().
 */
struct ref_transaction *ref_store_transaction_begin(struct ref_store *refs,
						    struct strbuf *err);
struct ref_transaction *ref_transaction_begin(struct strbuf *err);

/*
 * Reference transaction updates
 *
 * The following four functions add a reference check or update to a
 * ref_transaction.  They have some common similar parameters:
 *
 *     transaction -- a pointer to an open ref_transaction, obtained
 *         from ref_transaction_begin().
 *
 *     refname -- the name of the reference to be affected.
 *
 *     flags -- flags affecting the update, passed to
 *         update_ref_lock(). Can be REF_NODEREF, which means that
 *         symbolic references should not be followed.
 *
 *     msg -- a message describing the change (for the reflog).
 *
 *     err -- a strbuf for receiving a description of any error that
 *         might have occurred.
 *
 * The functions make internal copies of refname and msg, so the
 * caller retains ownership of these parameters.
 *
 * The functions return 0 on success and non-zero on failure. A
 * failure means that the transaction as a whole has failed and needs
 * to be rolled back.
 */

/*
 * Add a reference update to transaction. new_sha1 is the value that
 * the reference should have after the update, or null_sha1 if it
 * should be deleted. If new_sha1 is NULL, then the reference is not
 * changed at all. old_sha1 is the value that the reference must have
 * before the update, or null_sha1 if it must not have existed
 * beforehand. The old value is checked after the lock is taken to
 * prevent races. If the old value doesn't agree with old_sha1, the
 * whole transaction fails. If old_sha1 is NULL, then the previous
 * value is not checked.
 *
 * See the above comment "Reference transaction updates" for more
 * information.
 */
int ref_transaction_update(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *new_sha1,
			   const unsigned char *old_sha1,
			   unsigned int flags, const char *msg,
			   struct strbuf *err);

/*
 * Add a reference creation to transaction. new_sha1 is the value that
 * the reference should have after the update; it must not be
 * null_sha1. It is verified that the reference does not exist
 * already.
 *
 * See the above comment "Reference transaction updates" for more
 * information.
 */
int ref_transaction_create(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *new_sha1,
			   unsigned int flags, const char *msg,
			   struct strbuf *err);

/*
 * Add a reference deletion to transaction. If old_sha1 is non-NULL,
 * then it holds the value that the reference should have had before
 * the update (which must not be null_sha1).
 *
 * See the above comment "Reference transaction updates" for more
 * information.
 */
int ref_transaction_delete(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *old_sha1,
			   unsigned int flags, const char *msg,
			   struct strbuf *err);

/*
 * Verify, within a transaction, that refname has the value old_sha1,
 * or, if old_sha1 is null_sha1, then verify that the reference
 * doesn't exist. old_sha1 must be non-NULL.
 *
 * See the above comment "Reference transaction updates" for more
 * information.
 */
int ref_transaction_verify(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *old_sha1,
			   unsigned int flags,
			   struct strbuf *err);

/*
 * Commit all of the changes that have been queued in transaction, as
 * atomically as possible.
 *
 * Returns 0 for success, or one of the below error codes for errors.
 */
/* Naming conflict (for example, the ref names A and A/B conflict). */
#define TRANSACTION_NAME_CONFLICT -1
/* All other errors. */
#define TRANSACTION_GENERIC_ERROR -2
int ref_transaction_commit(struct ref_transaction *transaction,
			   struct strbuf *err);

/*
 * Like ref_transaction_commit(), but optimized for creating
 * references when originally initializing a repository (e.g., by "git
 * clone"). It writes the new references directly to packed-refs
 * without locking the individual references.
 *
 * It is a bug to call this function when there might be other
 * processes accessing the repository or if there are existing
 * references that might conflict with the ones being created. All
 * old_sha1 values must either be absent or NULL_SHA1.
 */
int initial_ref_transaction_commit(struct ref_transaction *transaction,
				   struct strbuf *err);

/*
 * Free an existing transaction and all associated data.
 */
void ref_transaction_free(struct ref_transaction *transaction);

/**
 * Lock, update, and unlock a single reference. This function
 * basically does a transaction containing a single call to
 * ref_transaction_update(). The parameters to this function have the
 * same meaning as the corresponding parameters to
 * ref_transaction_update(). Handle errors as requested by the `onerr`
 * argument.
 */
int refs_update_ref(struct ref_store *refs, const char *msg, const char *refname,
		    const unsigned char *new_sha1, const unsigned char *old_sha1,
		    unsigned int flags, enum action_on_err onerr);
int update_ref(const char *msg, const char *refname,
	       const unsigned char *new_sha1, const unsigned char *old_sha1,
	       unsigned int flags, enum action_on_err onerr);
int update_ref_oid(const char *msg, const char *refname,
	       const struct object_id *new_oid, const struct object_id *old_oid,
	       unsigned int flags, enum action_on_err onerr);

int parse_hide_refs_config(const char *var, const char *value, const char *);

/*
 * Check whether a ref is hidden. If no namespace is set, both the first and
 * the second parameter point to the full ref name. If a namespace is set and
 * the ref is inside that namespace, the first parameter is a pointer to the
 * name of the ref with the namespace prefix removed. If a namespace is set and
 * the ref is outside that namespace, the first parameter is NULL. The second
 * parameter always points to the full ref name.
 */
int ref_is_hidden(const char *, const char *);

enum ref_type {
	REF_TYPE_PER_WORKTREE,
	REF_TYPE_PSEUDOREF,
	REF_TYPE_NORMAL,
};

enum ref_type ref_type(const char *refname);

enum expire_reflog_flags {
	EXPIRE_REFLOGS_DRY_RUN = 1 << 0,
	EXPIRE_REFLOGS_UPDATE_REF = 1 << 1,
	EXPIRE_REFLOGS_VERBOSE = 1 << 2,
	EXPIRE_REFLOGS_REWRITE = 1 << 3
};

/*
 * The following interface is used for reflog expiration. The caller
 * calls reflog_expire(), supplying it with three callback functions,
 * of the following types. The callback functions define the
 * expiration policy that is desired.
 *
 * reflog_expiry_prepare_fn -- Called once after the reference is
 *     locked.
 *
 * reflog_expiry_should_prune_fn -- Called once for each entry in the
 *     existing reflog. It should return true iff that entry should be
 *     pruned.
 *
 * reflog_expiry_cleanup_fn -- Called once before the reference is
 *     unlocked again.
 */
typedef void reflog_expiry_prepare_fn(const char *refname,
				      const unsigned char *sha1,
				      void *cb_data);
typedef int reflog_expiry_should_prune_fn(unsigned char *osha1,
					  unsigned char *nsha1,
					  const char *email,
					  unsigned long timestamp, int tz,
					  const char *message, void *cb_data);
typedef void reflog_expiry_cleanup_fn(void *cb_data);

/*
 * Expire reflog entries for the specified reference. sha1 is the old
 * value of the reference. flags is a combination of the constants in
 * enum expire_reflog_flags. The three function pointers are described
 * above. On success, return zero.
 */
int refs_reflog_expire(struct ref_store *refs,
		       const char *refname,
		       const unsigned char *sha1,
		       unsigned int flags,
		       reflog_expiry_prepare_fn prepare_fn,
		       reflog_expiry_should_prune_fn should_prune_fn,
		       reflog_expiry_cleanup_fn cleanup_fn,
		       void *policy_cb_data);
int reflog_expire(const char *refname, const unsigned char *sha1,
		  unsigned int flags,
		  reflog_expiry_prepare_fn prepare_fn,
		  reflog_expiry_should_prune_fn should_prune_fn,
		  reflog_expiry_cleanup_fn cleanup_fn,
		  void *policy_cb_data);

int ref_storage_backend_exists(const char *name);

struct ref_store *get_main_ref_store(void);
/*
 * Return the ref_store instance for the specified submodule. For the
 * main repository, use submodule==NULL; such a call cannot fail. For
 * a submodule, the submodule must exist and be a nonbare repository,
 * otherwise return NULL. If the requested reference store has not yet
 * been initialized, initialize it first.
 *
 * For backwards compatibility, submodule=="" is treated the same as
 * submodule==NULL.
 */
struct ref_store *get_submodule_ref_store(const char *submodule);
struct ref_store *get_worktree_ref_store(const struct worktree *wt);

#endif /* REFS_H */
