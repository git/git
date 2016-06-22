#ifndef REFS_REFS_INTERNAL_H
#define REFS_REFS_INTERNAL_H

/*
 * Data structures and functions for the internal use of the refs
 * module. Code outside of the refs module should use only the public
 * functions defined in "refs.h", and should *not* include this file.
 */

/*
 * Flag passed to lock_ref_sha1_basic() telling it to tolerate broken
 * refs (i.e., because the reference is about to be deleted anyway).
 */
#define REF_DELETING	0x02

/*
 * Used as a flag in ref_update::flags when a loose ref is being
 * pruned. This flag must only be used when REF_NODEREF is set.
 */
#define REF_ISPRUNING	0x04

/*
 * Used as a flag in ref_update::flags when the reference should be
 * updated to new_sha1.
 */
#define REF_HAVE_NEW	0x08

/*
 * Used as a flag in ref_update::flags when old_sha1 should be
 * checked.
 */
#define REF_HAVE_OLD	0x10

/*
 * Used as a flag in ref_update::flags when the lockfile needs to be
 * committed.
 */
#define REF_NEEDS_COMMIT 0x20

/*
 * 0x40 is REF_FORCE_CREATE_REFLOG, so skip it if you're adding a
 * value to ref_update::flags
 */

/*
 * Used as a flag in ref_update::flags when we want to log a ref
 * update but not actually perform it.  This is used when a symbolic
 * ref update is split up.
 */
#define REF_LOG_ONLY 0x80

/*
 * Internal flag, meaning that the containing ref_update was via an
 * update to HEAD.
 */
#define REF_UPDATE_VIA_HEAD 0x100

/*
 * Return true iff refname is minimally safe. "Safe" here means that
 * deleting a loose reference by this name will not do any damage, for
 * example by causing a file that is not a reference to be deleted.
 * This function does not check that the reference name is legal; for
 * that, use check_refname_format().
 *
 * We consider a refname that starts with "refs/" to be safe as long
 * as any ".." components that it might contain do not escape "refs/".
 * Names that do not start with "refs/" are considered safe iff they
 * consist entirely of upper case characters and '_' (like "HEAD" and
 * "MERGE_HEAD" but not "config" or "FOO/BAR").
 */
int refname_is_safe(const char *refname);

enum peel_status {
	/* object was peeled successfully: */
	PEEL_PEELED = 0,

	/*
	 * object cannot be peeled because the named object (or an
	 * object referred to by a tag in the peel chain), does not
	 * exist.
	 */
	PEEL_INVALID = -1,

	/* object cannot be peeled because it is not a tag: */
	PEEL_NON_TAG = -2,

	/* ref_entry contains no peeled value because it is a symref: */
	PEEL_IS_SYMREF = -3,

	/*
	 * ref_entry cannot be peeled because it is broken (i.e., the
	 * symbolic reference cannot even be resolved to an object
	 * name):
	 */
	PEEL_BROKEN = -4
};

/*
 * Peel the named object; i.e., if the object is a tag, resolve the
 * tag recursively until a non-tag is found.  If successful, store the
 * result to sha1 and return PEEL_PEELED.  If the object is not a tag
 * or is not valid, return PEEL_NON_TAG or PEEL_INVALID, respectively,
 * and leave sha1 unchanged.
 */
enum peel_status peel_object(const unsigned char *name, unsigned char *sha1);

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
int verify_refname_available(const char *newname,
			     const struct string_list *extras,
			     const struct string_list *skip,
			     struct strbuf *err);

/*
 * Copy the reflog message msg to buf, which has been allocated sufficiently
 * large, while cleaning up the whitespaces.  Especially, convert LF to space,
 * because reflog file is one line per entry.
 */
int copy_reflog_msg(char *buf, const char *msg);

int should_autocreate_reflog(const char *refname);

/**
 * Information needed for a single ref update. Set new_sha1 to the new
 * value or to null_sha1 to delete the ref. To check the old value
 * while the ref is locked, set (flags & REF_HAVE_OLD) and set
 * old_sha1 to the old value, or to null_sha1 to ensure the ref does
 * not exist before update.
 */
struct ref_update {

	/*
	 * If (flags & REF_HAVE_NEW), set the reference to this value:
	 */
	unsigned char new_sha1[20];

	/*
	 * If (flags & REF_HAVE_OLD), check that the reference
	 * previously had this value:
	 */
	unsigned char old_sha1[20];

	/*
	 * One or more of REF_HAVE_NEW, REF_HAVE_OLD, REF_NODEREF,
	 * REF_DELETING, REF_ISPRUNING, REF_LOG_ONLY, and
	 * REF_UPDATE_VIA_HEAD:
	 */
	unsigned int flags;

	struct ref_lock *lock;
	unsigned int type;
	char *msg;

	/*
	 * If this ref_update was split off of a symref update via
	 * split_symref_update(), then this member points at that
	 * update. This is used for two purposes:
	 * 1. When reporting errors, we report the refname under which
	 *    the update was originally requested.
	 * 2. When we read the old value of this reference, we
	 *    propagate it back to its parent update for recording in
	 *    the latter's reflog.
	 */
	struct ref_update *parent_update;

	const char refname[FLEX_ARRAY];
};

/*
 * Add a ref_update with the specified properties to transaction, and
 * return a pointer to the new object. This function does not verify
 * that refname is well-formed. new_sha1 and old_sha1 are only
 * dereferenced if the REF_HAVE_NEW and REF_HAVE_OLD bits,
 * respectively, are set in flags.
 */
struct ref_update *ref_transaction_add_update(
		struct ref_transaction *transaction,
		const char *refname, unsigned int flags,
		const unsigned char *new_sha1,
		const unsigned char *old_sha1,
		const char *msg);

/*
 * Transaction states.
 * OPEN:   The transaction is in a valid state and can accept new updates.
 *         An OPEN transaction can be committed.
 * CLOSED: A closed transaction is no longer active and no other operations
 *         than free can be used on it in this state.
 *         A transaction can either become closed by successfully committing
 *         an active transaction or if there is a failure while building
 *         the transaction thus rendering it failed/inactive.
 */
enum ref_transaction_state {
	REF_TRANSACTION_OPEN   = 0,
	REF_TRANSACTION_CLOSED = 1
};

/*
 * Data structure for holding a reference transaction, which can
 * consist of checks and updates to multiple references, carried out
 * as atomically as possible.  This structure is opaque to callers.
 */
struct ref_transaction {
	struct ref_update **updates;
	size_t alloc;
	size_t nr;
	enum ref_transaction_state state;
};

int files_log_ref_write(const char *refname, const unsigned char *old_sha1,
			const unsigned char *new_sha1, const char *msg,
			int flags, struct strbuf *err);

/*
 * Check for entries in extras that are within the specified
 * directory, where dirname is a reference directory name including
 * the trailing slash (e.g., "refs/heads/foo/"). Ignore any
 * conflicting references that are found in skip. If there is a
 * conflicting reference, return its name.
 *
 * extras and skip must be sorted lists of reference names. Either one
 * can be NULL, signifying the empty list.
 */
const char *find_descendant_ref(const char *dirname,
				const struct string_list *extras,
				const struct string_list *skip);

int rename_ref_available(const char *oldname, const char *newname);

/* We allow "recursive" symbolic refs. Only within reason, though */
#define SYMREF_MAXDEPTH 5

/* Include broken references in a do_for_each_ref*() iteration: */
#define DO_FOR_EACH_INCLUDE_BROKEN 0x01

/*
 * The common backend for the for_each_*ref* functions
 */
int do_for_each_ref(const char *submodule, const char *base,
		    each_ref_fn fn, int trim, int flags, void *cb_data);

/*
 * Read the specified reference from the filesystem or packed refs
 * file, non-recursively. Set type to describe the reference, and:
 *
 * - If refname is the name of a normal reference, fill in sha1
 *   (leaving referent unchanged).
 *
 * - If refname is the name of a symbolic reference, write the full
 *   name of the reference to which it refers (e.g.
 *   "refs/heads/master") to referent and set the REF_ISSYMREF bit in
 *   type (leaving sha1 unchanged). The caller is responsible for
 *   validating that referent is a valid reference name.
 *
 * WARNING: refname might be used as part of a filename, so it is
 * important from a security standpoint that it be safe in the sense
 * of refname_is_safe(). Moreover, for symrefs this function sets
 * referent to whatever the repository says, which might not be a
 * properly-formatted or even safe reference name. NEITHER INPUT NOR
 * OUTPUT REFERENCE NAMES ARE VALIDATED WITHIN THIS FUNCTION.
 *
 * Return 0 on success. If the ref doesn't exist, set errno to ENOENT
 * and return -1. If the ref exists but is neither a symbolic ref nor
 * a sha1, it is broken; set REF_ISBROKEN in type, set errno to
 * EINVAL, and return -1. If there is another error reading the ref,
 * set errno appropriately and return -1.
 *
 * Backend-specific flags might be set in type as well, regardless of
 * outcome.
 *
 * It is OK for refname to point into referent. If so:
 *
 * - if the function succeeds with REF_ISSYMREF, referent will be
 *   overwritten and the memory formerly pointed to by it might be
 *   changed or even freed.
 *
 * - in all other cases, referent will be untouched, and therefore
 *   refname will still be valid and unchanged.
 */
int read_raw_ref(const char *refname, unsigned char *sha1,
		 struct strbuf *referent, unsigned int *type);

#endif /* REFS_REFS_INTERNAL_H */
