#ifndef REFS_H
#define REFS_H

struct ref_lock {
	char *ref_name;
	char *orig_ref_name;
	struct lock_file *lk;
	unsigned char old_sha1[20];
	int lock_fd;
	int force_write;
};

struct ref_transaction;

/*
 * Bit values set in the flags argument passed to each_ref_fn():
 */

/* Reference is a symbolic reference. */
#define REF_ISSYMREF 0x01

/* Reference is a packed reference. */
#define REF_ISPACKED 0x02

/*
 * Reference cannot be resolved to an object name: dangling symbolic
 * reference (directly or indirectly), corrupt reference file, or
 * symbolic reference refers to ill-formatted reference name.
 */
#define REF_ISBROKEN 0x04

/*
 * The signature for the callback function for the for_each_*()
 * functions below.  The memory pointed to by the refname and sha1
 * arguments is only guaranteed to be valid for the duration of a
 * single callback invocation.
 */
typedef int each_ref_fn(const char *refname,
			const unsigned char *sha1, int flags, void *cb_data);

/*
 * The following functions invoke the specified callback function for
 * each reference indicated.  If the function ever returns a nonzero
 * value, stop the iteration and return that value.  Please note that
 * it is not safe to modify references while an iteration is in
 * progress, unless the same callback function invocation that
 * modifies the reference also returns a nonzero value to immediately
 * stop the iteration.
 */
extern int head_ref(each_ref_fn, void *);
extern int for_each_ref(each_ref_fn, void *);
extern int for_each_ref_in(const char *, each_ref_fn, void *);
extern int for_each_tag_ref(each_ref_fn, void *);
extern int for_each_branch_ref(each_ref_fn, void *);
extern int for_each_remote_ref(each_ref_fn, void *);
extern int for_each_replace_ref(each_ref_fn, void *);
extern int for_each_glob_ref(each_ref_fn, const char *pattern, void *);
extern int for_each_glob_ref_in(each_ref_fn, const char *pattern, const char* prefix, void *);

extern int head_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_ref_in_submodule(const char *submodule, const char *prefix,
		each_ref_fn fn, void *cb_data);
extern int for_each_tag_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_branch_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_remote_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);

extern int head_ref_namespaced(each_ref_fn fn, void *cb_data);
extern int for_each_namespaced_ref(each_ref_fn fn, void *cb_data);

static inline const char *has_glob_specials(const char *pattern)
{
	return strpbrk(pattern, "?*[");
}

/* can be used to learn about broken ref and symref */
extern int for_each_rawref(each_ref_fn, void *);

extern void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname);
extern void warn_dangling_symrefs(FILE *fp, const char *msg_fmt, const struct string_list* refnames);

/*
 * Lock the packed-refs file for writing.  Flags is passed to
 * hold_lock_file_for_update().  Return 0 on success.
 * Errno is set to something meaningful on error.
 */
extern int lock_packed_refs(int flags);

/*
 * Add a reference to the in-memory packed reference cache.  This may
 * only be called while the packed-refs file is locked (see
 * lock_packed_refs()).  To actually write the packed-refs file, call
 * commit_packed_refs().
 */
extern void add_packed_ref(const char *refname, const unsigned char *sha1);

/*
 * Write the current version of the packed refs cache from memory to
 * disk.  The packed-refs file must already be locked for writing (see
 * lock_packed_refs()).  Return zero on success.
 * Sets errno to something meaningful on error.
 */
extern int commit_packed_refs(void);

/*
 * Rollback the lockfile for the packed-refs file, and discard the
 * in-memory packed reference cache.  (The packed-refs file will be
 * read anew if it is needed again after this function is called.)
 */
extern void rollback_packed_refs(void);

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
int pack_refs(unsigned int flags);

extern int repack_without_refs(const char **refnames, int n,
			       struct strbuf *err);

extern int ref_exists(const char *);

extern int is_branch(const char *refname);

/*
 * If refname is a non-symbolic reference that refers to a tag object,
 * and the tag can be (recursively) dereferenced to a non-tag object,
 * store the SHA1 of the referred-to object to sha1 and return 0.  If
 * any of these conditions are not met, return a non-zero value.
 * Symbolic references are considered unpeelable, even if they
 * ultimately resolve to a peelable tag.
 */
extern int peel_ref(const char *refname, unsigned char *sha1);

/*
 * Locks a "refs/" ref returning the lock on success and NULL on failure.
 * On failure errno is set to something meaningful.
 */
extern struct ref_lock *lock_ref_sha1(const char *refname, const unsigned char *old_sha1);

/** Locks any ref (for 'HEAD' type refs). */
#define REF_NODEREF	0x01
/* errno is set to something meaningful on failure */
extern struct ref_lock *lock_any_ref_for_update(const char *refname,
						const unsigned char *old_sha1,
						int flags, int *type_p);

/** Close the file descriptor owned by a lock and return the status */
extern int close_ref(struct ref_lock *lock);

/** Close and commit the ref locked by the lock */
extern int commit_ref(struct ref_lock *lock);

/** Release any lock taken but not written. **/
extern void unlock_ref(struct ref_lock *lock);

/** Writes sha1 into the ref specified by the lock. **/
extern int write_ref_sha1(struct ref_lock *lock, const unsigned char *sha1, const char *msg);

/*
 * Setup reflog before using. Set errno to something meaningful on failure.
 */
int log_ref_setup(const char *refname, char *logfile, int bufsize);

/** Reads log for the value of ref during at_time. **/
extern int read_ref_at(const char *refname, unsigned long at_time, int cnt,
		       unsigned char *sha1, char **msg,
		       unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt);

/** Check if a particular reflog exists */
extern int reflog_exists(const char *refname);

/** Delete a reflog */
extern int delete_reflog(const char *refname);

/* iterate over reflog entries */
typedef int each_reflog_ent_fn(unsigned char *osha1, unsigned char *nsha1, const char *, unsigned long, int, const char *, void *);
int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn, void *cb_data);
int for_each_reflog_ent_reverse(const char *refname, each_reflog_ent_fn fn, void *cb_data);

/*
 * Calls the specified function for each reflog file until it returns nonzero,
 * and returns the value
 */
extern int for_each_reflog(each_ref_fn, void *);

#define REFNAME_ALLOW_ONELEVEL 1
#define REFNAME_REFSPEC_PATTERN 2
#define REFNAME_DOT_COMPONENT 4

/*
 * Return 0 iff refname has the correct format for a refname according
 * to the rules described in Documentation/git-check-ref-format.txt.
 * If REFNAME_ALLOW_ONELEVEL is set in flags, then accept one-level
 * reference names.  If REFNAME_REFSPEC_PATTERN is set in flags, then
 * allow a "*" wildcard character in place of one of the name
 * components.  No leading or repeated slashes are accepted.  If
 * REFNAME_DOT_COMPONENT is set in flags, then allow refname
 * components to start with "." (but not a whole component equal to
 * "." or "..").
 */
extern int check_refname_format(const char *refname, int flags);

extern const char *prettify_refname(const char *refname);
extern char *shorten_unambiguous_ref(const char *refname, int strict);

/** rename ref, return 0 on success **/
extern int rename_ref(const char *oldref, const char *newref, const char *logmsg);

/**
 * Resolve refname in the nested "gitlink" repository that is located
 * at path.  If the resolution is successful, return 0 and set sha1 to
 * the name of the object; otherwise, return a non-zero value.
 */
extern int resolve_gitlink_ref(const char *path, const char *refname, unsigned char *sha1);

enum action_on_err {
	UPDATE_REFS_MSG_ON_ERR,
	UPDATE_REFS_DIE_ON_ERR,
	UPDATE_REFS_QUIET_ON_ERR
};

/*
 * Begin a reference transaction.  The reference transaction must
 * be freed by calling ref_transaction_free().
 */
struct ref_transaction *ref_transaction_begin(void);

/*
 * The following functions add a reference check or update to a
 * ref_transaction.  In all of them, refname is the name of the
 * reference to be affected.  The functions make internal copies of
 * refname, so the caller retains ownership of the parameter.  flags
 * can be REF_NODEREF; it is passed to update_ref_lock().
 */

/*
 * Add a reference update to transaction.  new_sha1 is the value that
 * the reference should have after the update, or zeros if it should
 * be deleted.  If have_old is true, then old_sha1 holds the value
 * that the reference should have had before the update, or zeros if
 * it must not have existed beforehand.
 * Function returns 0 on success and non-zero on failure. A failure to update
 * means that the transaction as a whole has failed and will need to be
 * rolled back. On failure the err buffer will be updated.
 */
int ref_transaction_update(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *new_sha1,
			   const unsigned char *old_sha1,
			   int flags, int have_old,
			   struct strbuf *err);

/*
 * Add a reference creation to transaction.  new_sha1 is the value
 * that the reference should have after the update; it must not be the
 * null SHA-1.  It is verified that the reference does not exist
 * already.
 */
void ref_transaction_create(struct ref_transaction *transaction,
			    const char *refname,
			    const unsigned char *new_sha1,
			    int flags);

/*
 * Add a reference deletion to transaction.  If have_old is true, then
 * old_sha1 holds the value that the reference should have had before
 * the update (which must not be the null SHA-1).
 */
void ref_transaction_delete(struct ref_transaction *transaction,
			    const char *refname,
			    const unsigned char *old_sha1,
			    int flags, int have_old);

/*
 * Commit all of the changes that have been queued in transaction, as
 * atomically as possible.  Return a nonzero value if there is a
 * problem.
 * If err is non-NULL we will add an error string to it to explain why
 * the transaction failed. The string does not end in newline.
 */
int ref_transaction_commit(struct ref_transaction *transaction,
			   const char *msg, struct strbuf *err);

/*
 * Free an existing transaction and all associated data.
 */
void ref_transaction_free(struct ref_transaction *transaction);

/** Lock a ref and then write its file */
int update_ref(const char *action, const char *refname,
		const unsigned char *sha1, const unsigned char *oldval,
		int flags, enum action_on_err onerr);

extern int parse_hide_refs_config(const char *var, const char *value, const char *);
extern int ref_is_hidden(const char *);

#endif /* REFS_H */
