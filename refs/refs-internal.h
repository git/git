#ifndef REFS_REFS_INTERNAL_H
#define REFS_REFS_INTERNAL_H

#include "cache.h"
#include "refs.h"
#include "iterator.h"

struct ref_transaction;

/*
 * Data structures and functions for the internal use of the refs
 * module. Code outside of the refs module should use only the public
 * functions defined in "refs.h", and should *not* include this file.
 */

/*
 * The following flags can appear in `ref_update::flags`. Their
 * numerical values must not conflict with those of REF_NO_DEREF and
 * REF_FORCE_CREATE_REFLOG, which are also stored in
 * `ref_update::flags`.
 */

/*
 * The reference should be updated to new_oid.
 */
#define REF_HAVE_NEW (1 << 2)

/*
 * The current reference's value should be checked to make sure that
 * it agrees with old_oid.
 */
#define REF_HAVE_OLD (1 << 3)

/*
 * Used as a flag in ref_update::flags when we want to log a ref
 * update but not actually perform it.  This is used when a symbolic
 * ref update is split up.
 */
#define REF_LOG_ONLY (1 << 7)

/*
 * Return the length of time to retry acquiring a loose reference lock
 * before giving up, in milliseconds:
 */
long get_files_ref_lock_timeout_ms(void);

/*
 * Return true iff refname is minimally safe. "Safe" here means that
 * deleting a loose reference by this name will not do any damage, for
 * example by causing a file that is not a reference to be deleted.
 * This function does not check that the reference name is legal; for
 * that, use check_refname_format().
 *
 * A refname that starts with "refs/" is considered safe iff it
 * doesn't contain any "." or ".." components or consecutive '/'
 * characters, end with '/', or (on Windows) contain any '\'
 * characters. Names that do not start with "refs/" are considered
 * safe iff they consist entirely of upper case characters and '_'
 * (like "HEAD" and "MERGE_HEAD" but not "config" or "FOO/BAR").
 */
int refname_is_safe(const char *refname);

/*
 * Helper function: return true if refname, which has the specified
 * oid and flags, can be resolved to an object in the database. If the
 * referred-to object does not exist, emit a warning and return false.
 */
int ref_resolves_to_object(const char *refname,
			   const struct object_id *oid,
			   unsigned int flags);

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
 * result to oid and return PEEL_PEELED.  If the object is not a tag
 * or is not valid, return PEEL_NON_TAG or PEEL_INVALID, respectively,
 * and leave oid unchanged.
 */
enum peel_status peel_object(const struct object_id *name, struct object_id *oid);

/**
 * Information needed for a single ref update. Set new_oid to the new
 * value or to null_oid to delete the ref. To check the old value
 * while the ref is locked, set (flags & REF_HAVE_OLD) and set old_oid
 * to the old value, or to null_oid to ensure the ref does not exist
 * before update.
 */
struct ref_update {
	/*
	 * If (flags & REF_HAVE_NEW), set the reference to this value
	 * (or delete it, if `new_oid` is `null_oid`).
	 */
	struct object_id new_oid;

	/*
	 * If (flags & REF_HAVE_OLD), check that the reference
	 * previously had this value (or didn't previously exist, if
	 * `old_oid` is `null_oid`).
	 */
	struct object_id old_oid;

	/*
	 * One or more of REF_NO_DEREF, REF_FORCE_CREATE_REFLOG,
	 * REF_HAVE_NEW, REF_HAVE_OLD, or backend-specific flags.
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

int refs_read_raw_ref(struct ref_store *ref_store,
		      const char *refname, struct object_id *oid,
		      struct strbuf *referent, unsigned int *type);

/*
 * Write an error to `err` and return a nonzero value iff the same
 * refname appears multiple times in `refnames`. `refnames` must be
 * sorted on entry to this function.
 */
int ref_update_reject_duplicates(struct string_list *refnames,
				 struct strbuf *err);

/*
 * Add a ref_update with the specified properties to transaction, and
 * return a pointer to the new object. This function does not verify
 * that refname is well-formed. new_oid and old_oid are only
 * dereferenced if the REF_HAVE_NEW and REF_HAVE_OLD bits,
 * respectively, are set in flags.
 */
struct ref_update *ref_transaction_add_update(
		struct ref_transaction *transaction,
		const char *refname, unsigned int flags,
		const struct object_id *new_oid,
		const struct object_id *old_oid,
		const char *msg);

/*
 * Transaction states.
 *
 * OPEN:   The transaction is initialized and new updates can still be
 *         added to it. An OPEN transaction can be prepared,
 *         committed, freed, or aborted (freeing and aborting an open
 *         transaction are equivalent).
 *
 * PREPARED: ref_transaction_prepare(), which locks all of the
 *         references involved in the update and checks that the
 *         update has no errors, has been called successfully for the
 *         transaction. A PREPARED transaction can be committed or
 *         aborted.
 *
 * CLOSED: The transaction is no longer active. A transaction becomes
 *         CLOSED if there is a failure while building the transaction
 *         or if a transaction is committed or aborted. A CLOSED
 *         transaction can only be freed.
 */
enum ref_transaction_state {
	REF_TRANSACTION_OPEN     = 0,
	REF_TRANSACTION_PREPARED = 1,
	REF_TRANSACTION_CLOSED   = 2
};

/*
 * Data structure for holding a reference transaction, which can
 * consist of checks and updates to multiple references, carried out
 * as atomically as possible.  This structure is opaque to callers.
 */
struct ref_transaction {
	struct ref_store *ref_store;
	struct ref_update **updates;
	size_t alloc;
	size_t nr;
	enum ref_transaction_state state;
	void *backend_data;
};

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
int refs_rename_ref_available(struct ref_store *refs,
			      const char *old_refname,
			      const char *new_refname);

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
 * associated with the iteration, frees the ref_iterator object, and
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
 *                     printf("%s is %s\n", iter->refname, oid_to_hex(iter->oid));
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

	/*
	 * Does this `ref_iterator` iterate over references in order
	 * by refname?
	 */
	unsigned int ordered : 1;

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
 * Return an iterator that goes over each reference in `refs` for
 * which the refname begins with prefix. If trim is non-zero, then
 * trim that many characters off the beginning of each refname.
 * The output is ordered by refname. The following flags are supported:
 *
 * DO_FOR_EACH_INCLUDE_BROKEN: include broken references in
 *         the iteration.
 *
 * DO_FOR_EACH_PER_WORKTREE_ONLY: only produce REF_TYPE_PER_WORKTREE refs.
 */
struct ref_iterator *refs_ref_iterator_begin(
		struct ref_store *refs,
		const char *prefix, int trim, int flags);

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
 * over. A derived class should set `ordered` to 1 or 0 based on
 * whether it generates its output in order by reference name.
 */
struct ref_iterator *merge_ref_iterator_begin(
		int ordered,
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
 *
 * The resulting ref_iterator is ordered if iter0 is.
 */
struct ref_iterator *prefix_ref_iterator_begin(struct ref_iterator *iter0,
					       const char *prefix,
					       int trim);

/* Internal implementation of reference iteration: */

/*
 * Base class constructor for ref_iterators. Initialize the
 * ref_iterator part of iter, setting its vtable pointer as specified.
 * `ordered` should be set to 1 if the iterator will iterate over
 * references in order by refname; otherwise it should be set to 0.
 * This is meant to be called only by the initializers of derived
 * classes.
 */
void base_ref_iterator_init(struct ref_iterator *iter,
			    struct ref_iterator_vtable *vtable,
			    int ordered);

/*
 * Base class destructor for ref_iterators. Destroy the ref_iterator
 * part of iter and shallow-free the object. This is meant to be
 * called only by the destructors of derived classes.
 */
void base_ref_iterator_free(struct ref_iterator *iter);

/* Virtual function declarations for ref_iterators: */

/*
 * backend-specific implementation of ref_iterator_advance. For symrefs, the
 * function should set REF_ISSYMREF, and it should also dereference the symref
 * to provide the OID referent. If DO_FOR_EACH_INCLUDE_BROKEN is set, symrefs
 * with non-existent referents and refs pointing to non-existent object names
 * should also be returned. If DO_FOR_EACH_PER_WORKTREE_ONLY, only
 * REF_TYPE_PER_WORKTREE refs should be returned.
 */
typedef int ref_iterator_advance_fn(struct ref_iterator *ref_iterator);

/*
 * Peels the current ref, returning 0 for success or -1 for failure.
 */
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
int do_for_each_repo_ref_iterator(struct repository *r,
				  struct ref_iterator *iter,
				  each_repo_ref_fn fn, void *cb_data);

/*
 * Only include per-worktree refs in a do_for_each_ref*() iteration.
 * Normally this will be used with a files ref_store, since that's
 * where all reference backends will presumably store their
 * per-worktree refs.
 */
#define DO_FOR_EACH_PER_WORKTREE_ONLY 0x02

struct ref_store;

/* refs backends */

/* ref_store_init flags */
#define REF_STORE_READ		(1 << 0)
#define REF_STORE_WRITE		(1 << 1) /* can perform update operations */
#define REF_STORE_ODB		(1 << 2) /* has access to object database */
#define REF_STORE_MAIN		(1 << 3)
#define REF_STORE_ALL_CAPS	(REF_STORE_READ | \
				 REF_STORE_WRITE | \
				 REF_STORE_ODB | \
				 REF_STORE_MAIN)

/*
 * Initialize the ref_store for the specified gitdir. These functions
 * should call base_ref_store_init() to initialize the shared part of
 * the ref_store and to record the ref_store for later lookup.
 */
typedef struct ref_store *ref_store_init_fn(const char *gitdir,
					    unsigned int flags);

typedef int ref_init_db_fn(struct ref_store *refs, struct strbuf *err);

typedef int ref_transaction_prepare_fn(struct ref_store *refs,
				       struct ref_transaction *transaction,
				       struct strbuf *err);

typedef int ref_transaction_finish_fn(struct ref_store *refs,
				      struct ref_transaction *transaction,
				      struct strbuf *err);

typedef int ref_transaction_abort_fn(struct ref_store *refs,
				     struct ref_transaction *transaction,
				     struct strbuf *err);

typedef int ref_transaction_commit_fn(struct ref_store *refs,
				      struct ref_transaction *transaction,
				      struct strbuf *err);

typedef int pack_refs_fn(struct ref_store *ref_store, unsigned int flags);
typedef int create_symref_fn(struct ref_store *ref_store,
			     const char *ref_target,
			     const char *refs_heads_master,
			     const char *logmsg);
typedef int delete_refs_fn(struct ref_store *ref_store, const char *msg,
			   struct string_list *refnames, unsigned int flags);
typedef int rename_ref_fn(struct ref_store *ref_store,
			  const char *oldref, const char *newref,
			  const char *logmsg);
typedef int copy_ref_fn(struct ref_store *ref_store,
			  const char *oldref, const char *newref,
			  const char *logmsg);

/*
 * Iterate over the references in `ref_store` whose names start with
 * `prefix`. `prefix` is matched as a literal string, without regard
 * for path separators. If prefix is NULL or the empty string, iterate
 * over all references in `ref_store`. The output is ordered by
 * refname.
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
			     const char *refname, const struct object_id *oid,
			     unsigned int flags,
			     reflog_expiry_prepare_fn prepare_fn,
			     reflog_expiry_should_prune_fn should_prune_fn,
			     reflog_expiry_cleanup_fn cleanup_fn,
			     void *policy_cb_data);

/*
 * Read a reference from the specified reference store, non-recursively.
 * Set type to describe the reference, and:
 *
 * - If refname is the name of a normal reference, fill in oid
 *   (leaving referent unchanged).
 *
 * - If refname is the name of a symbolic reference, write the full
 *   name of the reference to which it refers (e.g.
 *   "refs/heads/master") to referent and set the REF_ISSYMREF bit in
 *   type (leaving oid unchanged). The caller is responsible for
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
 * an object ID, it is broken; set REF_ISBROKEN in type, set errno to
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
			    const char *refname, struct object_id *oid,
			    struct strbuf *referent, unsigned int *type);

struct ref_storage_be {
	struct ref_storage_be *next;
	const char *name;
	ref_store_init_fn *init;
	ref_init_db_fn *init_db;

	ref_transaction_prepare_fn *transaction_prepare;
	ref_transaction_finish_fn *transaction_finish;
	ref_transaction_abort_fn *transaction_abort;
	ref_transaction_commit_fn *initial_transaction_commit;

	pack_refs_fn *pack_refs;
	create_symref_fn *create_symref;
	delete_refs_fn *delete_refs;
	rename_ref_fn *rename_ref;
	copy_ref_fn *copy_ref;

	ref_iterator_begin_fn *iterator_begin;
	read_raw_ref_fn *read_raw_ref;

	reflog_iterator_begin_fn *reflog_iterator_begin;
	for_each_reflog_ent_fn *for_each_reflog_ent;
	for_each_reflog_ent_reverse_fn *for_each_reflog_ent_reverse;
	reflog_exists_fn *reflog_exists;
	create_reflog_fn *create_reflog;
	delete_reflog_fn *delete_reflog;
	reflog_expire_fn *reflog_expire;
};

extern struct ref_storage_be refs_be_files;
extern struct ref_storage_be refs_be_packed;

/*
 * A representation of the reference store for the main repository or
 * a submodule. The ref_store instances for submodules are kept in a
 * hash map; see get_submodule_ref_store() for more info.
 */
struct ref_store {
	/* The backend describing this ref_store's storage scheme: */
	const struct ref_storage_be *be;

	/* The gitdir that this ref_store applies to: */
	char *gitdir;
};

/*
 * Parse contents of a loose ref file.
 */
int parse_loose_ref_contents(const char *buf, struct object_id *oid,
			     struct strbuf *referent, unsigned int *type);

/*
 * Fill in the generic part of refs and add it to our collection of
 * reference stores.
 */
void base_ref_store_init(struct ref_store *refs,
			 const struct ref_storage_be *be);

/*
 * Support GIT_TRACE_REFS by optionally wrapping the given ref_store instance.
 */
struct ref_store *maybe_debug_wrap_ref_store(const char *gitdir, struct ref_store *store);

#endif /* REFS_REFS_INTERNAL_H */
