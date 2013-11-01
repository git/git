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

/**
 * Information needed for a single ref update.  Set new_sha1 to the
 * new value or to zero to delete the ref.  To check the old value
 * while locking the ref, set have_old to 1 and set old_sha1 to the
 * value or to zero to ensure the ref does not exist before update.
 */
struct ref_update {
	const char *ref_name;
	unsigned char new_sha1[20];
	unsigned char old_sha1[20];
	int flags; /* REF_NODEREF? */
	int have_old; /* 1 if old_sha1 is valid, 0 otherwise */
};

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

/*
 * Lock the packed-refs file for writing.  Flags is passed to
 * hold_lock_file_for_update().  Return 0 on success.
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

extern int ref_exists(const char *);

/*
 * If refname is a non-symbolic reference that refers to a tag object,
 * and the tag can be (recursively) dereferenced to a non-tag object,
 * store the SHA1 of the referred-to object to sha1 and return 0.  If
 * any of these conditions are not met, return a non-zero value.
 * Symbolic references are considered unpeelable, even if they
 * ultimately resolve to a peelable tag.
 */
extern int peel_ref(const char *refname, unsigned char *sha1);

/** Locks a "refs/" ref returning the lock on success and NULL on failure. **/
extern struct ref_lock *lock_ref_sha1(const char *refname, const unsigned char *old_sha1);

/** Locks any ref (for 'HEAD' type refs). */
#define REF_NODEREF	0x01
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

/** Setup reflog before using. **/
int log_ref_setup(const char *ref_name, char *logfile, int bufsize);

/** Reads log for the value of ref during at_time. **/
extern int read_ref_at(const char *refname, unsigned long at_time, int cnt,
		       unsigned char *sha1, char **msg,
		       unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt);

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

/** lock a ref and then write its file */
enum action_on_err { MSG_ON_ERR, DIE_ON_ERR, QUIET_ON_ERR };
int update_ref(const char *action, const char *refname,
		const unsigned char *sha1, const unsigned char *oldval,
		int flags, enum action_on_err onerr);

/**
 * Lock all refs and then perform all modifications.
 */
int update_refs(const char *action, const struct ref_update **updates,
		int n, enum action_on_err onerr);

extern int parse_hide_refs_config(const char *var, const char *value, const char *);
extern int ref_is_hidden(const char *);

#endif /* REFS_H */
