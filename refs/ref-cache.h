#ifndef REFS_REF_CACHE_H
#define REFS_REF_CACHE_H

#include "hash.h"

struct ref_dir;
struct ref_store;

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

/* ref_entry represents a directory of references */
#define REF_DIR 0x10

/*
 * Entry has not yet been read from disk (used only for REF_DIR
 * entries representing loose references)
 */
#define REF_INCOMPLETE 0x20

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
				   const char *dirname, size_t len);

struct ref_entry *create_ref_entry(const char *refname,
				   const struct object_id *oid, int flag);

/*
 * Return a pointer to a new `ref_cache`. Its top-level starts out
 * marked incomplete. If `fill_ref_dir` is non-NULL, it is the
 * function called to fill in incomplete directories in the
 * `ref_cache` when they are accessed. If it is NULL, then the whole
 * `ref_cache` must be filled (including clearing its directories'
 * `REF_INCOMPLETE` bits) before it is used, and `refs` can be NULL,
 * too.
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
 * Find the value entry with the given name in dir, sorting ref_dirs
 * and recursing into subdirectories as necessary.  If the name is not
 * found or it corresponds to a directory entry, return NULL.
 */
struct ref_entry *find_ref_entry(struct ref_dir *dir, const char *refname);

/*
 * Start iterating over references in `cache`. If `prefix` is
 * specified, only include references whose names start with that
 * prefix. If `prime_dir` is true, then fill any incomplete
 * directories before beginning the iteration. The output is ordered
 * by refname.
 */
struct ref_iterator *cache_ref_iterator_begin(struct ref_cache *cache,
					      const char *prefix,
					      struct repository *repo,
					      int prime_dir);

#endif /* REFS_REF_CACHE_H */
