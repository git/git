#ifndef REFS_REF_CACHE_H
#define REFS_REF_CACHE_H

struct ref_dir;

/*
 * If this ref_cache is filled lazily, this function is used to load
 * information into the specified ref_dir (shallow or deep, at the
 * option of the ref_store). dirname includes a trailing slash.
 */
typedef void fill_ref_dir_fn(struct ref_store *ref_store,
			     struct ref_dir *dir, const char *dirname);

struct ref_cache {
	struct ref_entry *root;

	/* A pointer to the ref_store whose cache this is: */
	struct ref_store *ref_store;

	/*
	 * Function used (if necessary) to lazily-fill cache. May be
	 * NULL.
	 */
	fill_ref_dir_fn *fill_ref_dir;
};

/*
 * Information used (along with the information in ref_entry) to
 * describe a single cached reference.  This data structure only
 * occurs embedded in a union in struct ref_entry, and only when
 * (ref_entry->flag & REF_DIR) is zero.
 */
struct ref_value {
	/*
	 * The name of the object to which this reference resolves
	 * (which may be a tag object).  If REF_ISBROKEN, this is
	 * null.  If REF_ISSYMREF, then this is the name of the object
	 * referred to by the last reference in the symlink chain.
	 */
	struct object_id oid;

	/*
	 * If REF_KNOWS_PEELED, then this field holds the peeled value
	 * of this reference, or null if the reference is known not to
	 * be peelable.  See the documentation for peel_ref() for an
	 * exact definition of "peelable".
	 */
	struct object_id peeled;
};

/*
 * Information used (along with the information in ref_entry) to
 * describe a level in the hierarchy of references.  This data
 * structure only occurs embedded in a union in struct ref_entry, and
 * only when (ref_entry.flag & REF_DIR) is set.  In that case,
 * (ref_entry.flag & REF_INCOMPLETE) determines whether the references
 * in the directory have already been read:
 *
 *     (ref_entry.flag & REF_INCOMPLETE) unset -- a directory of loose
 *         or packed references, already read.
 *
 *     (ref_entry.flag & REF_INCOMPLETE) set -- a directory of loose
 *         references that hasn't been read yet (nor has any of its
 *         subdirectories).
 *
 * Entries within a directory are stored within a growable array of
 * pointers to ref_entries (entries, nr, alloc).  Entries 0 <= i <
 * sorted are sorted by their component name in strcmp() order and the
 * remaining entries are unsorted.
 *
 * Loose references are read lazily, one directory at a time.  When a
 * directory of loose references is read, then all of the references
 * in that directory are stored, and REF_INCOMPLETE stubs are created
 * for any subdirectories, but the subdirectories themselves are not
 * read.  The reading is triggered by get_ref_dir().
 */
struct ref_dir {
	int nr, alloc;

	/*
	 * Entries with index 0 <= i < sorted are sorted by name.  New
	 * entries are appended to the list unsorted, and are sorted
	 * only when required; thus we avoid the need to sort the list
	 * after the addition of every reference.
	 */
	int sorted;

	/* The ref_cache containing this entry: */
	struct ref_cache *cache;

	struct ref_entry **entries;
};

/*
 * Bit values for ref_entry::flag.  REF_ISSYMREF=0x01,
 * REF_ISPACKED=0x02, REF_ISBROKEN=0x04 and REF_BAD_NAME=0x08 are
 * public values; see refs.h.
 */

/*
 * The field ref_entry->u.value.peeled of this value entry contains
 * the correct peeled value for the reference, which might be
 * null_sha1 if the reference is not a tag or if it is broken.
 */
#define REF_KNOWS_PEELED 0x10

/* ref_entry represents a directory of references */
#define REF_DIR 0x20

/*
 * Entry has not yet been read from disk (used only for REF_DIR
 * entries representing loose references)
 */
#define REF_INCOMPLETE 0x40

/*
 * A ref_entry represents either a reference or a "subdirectory" of
 * references.
 *
 * Each directory in the reference namespace is represented by a
 * ref_entry with (flags & REF_DIR) set and containing a subdir member
 * that holds the entries in that directory that have been read so
 * far.  If (flags & REF_INCOMPLETE) is set, then the directory and
 * its subdirectories haven't been read yet.  REF_INCOMPLETE is only
 * used for loose reference directories.
 *
 * References are represented by a ref_entry with (flags & REF_DIR)
 * unset and a value member that describes the reference's value.  The
 * flag member is at the ref_entry level, but it is also needed to
 * interpret the contents of the value field (in other words, a
 * ref_value object is not very much use without the enclosing
 * ref_entry).
 *
 * Reference names cannot end with slash and directories' names are
 * always stored with a trailing slash (except for the top-level
 * directory, which is always denoted by "").  This has two nice
 * consequences: (1) when the entries in each subdir are sorted
 * lexicographically by name (as they usually are), the references in
 * a whole tree can be generated in lexicographic order by traversing
 * the tree in left-to-right, depth-first order; (2) the names of
 * references and subdirectories cannot conflict, and therefore the
 * presence of an empty subdirectory does not block the creation of a
 * similarly-named reference.  (The fact that reference names with the
 * same leading components can conflict *with each other* is a
 * separate issue that is regulated by refs_verify_refname_available().)
 *
 * Please note that the name field contains the fully-qualified
 * reference (or subdirectory) name.  Space could be saved by only
 * storing the relative names.  But that would require the full names
 * to be generated on the fly when iterating in do_for_each_ref(), and
 * would break callback functions, who have always been able to assume
 * that the name strings that they are passed will not be freed during
 * the iteration.
 */
struct ref_entry {
	unsigned char flag; /* ISSYMREF? ISPACKED? */
	union {
		struct ref_value value; /* if not (flags&REF_DIR) */
		struct ref_dir subdir; /* if (flags&REF_DIR) */
	} u;
	/*
	 * The full name of the reference (e.g., "refs/heads/master")
	 * or the full name of the directory with a trailing slash
	 * (e.g., "refs/heads/"):
	 */
	char name[FLEX_ARRAY];
};

/*
 * Return the index of the entry with the given refname from the
 * ref_dir (non-recursively), sorting dir if necessary.  Return -1 if
 * no such entry is found.  dir must already be complete.
 */
int search_ref_dir(struct ref_dir *dir, const char *refname, size_t len);

struct ref_dir *get_ref_dir(struct ref_entry *entry);

/*
 * Create a struct ref_entry object for the specified dirname.
 * dirname is the name of the directory with a trailing slash (e.g.,
 * "refs/heads/") or "" for the top-level directory.
 */
struct ref_entry *create_dir_entry(struct ref_cache *cache,
				   const char *dirname, size_t len,
				   int incomplete);

struct ref_entry *create_ref_entry(const char *refname,
				   const unsigned char *sha1, int flag,
				   int check_name);

/*
 * Return a pointer to a new `ref_cache`. Its top-level starts out
 * marked incomplete. If `fill_ref_dir` is non-NULL, it is the
 * function called to fill in incomplete directories in the
 * `ref_cache` when they are accessed. If it is NULL, then the whole
 * `ref_cache` must be filled (including clearing its directories'
 * `REF_INCOMPLETE` bits) before it is used.
 */
struct ref_cache *create_ref_cache(struct ref_store *refs,
				   fill_ref_dir_fn *fill_ref_dir);

/*
 * Free the `ref_cache` and all of its associated data.
 */
void free_ref_cache(struct ref_cache *cache);

/*
 * Add a ref_entry to the end of dir (unsorted).  Entry is always
 * stored directly in dir; no recursion into subdirectories is
 * done.
 */
void add_entry_to_dir(struct ref_dir *dir, struct ref_entry *entry);

/*
 * Remove the entry with the given name from dir, recursing into
 * subdirectories as necessary.  If refname is the name of a directory
 * (i.e., ends with '/'), then remove the directory and its contents.
 * If the removal was successful, return the number of entries
 * remaining in the directory entry that contained the deleted entry.
 * If the name was not found, return -1.  Please note that this
 * function only deletes the entry from the cache; it does not delete
 * it from the filesystem or ensure that other cache entries (which
 * might be symbolic references to the removed entry) are updated.
 * Nor does it remove any containing dir entries that might be made
 * empty by the removal.  dir must represent the top-level directory
 * and must already be complete.
 */
int remove_entry_from_dir(struct ref_dir *dir, const char *refname);

/*
 * Add a ref_entry to the ref_dir (unsorted), recursing into
 * subdirectories as necessary.  dir must represent the top-level
 * directory.  Return 0 on success.
 */
int add_ref_entry(struct ref_dir *dir, struct ref_entry *ref);

/*
 * Find the value entry with the given name in dir, sorting ref_dirs
 * and recursing into subdirectories as necessary.  If the name is not
 * found or it corresponds to a directory entry, return NULL.
 */
struct ref_entry *find_ref_entry(struct ref_dir *dir, const char *refname);

/*
 * Start iterating over references in `cache`. If `prefix` is
 * specified, only include references whose names start with that
 * prefix. If `prime_dir` is true, then fill any incomplete
 * directories before beginning the iteration.
 */
struct ref_iterator *cache_ref_iterator_begin(struct ref_cache *cache,
					      const char *prefix,
					      int prime_dir);

typedef int each_ref_entry_fn(struct ref_entry *entry, void *cb_data);

/*
 * Call `fn` for each reference in `dir`. Recurse into subdirectories,
 * sorting them before iterating. This function does not sort `dir`
 * itself; it should be sorted beforehand. `fn` is called for all
 * references, including broken ones.
 */
int do_for_each_entry_in_dir(struct ref_dir *dir,
			     each_ref_entry_fn fn, void *cb_data);

/*
 * Peel the entry (if possible) and return its new peel_status.  If
 * repeel is true, re-peel the entry even if there is an old peeled
 * value that is already stored in it.
 *
 * It is OK to call this function with a packed reference entry that
 * might be stale and might even refer to an object that has since
 * been garbage-collected.  In such a case, if the entry has
 * REF_KNOWS_PEELED then leave the status unchanged and return
 * PEEL_PEELED or PEEL_NON_TAG; otherwise, return PEEL_INVALID.
 */
enum peel_status peel_entry(struct ref_entry *entry, int repeel);

#endif /* REFS_REF_CACHE_H */
