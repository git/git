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

	void *backend_data;
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

/*
 * Check whether an attempt to rename old_refname to new_refname would
 * cause a D/F conflict with any existing reference (other than
 * possibly old_refname). If there would be a conflict, emit an error
 * message and return false; otherwise, return true.
 *
 * Note that this function is not safe against all races with other
 * processes (though rename_ref() catches some races that might get by
 * this check).
 */
int rename_ref_available(const char *old_refname, const char *new_refname);

/* We allow "recursive" symbolic refs. Only within reason, though */
#define SYMREF_MAXDEPTH 5

/* Include broken references in a do_for_each_ref*() iteration: */
#define DO_FOR_EACH_INCLUDE_BROKEN 0x01

/*
 * Reference iterators
 *
 * A reference iterator encapsulates the state of an in-progress
 * iteration over references. Create an instance of `struct
 * ref_iterator` via one of the functions in this module.
 *
 * A freshly-created ref_iterator doesn't yet point at a reference. To
 * advance the iterator, call ref_iterator_advance(). If successful,
 * this sets the iterator's refname, oid, and flags fields to describe
 * the next reference and returns ITER_OK. The data pointed at by
 * refname and oid belong to the iterator; if you want to retain them
 * after calling ref_iterator_advance() again or calling
 * ref_iterator_abort(), you must make a copy. When the iteration has
 * been exhausted, ref_iterator_advance() releases any resources
 * assocated with the iteration, frees the ref_iterator object, and
 * returns ITER_DONE. If you want to abort the iteration early, call
 * ref_iterator_abort(), which also frees the ref_iterator object and
 * any associated resources. If there was an internal error advancing
 * to the next entry, ref_iterator_advance() aborts the iteration,
 * frees the ref_iterator, and returns ITER_ERROR.
 *
 * The reference currently being looked at can be peeled by calling
 * ref_iterator_peel(). This function is often faster than peel_ref(),
 * so it should be preferred when iterating over references.
 *
 * Putting it all together, a typical iteration looks like this:
 *
 *     int ok;
 *     struct ref_iterator *iter = ...;
 *
 *     while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
 *             if (want_to_stop_iteration()) {
 *                     ok = ref_iterator_abort(iter);
 *                     break;
 *             }
 *
 *             // Access information about the current reference:
 *             if (!(iter->flags & REF_ISSYMREF))
 *                     printf("%s is %s\n", iter->refname, oid_to_hex(&iter->oid));
 *
 *             // If you need to peel the reference:
 *             ref_iterator_peel(iter, &oid);
 *     }
 *
 *     if (ok != ITER_DONE)
 *             handle_error();
 */
struct ref_iterator {
	struct ref_iterator_vtable *vtable;
	const char *refname;
	const struct object_id *oid;
	unsigned int flags;
};

/*
 * Advance the iterator to the first or next item and return ITER_OK.
 * If the iteration is exhausted, free the resources associated with
 * the ref_iterator and return ITER_DONE. On errors, free the iterator
 * resources and return ITER_ERROR. It is a bug to use ref_iterator or
 * call this function again after it has returned ITER_DONE or
 * ITER_ERROR.
 */
int ref_iterator_advance(struct ref_iterator *ref_iterator);

/*
 * If possible, peel the reference currently being viewed by the
 * iterator. Return 0 on success.
 */
int ref_iterator_peel(struct ref_iterator *ref_iterator,
		      struct object_id *peeled);

/*
 * End the iteration before it has been exhausted, freeing the
 * reference iterator and any associated resources and returning
 * ITER_DONE. If the abort itself failed, return ITER_ERROR.
 */
int ref_iterator_abort(struct ref_iterator *ref_iterator);

/*
 * An iterator over nothing (its first ref_iterator_advance() call
 * returns ITER_DONE).
 */
struct ref_iterator *empty_ref_iterator_begin(void);

/*
 * Return true iff ref_iterator is an empty_ref_iterator.
 */
int is_empty_ref_iterator(struct ref_iterator *ref_iterator);

/*
 * A callback function used to instruct merge_ref_iterator how to
 * interleave the entries from iter0 and iter1. The function should
 * return one of the constants defined in enum iterator_selection. It
 * must not advance either of the iterators itself.
 *
 * The function must be prepared to handle the case that iter0 and/or
 * iter1 is NULL, which indicates that the corresponding sub-iterator
 * has been exhausted. Its return value must be consistent with the
 * current states of the iterators; e.g., it must not return
 * ITER_SKIP_1 if iter1 has already been exhausted.
 */
typedef enum iterator_selection ref_iterator_select_fn(
		struct ref_iterator *iter0, struct ref_iterator *iter1,
		void *cb_data);

/*
 * Iterate over the entries from iter0 and iter1, with the values
 * interleaved as directed by the select function. The iterator takes
 * ownership of iter0 and iter1 and frees them when the iteration is
 * over.
 */
struct ref_iterator *merge_ref_iterator_begin(
		struct ref_iterator *iter0, struct ref_iterator *iter1,
		ref_iterator_select_fn *select, void *cb_data);

/*
 * An iterator consisting of the union of the entries from front and
 * back. If there are entries common to the two sub-iterators, use the
 * one from front. Each iterator must iterate over its entries in
 * strcmp() order by refname for this to work.
 *
 * The new iterator takes ownership of its arguments and frees them
 * when the iteration is over. As a convenience to callers, if front
 * or back is an empty_ref_iterator, then abort that one immediately
 * and return the other iterator directly, without wrapping it.
 */
struct ref_iterator *overlay_ref_iterator_begin(
		struct ref_iterator *front, struct ref_iterator *back);

/*
 * Wrap iter0, only letting through the references whose names start
 * with prefix. If trim is set, set iter->refname to the name of the
 * reference with that many characters trimmed off the front;
 * otherwise set it to the full refname. The new iterator takes over
 * ownership of iter0 and frees it when iteration is over. It makes
 * its own copy of prefix.
 *
 * As an convenience to callers, if prefix is the empty string and
 * trim is zero, this function returns iter0 directly, without
 * wrapping it.
 */
struct ref_iterator *prefix_ref_iterator_begin(struct ref_iterator *iter0,
					       const char *prefix,
					       int trim);

/* Internal implementation of reference iteration: */

/*
 * Base class constructor for ref_iterators. Initialize the
 * ref_iterator part of iter, setting its vtable pointer as specified.
 * This is meant to be called only by the initializers of derived
 * classes.
 */
void base_ref_iterator_init(struct ref_iterator *iter,
			    struct ref_iterator_vtable *vtable);

/*
 * Base class destructor for ref_iterators. Destroy the ref_iterator
 * part of iter and shallow-free the object. This is meant to be
 * called only by the destructors of derived classes.
 */
void base_ref_iterator_free(struct ref_iterator *iter);

/* Virtual function declarations for ref_iterators: */

typedef int ref_iterator_advance_fn(struct ref_iterator *ref_iterator);

typedef int ref_iterator_peel_fn(struct ref_iterator *ref_iterator,
				 struct object_id *peeled);

/*
 * Implementations of this function should free any resources specific
 * to the derived class, then call base_ref_iterator_free() to clean
 * up and free the ref_iterator object.
 */
typedef int ref_iterator_abort_fn(struct ref_iterator *ref_iterator);

struct ref_iterator_vtable {
	ref_iterator_advance_fn *advance;
	ref_iterator_peel_fn *peel;
	ref_iterator_abort_fn *abort;
};

/*
 * current_ref_iter is a performance hack: when iterating over
 * references using the for_each_ref*() functions, current_ref_iter is
 * set to the reference iterator before calling the callback function.
 * If the callback function calls peel_ref(), then peel_ref() first
 * checks whether the reference to be peeled is the one referred to by
 * the iterator (it usually is) and if so, asks the iterator for the
 * peeled version of the reference if it is available. This avoids a
 * refname lookup in a common case. current_ref_iter is set to NULL
 * when the iteration is over.
 */
extern struct ref_iterator *current_ref_iter;

/*
 * The common backend for the for_each_*ref* functions. Call fn for
 * each reference in iter. If the iterator itself ever returns
 * ITER_ERROR, return -1. If fn ever returns a non-zero value, stop
 * the iteration and return that value. Otherwise, return 0. In any
 * case, free the iterator when done. This function is basically an
 * adapter between the callback style of reference iteration and the
 * iterator style.
 */
int do_for_each_ref_iterator(struct ref_iterator *iter,
			     each_ref_fn fn, void *cb_data);

/*
 * Only include per-worktree refs in a do_for_each_ref*() iteration.
 * Normally this will be used with a files ref_store, since that's
 * where all reference backends will presumably store their
 * per-worktree refs.
 */
#define DO_FOR_EACH_PER_WORKTREE_ONLY 0x02

struct ref_store;

/* refs backends */

/*
 * Initialize the ref_store for the specified submodule, or for the
 * main repository if submodule == NULL. These functions should call
 * base_ref_store_init() to initialize the shared part of the
 * ref_store and to record the ref_store for later lookup.
 */
typedef struct ref_store *ref_store_init_fn(const char *submodule);

typedef int ref_init_db_fn(struct ref_store *refs, struct strbuf *err);

typedef int ref_transaction_commit_fn(struct ref_store *refs,
				      struct ref_transaction *transaction,
				      struct strbuf *err);

typedef int pack_refs_fn(struct ref_store *ref_store, unsigned int flags);
typedef int peel_ref_fn(struct ref_store *ref_store,
			const char *refname, unsigned char *sha1);
typedef int create_symref_fn(struct ref_store *ref_store,
			     const char *ref_target,
			     const char *refs_heads_master,
			     const char *logmsg);
typedef int delete_refs_fn(struct ref_store *ref_store,
			   struct string_list *refnames, unsigned int flags);
typedef int rename_ref_fn(struct ref_store *ref_store,
			  const char *oldref, const char *newref,
			  const char *logmsg);

/*
 * Iterate over the references in the specified ref_store that are
 * within find_containing_dir(prefix). If prefix is NULL or the empty
 * string, iterate over all references in the submodule.
 */
typedef struct ref_iterator *ref_iterator_begin_fn(
		struct ref_store *ref_store,
		const char *prefix, unsigned int flags);

/* reflog functions */

/*
 * Iterate over the references in the specified ref_store that have a
 * reflog. The refs are iterated over in arbitrary order.
 */
typedef struct ref_iterator *reflog_iterator_begin_fn(
		struct ref_store *ref_store);

typedef int for_each_reflog_ent_fn(struct ref_store *ref_store,
				   const char *refname,
				   each_reflog_ent_fn fn,
				   void *cb_data);
typedef int for_each_reflog_ent_reverse_fn(struct ref_store *ref_store,
					   const char *refname,
					   each_reflog_ent_fn fn,
					   void *cb_data);
typedef int reflog_exists_fn(struct ref_store *ref_store, const char *refname);
typedef int create_reflog_fn(struct ref_store *ref_store, const char *refname,
			     int force_create, struct strbuf *err);
typedef int delete_reflog_fn(struct ref_store *ref_store, const char *refname);
typedef int reflog_expire_fn(struct ref_store *ref_store,
			     const char *refname, const unsigned char *sha1,
			     unsigned int flags,
			     reflog_expiry_prepare_fn prepare_fn,
			     reflog_expiry_should_prune_fn should_prune_fn,
			     reflog_expiry_cleanup_fn cleanup_fn,
			     void *policy_cb_data);

/*
 * Read a reference from the specified reference store, non-recursively.
 * Set type to describe the reference, and:
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
typedef int read_raw_ref_fn(struct ref_store *ref_store,
			    const char *refname, unsigned char *sha1,
			    struct strbuf *referent, unsigned int *type);

typedef int verify_refname_available_fn(struct ref_store *ref_store,
					const char *newname,
					const struct string_list *extras,
					const struct string_list *skip,
					struct strbuf *err);

struct ref_storage_be {
	struct ref_storage_be *next;
	const char *name;
	ref_store_init_fn *init;
	ref_init_db_fn *init_db;
	ref_transaction_commit_fn *transaction_commit;
	ref_transaction_commit_fn *initial_transaction_commit;

	pack_refs_fn *pack_refs;
	peel_ref_fn *peel_ref;
	create_symref_fn *create_symref;
	delete_refs_fn *delete_refs;
	rename_ref_fn *rename_ref;

	ref_iterator_begin_fn *iterator_begin;
	read_raw_ref_fn *read_raw_ref;
	verify_refname_available_fn *verify_refname_available;

	reflog_iterator_begin_fn *reflog_iterator_begin;
	for_each_reflog_ent_fn *for_each_reflog_ent;
	for_each_reflog_ent_reverse_fn *for_each_reflog_ent_reverse;
	reflog_exists_fn *reflog_exists;
	create_reflog_fn *create_reflog;
	delete_reflog_fn *delete_reflog;
	reflog_expire_fn *reflog_expire;
};

extern struct ref_storage_be refs_be_files;

/*
 * A representation of the reference store for the main repository or
 * a submodule. The ref_store instances for submodules are kept in a
 * linked list.
 */
struct ref_store {
	/* The backend describing this ref_store's storage scheme: */
	const struct ref_storage_be *be;

	/*
	 * The name of the submodule represented by this object, or
	 * the empty string if it represents the main repository's
	 * reference store:
	 */
	const char *submodule;

	/*
	 * Submodule reference store instances are stored in a linked
	 * list using this pointer.
	 */
	struct ref_store *next;
};

/*
 * Fill in the generic part of refs for the specified submodule and
 * add it to our collection of reference stores.
 */
void base_ref_store_init(struct ref_store *refs,
			 const struct ref_storage_be *be,
			 const char *submodule);

/*
 * Create, record, and return a ref_store instance for the specified
 * submodule (or the main repository if submodule is NULL).
 *
 * For backwards compatibility, submodule=="" is treated the same as
 * submodule==NULL.
 */
struct ref_store *ref_store_init(const char *submodule);

/*
 * Return the ref_store instance for the specified submodule (or the
 * main repository if submodule is NULL). If that ref_store hasn't
 * been initialized yet, return NULL.
 *
 * For backwards compatibility, submodule=="" is treated the same as
 * submodule==NULL.
 */
struct ref_store *lookup_ref_store(const char *submodule);

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
struct ref_store *get_ref_store(const char *submodule);

/*
 * Die if refs is for a submodule (i.e., not for the main repository).
 * caller is used in any necessary error messages.
 */
void assert_main_repository(struct ref_store *refs, const char *caller);

#endif /* REFS_REFS_INTERNAL_H */
