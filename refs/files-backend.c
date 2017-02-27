#include "../cache.h"
#include "../refs.h"
#include "refs-internal.h"
#include "../iterator.h"
#include "../dir-iterator.h"
#include "../lockfile.h"
#include "../object.h"
#include "../dir.h"

struct ref_lock {
	char *ref_name;
	struct lock_file *lk;
	struct object_id old_oid;
};

struct ref_entry;

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

struct files_ref_store;

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

	/* A pointer to the files_ref_store that contains this ref_dir. */
	struct files_ref_store *ref_store;

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
 * separate issue that is regulated by verify_refname_available().)
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

static void read_loose_refs(const char *dirname, struct ref_dir *dir);
static int search_ref_dir(struct ref_dir *dir, const char *refname, size_t len);
static struct ref_entry *create_dir_entry(struct files_ref_store *ref_store,
					  const char *dirname, size_t len,
					  int incomplete);
static void add_entry_to_dir(struct ref_dir *dir, struct ref_entry *entry);

static struct ref_dir *get_ref_dir(struct ref_entry *entry)
{
	struct ref_dir *dir;
	assert(entry->flag & REF_DIR);
	dir = &entry->u.subdir;
	if (entry->flag & REF_INCOMPLETE) {
		read_loose_refs(entry->name, dir);

		/*
		 * Manually add refs/bisect, which, being
		 * per-worktree, might not appear in the directory
		 * listing for refs/ in the main repo.
		 */
		if (!strcmp(entry->name, "refs/")) {
			int pos = search_ref_dir(dir, "refs/bisect/", 12);
			if (pos < 0) {
				struct ref_entry *child_entry;
				child_entry = create_dir_entry(dir->ref_store,
							       "refs/bisect/",
							       12, 1);
				add_entry_to_dir(dir, child_entry);
				read_loose_refs("refs/bisect",
						&child_entry->u.subdir);
			}
		}
		entry->flag &= ~REF_INCOMPLETE;
	}
	return dir;
}

static struct ref_entry *create_ref_entry(const char *refname,
					  const unsigned char *sha1, int flag,
					  int check_name)
{
	struct ref_entry *ref;

	if (check_name &&
	    check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		die("Reference has invalid format: '%s'", refname);
	FLEX_ALLOC_STR(ref, name, refname);
	hashcpy(ref->u.value.oid.hash, sha1);
	oidclr(&ref->u.value.peeled);
	ref->flag = flag;
	return ref;
}

static void clear_ref_dir(struct ref_dir *dir);

static void free_ref_entry(struct ref_entry *entry)
{
	if (entry->flag & REF_DIR) {
		/*
		 * Do not use get_ref_dir() here, as that might
		 * trigger the reading of loose refs.
		 */
		clear_ref_dir(&entry->u.subdir);
	}
	free(entry);
}

/*
 * Add a ref_entry to the end of dir (unsorted).  Entry is always
 * stored directly in dir; no recursion into subdirectories is
 * done.
 */
static void add_entry_to_dir(struct ref_dir *dir, struct ref_entry *entry)
{
	ALLOC_GROW(dir->entries, dir->nr + 1, dir->alloc);
	dir->entries[dir->nr++] = entry;
	/* optimize for the case that entries are added in order */
	if (dir->nr == 1 ||
	    (dir->nr == dir->sorted + 1 &&
	     strcmp(dir->entries[dir->nr - 2]->name,
		    dir->entries[dir->nr - 1]->name) < 0))
		dir->sorted = dir->nr;
}

/*
 * Clear and free all entries in dir, recursively.
 */
static void clear_ref_dir(struct ref_dir *dir)
{
	int i;
	for (i = 0; i < dir->nr; i++)
		free_ref_entry(dir->entries[i]);
	free(dir->entries);
	dir->sorted = dir->nr = dir->alloc = 0;
	dir->entries = NULL;
}

/*
 * Create a struct ref_entry object for the specified dirname.
 * dirname is the name of the directory with a trailing slash (e.g.,
 * "refs/heads/") or "" for the top-level directory.
 */
static struct ref_entry *create_dir_entry(struct files_ref_store *ref_store,
					  const char *dirname, size_t len,
					  int incomplete)
{
	struct ref_entry *direntry;
	FLEX_ALLOC_MEM(direntry, name, dirname, len);
	direntry->u.subdir.ref_store = ref_store;
	direntry->flag = REF_DIR | (incomplete ? REF_INCOMPLETE : 0);
	return direntry;
}

static int ref_entry_cmp(const void *a, const void *b)
{
	struct ref_entry *one = *(struct ref_entry **)a;
	struct ref_entry *two = *(struct ref_entry **)b;
	return strcmp(one->name, two->name);
}

static void sort_ref_dir(struct ref_dir *dir);

struct string_slice {
	size_t len;
	const char *str;
};

static int ref_entry_cmp_sslice(const void *key_, const void *ent_)
{
	const struct string_slice *key = key_;
	const struct ref_entry *ent = *(const struct ref_entry * const *)ent_;
	int cmp = strncmp(key->str, ent->name, key->len);
	if (cmp)
		return cmp;
	return '\0' - (unsigned char)ent->name[key->len];
}

/*
 * Return the index of the entry with the given refname from the
 * ref_dir (non-recursively), sorting dir if necessary.  Return -1 if
 * no such entry is found.  dir must already be complete.
 */
static int search_ref_dir(struct ref_dir *dir, const char *refname, size_t len)
{
	struct ref_entry **r;
	struct string_slice key;

	if (refname == NULL || !dir->nr)
		return -1;

	sort_ref_dir(dir);
	key.len = len;
	key.str = refname;
	r = bsearch(&key, dir->entries, dir->nr, sizeof(*dir->entries),
		    ref_entry_cmp_sslice);

	if (r == NULL)
		return -1;

	return r - dir->entries;
}

/*
 * Search for a directory entry directly within dir (without
 * recursing).  Sort dir if necessary.  subdirname must be a directory
 * name (i.e., end in '/').  If mkdir is set, then create the
 * directory if it is missing; otherwise, return NULL if the desired
 * directory cannot be found.  dir must already be complete.
 */
static struct ref_dir *search_for_subdir(struct ref_dir *dir,
					 const char *subdirname, size_t len,
					 int mkdir)
{
	int entry_index = search_ref_dir(dir, subdirname, len);
	struct ref_entry *entry;
	if (entry_index == -1) {
		if (!mkdir)
			return NULL;
		/*
		 * Since dir is complete, the absence of a subdir
		 * means that the subdir really doesn't exist;
		 * therefore, create an empty record for it but mark
		 * the record complete.
		 */
		entry = create_dir_entry(dir->ref_store, subdirname, len, 0);
		add_entry_to_dir(dir, entry);
	} else {
		entry = dir->entries[entry_index];
	}
	return get_ref_dir(entry);
}

/*
 * If refname is a reference name, find the ref_dir within the dir
 * tree that should hold refname.  If refname is a directory name
 * (i.e., ends in '/'), then return that ref_dir itself.  dir must
 * represent the top-level directory and must already be complete.
 * Sort ref_dirs and recurse into subdirectories as necessary.  If
 * mkdir is set, then create any missing directories; otherwise,
 * return NULL if the desired directory cannot be found.
 */
static struct ref_dir *find_containing_dir(struct ref_dir *dir,
					   const char *refname, int mkdir)
{
	const char *slash;
	for (slash = strchr(refname, '/'); slash; slash = strchr(slash + 1, '/')) {
		size_t dirnamelen = slash - refname + 1;
		struct ref_dir *subdir;
		subdir = search_for_subdir(dir, refname, dirnamelen, mkdir);
		if (!subdir) {
			dir = NULL;
			break;
		}
		dir = subdir;
	}

	return dir;
}

/*
 * Find the value entry with the given name in dir, sorting ref_dirs
 * and recursing into subdirectories as necessary.  If the name is not
 * found or it corresponds to a directory entry, return NULL.
 */
static struct ref_entry *find_ref(struct ref_dir *dir, const char *refname)
{
	int entry_index;
	struct ref_entry *entry;
	dir = find_containing_dir(dir, refname, 0);
	if (!dir)
		return NULL;
	entry_index = search_ref_dir(dir, refname, strlen(refname));
	if (entry_index == -1)
		return NULL;
	entry = dir->entries[entry_index];
	return (entry->flag & REF_DIR) ? NULL : entry;
}

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
static int remove_entry(struct ref_dir *dir, const char *refname)
{
	int refname_len = strlen(refname);
	int entry_index;
	struct ref_entry *entry;
	int is_dir = refname[refname_len - 1] == '/';
	if (is_dir) {
		/*
		 * refname represents a reference directory.  Remove
		 * the trailing slash; otherwise we will get the
		 * directory *representing* refname rather than the
		 * one *containing* it.
		 */
		char *dirname = xmemdupz(refname, refname_len - 1);
		dir = find_containing_dir(dir, dirname, 0);
		free(dirname);
	} else {
		dir = find_containing_dir(dir, refname, 0);
	}
	if (!dir)
		return -1;
	entry_index = search_ref_dir(dir, refname, refname_len);
	if (entry_index == -1)
		return -1;
	entry = dir->entries[entry_index];

	memmove(&dir->entries[entry_index],
		&dir->entries[entry_index + 1],
		(dir->nr - entry_index - 1) * sizeof(*dir->entries)
		);
	dir->nr--;
	if (dir->sorted > entry_index)
		dir->sorted--;
	free_ref_entry(entry);
	return dir->nr;
}

/*
 * Add a ref_entry to the ref_dir (unsorted), recursing into
 * subdirectories as necessary.  dir must represent the top-level
 * directory.  Return 0 on success.
 */
static int add_ref(struct ref_dir *dir, struct ref_entry *ref)
{
	dir = find_containing_dir(dir, ref->name, 1);
	if (!dir)
		return -1;
	add_entry_to_dir(dir, ref);
	return 0;
}

/*
 * Emit a warning and return true iff ref1 and ref2 have the same name
 * and the same sha1.  Die if they have the same name but different
 * sha1s.
 */
static int is_dup_ref(const struct ref_entry *ref1, const struct ref_entry *ref2)
{
	if (strcmp(ref1->name, ref2->name))
		return 0;

	/* Duplicate name; make sure that they don't conflict: */

	if ((ref1->flag & REF_DIR) || (ref2->flag & REF_DIR))
		/* This is impossible by construction */
		die("Reference directory conflict: %s", ref1->name);

	if (oidcmp(&ref1->u.value.oid, &ref2->u.value.oid))
		die("Duplicated ref, and SHA1s don't match: %s", ref1->name);

	warning("Duplicated ref: %s", ref1->name);
	return 1;
}

/*
 * Sort the entries in dir non-recursively (if they are not already
 * sorted) and remove any duplicate entries.
 */
static void sort_ref_dir(struct ref_dir *dir)
{
	int i, j;
	struct ref_entry *last = NULL;

	/*
	 * This check also prevents passing a zero-length array to qsort(),
	 * which is a problem on some platforms.
	 */
	if (dir->sorted == dir->nr)
		return;

	QSORT(dir->entries, dir->nr, ref_entry_cmp);

	/* Remove any duplicates: */
	for (i = 0, j = 0; j < dir->nr; j++) {
		struct ref_entry *entry = dir->entries[j];
		if (last && is_dup_ref(last, entry))
			free_ref_entry(entry);
		else
			last = dir->entries[i++] = entry;
	}
	dir->sorted = dir->nr = i;
}

/*
 * Return true if refname, which has the specified oid and flags, can
 * be resolved to an object in the database. If the referred-to object
 * does not exist, emit a warning and return false.
 */
static int ref_resolves_to_object(const char *refname,
				  const struct object_id *oid,
				  unsigned int flags)
{
	if (flags & REF_ISBROKEN)
		return 0;
	if (!has_sha1_file(oid->hash)) {
		error("%s does not point to a valid object!", refname);
		return 0;
	}
	return 1;
}

/*
 * Return true if the reference described by entry can be resolved to
 * an object in the database; otherwise, emit a warning and return
 * false.
 */
static int entry_resolves_to_object(struct ref_entry *entry)
{
	return ref_resolves_to_object(entry->name,
				      &entry->u.value.oid, entry->flag);
}

typedef int each_ref_entry_fn(struct ref_entry *entry, void *cb_data);

/*
 * Call fn for each reference in dir that has index in the range
 * offset <= index < dir->nr.  Recurse into subdirectories that are in
 * that index range, sorting them before iterating.  This function
 * does not sort dir itself; it should be sorted beforehand.  fn is
 * called for all references, including broken ones.
 */
static int do_for_each_entry_in_dir(struct ref_dir *dir, int offset,
				    each_ref_entry_fn fn, void *cb_data)
{
	int i;
	assert(dir->sorted == dir->nr);
	for (i = offset; i < dir->nr; i++) {
		struct ref_entry *entry = dir->entries[i];
		int retval;
		if (entry->flag & REF_DIR) {
			struct ref_dir *subdir = get_ref_dir(entry);
			sort_ref_dir(subdir);
			retval = do_for_each_entry_in_dir(subdir, 0, fn, cb_data);
		} else {
			retval = fn(entry, cb_data);
		}
		if (retval)
			return retval;
	}
	return 0;
}

/*
 * Load all of the refs from the dir into our in-memory cache. The hard work
 * of loading loose refs is done by get_ref_dir(), so we just need to recurse
 * through all of the sub-directories. We do not even need to care about
 * sorting, as traversal order does not matter to us.
 */
static void prime_ref_dir(struct ref_dir *dir)
{
	int i;
	for (i = 0; i < dir->nr; i++) {
		struct ref_entry *entry = dir->entries[i];
		if (entry->flag & REF_DIR)
			prime_ref_dir(get_ref_dir(entry));
	}
}

/*
 * A level in the reference hierarchy that is currently being iterated
 * through.
 */
struct cache_ref_iterator_level {
	/*
	 * The ref_dir being iterated over at this level. The ref_dir
	 * is sorted before being stored here.
	 */
	struct ref_dir *dir;

	/*
	 * The index of the current entry within dir (which might
	 * itself be a directory). If index == -1, then the iteration
	 * hasn't yet begun. If index == dir->nr, then the iteration
	 * through this level is over.
	 */
	int index;
};

/*
 * Represent an iteration through a ref_dir in the memory cache. The
 * iteration recurses through subdirectories.
 */
struct cache_ref_iterator {
	struct ref_iterator base;

	/*
	 * The number of levels currently on the stack. This is always
	 * at least 1, because when it becomes zero the iteration is
	 * ended and this struct is freed.
	 */
	size_t levels_nr;

	/* The number of levels that have been allocated on the stack */
	size_t levels_alloc;

	/*
	 * A stack of levels. levels[0] is the uppermost level that is
	 * being iterated over in this iteration. (This is not
	 * necessary the top level in the references hierarchy. If we
	 * are iterating through a subtree, then levels[0] will hold
	 * the ref_dir for that subtree, and subsequent levels will go
	 * on from there.)
	 */
	struct cache_ref_iterator_level *levels;
};

static int cache_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct cache_ref_iterator *iter =
		(struct cache_ref_iterator *)ref_iterator;

	while (1) {
		struct cache_ref_iterator_level *level =
			&iter->levels[iter->levels_nr - 1];
		struct ref_dir *dir = level->dir;
		struct ref_entry *entry;

		if (level->index == -1)
			sort_ref_dir(dir);

		if (++level->index == level->dir->nr) {
			/* This level is exhausted; pop up a level */
			if (--iter->levels_nr == 0)
				return ref_iterator_abort(ref_iterator);

			continue;
		}

		entry = dir->entries[level->index];

		if (entry->flag & REF_DIR) {
			/* push down a level */
			ALLOC_GROW(iter->levels, iter->levels_nr + 1,
				   iter->levels_alloc);

			level = &iter->levels[iter->levels_nr++];
			level->dir = get_ref_dir(entry);
			level->index = -1;
		} else {
			iter->base.refname = entry->name;
			iter->base.oid = &entry->u.value.oid;
			iter->base.flags = entry->flag;
			return ITER_OK;
		}
	}
}

static enum peel_status peel_entry(struct ref_entry *entry, int repeel);

static int cache_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct cache_ref_iterator *iter =
		(struct cache_ref_iterator *)ref_iterator;
	struct cache_ref_iterator_level *level;
	struct ref_entry *entry;

	level = &iter->levels[iter->levels_nr - 1];

	if (level->index == -1)
		die("BUG: peel called before advance for cache iterator");

	entry = level->dir->entries[level->index];

	if (peel_entry(entry, 0))
		return -1;
	oidcpy(peeled, &entry->u.value.peeled);
	return 0;
}

static int cache_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct cache_ref_iterator *iter =
		(struct cache_ref_iterator *)ref_iterator;

	free(iter->levels);
	base_ref_iterator_free(ref_iterator);
	return ITER_DONE;
}

static struct ref_iterator_vtable cache_ref_iterator_vtable = {
	cache_ref_iterator_advance,
	cache_ref_iterator_peel,
	cache_ref_iterator_abort
};

static struct ref_iterator *cache_ref_iterator_begin(struct ref_dir *dir)
{
	struct cache_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	struct cache_ref_iterator_level *level;

	iter = xcalloc(1, sizeof(*iter));
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &cache_ref_iterator_vtable);
	ALLOC_GROW(iter->levels, 10, iter->levels_alloc);

	iter->levels_nr = 1;
	level = &iter->levels[0];
	level->index = -1;
	level->dir = dir;

	return ref_iterator;
}

struct nonmatching_ref_data {
	const struct string_list *skip;
	const char *conflicting_refname;
};

static int nonmatching_ref_fn(struct ref_entry *entry, void *vdata)
{
	struct nonmatching_ref_data *data = vdata;

	if (data->skip && string_list_has_string(data->skip, entry->name))
		return 0;

	data->conflicting_refname = entry->name;
	return 1;
}

/*
 * Return 0 if a reference named refname could be created without
 * conflicting with the name of an existing reference in dir.
 * See verify_refname_available for more information.
 */
static int verify_refname_available_dir(const char *refname,
					const struct string_list *extras,
					const struct string_list *skip,
					struct ref_dir *dir,
					struct strbuf *err)
{
	const char *slash;
	const char *extra_refname;
	int pos;
	struct strbuf dirname = STRBUF_INIT;
	int ret = -1;

	/*
	 * For the sake of comments in this function, suppose that
	 * refname is "refs/foo/bar".
	 */

	assert(err);

	strbuf_grow(&dirname, strlen(refname) + 1);
	for (slash = strchr(refname, '/'); slash; slash = strchr(slash + 1, '/')) {
		/* Expand dirname to the new prefix, not including the trailing slash: */
		strbuf_add(&dirname, refname + dirname.len, slash - refname - dirname.len);

		/*
		 * We are still at a leading dir of the refname (e.g.,
		 * "refs/foo"; if there is a reference with that name,
		 * it is a conflict, *unless* it is in skip.
		 */
		if (dir) {
			pos = search_ref_dir(dir, dirname.buf, dirname.len);
			if (pos >= 0 &&
			    (!skip || !string_list_has_string(skip, dirname.buf))) {
				/*
				 * We found a reference whose name is
				 * a proper prefix of refname; e.g.,
				 * "refs/foo", and is not in skip.
				 */
				strbuf_addf(err, "'%s' exists; cannot create '%s'",
					    dirname.buf, refname);
				goto cleanup;
			}
		}

		if (extras && string_list_has_string(extras, dirname.buf) &&
		    (!skip || !string_list_has_string(skip, dirname.buf))) {
			strbuf_addf(err, "cannot process '%s' and '%s' at the same time",
				    refname, dirname.buf);
			goto cleanup;
		}

		/*
		 * Otherwise, we can try to continue our search with
		 * the next component. So try to look up the
		 * directory, e.g., "refs/foo/". If we come up empty,
		 * we know there is nothing under this whole prefix,
		 * but even in that case we still have to continue the
		 * search for conflicts with extras.
		 */
		strbuf_addch(&dirname, '/');
		if (dir) {
			pos = search_ref_dir(dir, dirname.buf, dirname.len);
			if (pos < 0) {
				/*
				 * There was no directory "refs/foo/",
				 * so there is nothing under this
				 * whole prefix. So there is no need
				 * to continue looking for conflicting
				 * references. But we need to continue
				 * looking for conflicting extras.
				 */
				dir = NULL;
			} else {
				dir = get_ref_dir(dir->entries[pos]);
			}
		}
	}

	/*
	 * We are at the leaf of our refname (e.g., "refs/foo/bar").
	 * There is no point in searching for a reference with that
	 * name, because a refname isn't considered to conflict with
	 * itself. But we still need to check for references whose
	 * names are in the "refs/foo/bar/" namespace, because they
	 * *do* conflict.
	 */
	strbuf_addstr(&dirname, refname + dirname.len);
	strbuf_addch(&dirname, '/');

	if (dir) {
		pos = search_ref_dir(dir, dirname.buf, dirname.len);

		if (pos >= 0) {
			/*
			 * We found a directory named "$refname/"
			 * (e.g., "refs/foo/bar/"). It is a problem
			 * iff it contains any ref that is not in
			 * "skip".
			 */
			struct nonmatching_ref_data data;

			data.skip = skip;
			data.conflicting_refname = NULL;
			dir = get_ref_dir(dir->entries[pos]);
			sort_ref_dir(dir);
			if (do_for_each_entry_in_dir(dir, 0, nonmatching_ref_fn, &data)) {
				strbuf_addf(err, "'%s' exists; cannot create '%s'",
					    data.conflicting_refname, refname);
				goto cleanup;
			}
		}
	}

	extra_refname = find_descendant_ref(dirname.buf, extras, skip);
	if (extra_refname)
		strbuf_addf(err, "cannot process '%s' and '%s' at the same time",
			    refname, extra_refname);
	else
		ret = 0;

cleanup:
	strbuf_release(&dirname);
	return ret;
}

struct packed_ref_cache {
	struct ref_entry *root;

	/*
	 * Count of references to the data structure in this instance,
	 * including the pointer from files_ref_store::packed if any.
	 * The data will not be freed as long as the reference count
	 * is nonzero.
	 */
	unsigned int referrers;

	/*
	 * Iff the packed-refs file associated with this instance is
	 * currently locked for writing, this points at the associated
	 * lock (which is owned by somebody else).  The referrer count
	 * is also incremented when the file is locked and decremented
	 * when it is unlocked.
	 */
	struct lock_file *lock;

	/* The metadata from when this packed-refs cache was read */
	struct stat_validity validity;
};

/*
 * Future: need to be in "struct repository"
 * when doing a full libification.
 */
struct files_ref_store {
	struct ref_store base;

	/*
	 * The name of the submodule represented by this object, or
	 * NULL if it represents the main repository's reference
	 * store:
	 */
	const char *submodule;

	struct ref_entry *loose;
	struct packed_ref_cache *packed;
};

/* Lock used for the main packed-refs file: */
static struct lock_file packlock;

/*
 * Increment the reference count of *packed_refs.
 */
static void acquire_packed_ref_cache(struct packed_ref_cache *packed_refs)
{
	packed_refs->referrers++;
}

/*
 * Decrease the reference count of *packed_refs.  If it goes to zero,
 * free *packed_refs and return true; otherwise return false.
 */
static int release_packed_ref_cache(struct packed_ref_cache *packed_refs)
{
	if (!--packed_refs->referrers) {
		free_ref_entry(packed_refs->root);
		stat_validity_clear(&packed_refs->validity);
		free(packed_refs);
		return 1;
	} else {
		return 0;
	}
}

static void clear_packed_ref_cache(struct files_ref_store *refs)
{
	if (refs->packed) {
		struct packed_ref_cache *packed_refs = refs->packed;

		if (packed_refs->lock)
			die("internal error: packed-ref cache cleared while locked");
		refs->packed = NULL;
		release_packed_ref_cache(packed_refs);
	}
}

static void clear_loose_ref_cache(struct files_ref_store *refs)
{
	if (refs->loose) {
		free_ref_entry(refs->loose);
		refs->loose = NULL;
	}
}

/*
 * Create a new submodule ref cache and add it to the internal
 * set of caches.
 */
static struct ref_store *files_ref_store_create(const char *submodule)
{
	struct files_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct ref_store *ref_store = (struct ref_store *)refs;

	base_ref_store_init(ref_store, &refs_be_files);

	refs->submodule = xstrdup_or_null(submodule);

	return ref_store;
}

/*
 * Die if refs is for a submodule (i.e., not for the main repository).
 * caller is used in any necessary error messages.
 */
static void files_assert_main_repository(struct files_ref_store *refs,
					 const char *caller)
{
	if (refs->submodule)
		die("BUG: %s called for a submodule", caller);
}

/*
 * Downcast ref_store to files_ref_store. Die if ref_store is not a
 * files_ref_store. If submodule_allowed is not true, then also die if
 * files_ref_store is for a submodule (i.e., not for the main
 * repository). caller is used in any necessary error messages.
 */
static struct files_ref_store *files_downcast(
		struct ref_store *ref_store, int submodule_allowed,
		const char *caller)
{
	struct files_ref_store *refs;

	if (ref_store->be != &refs_be_files)
		die("BUG: ref_store is type \"%s\" not \"files\" in %s",
		    ref_store->be->name, caller);

	refs = (struct files_ref_store *)ref_store;

	if (!submodule_allowed)
		files_assert_main_repository(refs, caller);

	return refs;
}

/* The length of a peeled reference line in packed-refs, including EOL: */
#define PEELED_LINE_LENGTH 42

/*
 * The packed-refs header line that we write out.  Perhaps other
 * traits will be added later.  The trailing space is required.
 */
static const char PACKED_REFS_HEADER[] =
	"# pack-refs with: peeled fully-peeled \n";

/*
 * Parse one line from a packed-refs file.  Write the SHA1 to sha1.
 * Return a pointer to the refname within the line (null-terminated),
 * or NULL if there was a problem.
 */
static const char *parse_ref_line(struct strbuf *line, unsigned char *sha1)
{
	const char *ref;

	/*
	 * 42: the answer to everything.
	 *
	 * In this case, it happens to be the answer to
	 *  40 (length of sha1 hex representation)
	 *  +1 (space in between hex and name)
	 *  +1 (newline at the end of the line)
	 */
	if (line->len <= 42)
		return NULL;

	if (get_sha1_hex(line->buf, sha1) < 0)
		return NULL;
	if (!isspace(line->buf[40]))
		return NULL;

	ref = line->buf + 41;
	if (isspace(*ref))
		return NULL;

	if (line->buf[line->len - 1] != '\n')
		return NULL;
	line->buf[--line->len] = 0;

	return ref;
}

/*
 * Read f, which is a packed-refs file, into dir.
 *
 * A comment line of the form "# pack-refs with: " may contain zero or
 * more traits. We interpret the traits as follows:
 *
 *   No traits:
 *
 *      Probably no references are peeled. But if the file contains a
 *      peeled value for a reference, we will use it.
 *
 *   peeled:
 *
 *      References under "refs/tags/", if they *can* be peeled, *are*
 *      peeled in this file. References outside of "refs/tags/" are
 *      probably not peeled even if they could have been, but if we find
 *      a peeled value for such a reference we will use it.
 *
 *   fully-peeled:
 *
 *      All references in the file that can be peeled are peeled.
 *      Inversely (and this is more important), any references in the
 *      file for which no peeled value is recorded is not peelable. This
 *      trait should typically be written alongside "peeled" for
 *      compatibility with older clients, but we do not require it
 *      (i.e., "peeled" is a no-op if "fully-peeled" is set).
 */
static void read_packed_refs(FILE *f, struct ref_dir *dir)
{
	struct ref_entry *last = NULL;
	struct strbuf line = STRBUF_INIT;
	enum { PEELED_NONE, PEELED_TAGS, PEELED_FULLY } peeled = PEELED_NONE;

	while (strbuf_getwholeline(&line, f, '\n') != EOF) {
		unsigned char sha1[20];
		const char *refname;
		const char *traits;

		if (skip_prefix(line.buf, "# pack-refs with:", &traits)) {
			if (strstr(traits, " fully-peeled "))
				peeled = PEELED_FULLY;
			else if (strstr(traits, " peeled "))
				peeled = PEELED_TAGS;
			/* perhaps other traits later as well */
			continue;
		}

		refname = parse_ref_line(&line, sha1);
		if (refname) {
			int flag = REF_ISPACKED;

			if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
				if (!refname_is_safe(refname))
					die("packed refname is dangerous: %s", refname);
				hashclr(sha1);
				flag |= REF_BAD_NAME | REF_ISBROKEN;
			}
			last = create_ref_entry(refname, sha1, flag, 0);
			if (peeled == PEELED_FULLY ||
			    (peeled == PEELED_TAGS && starts_with(refname, "refs/tags/")))
				last->flag |= REF_KNOWS_PEELED;
			add_ref(dir, last);
			continue;
		}
		if (last &&
		    line.buf[0] == '^' &&
		    line.len == PEELED_LINE_LENGTH &&
		    line.buf[PEELED_LINE_LENGTH - 1] == '\n' &&
		    !get_sha1_hex(line.buf + 1, sha1)) {
			hashcpy(last->u.value.peeled.hash, sha1);
			/*
			 * Regardless of what the file header said,
			 * we definitely know the value of *this*
			 * reference:
			 */
			last->flag |= REF_KNOWS_PEELED;
		}
	}

	strbuf_release(&line);
}

/*
 * Get the packed_ref_cache for the specified files_ref_store,
 * creating it if necessary.
 */
static struct packed_ref_cache *get_packed_ref_cache(struct files_ref_store *refs)
{
	char *packed_refs_file;

	if (refs->submodule)
		packed_refs_file = git_pathdup_submodule(refs->submodule,
							 "packed-refs");
	else
		packed_refs_file = git_pathdup("packed-refs");

	if (refs->packed &&
	    !stat_validity_check(&refs->packed->validity, packed_refs_file))
		clear_packed_ref_cache(refs);

	if (!refs->packed) {
		FILE *f;

		refs->packed = xcalloc(1, sizeof(*refs->packed));
		acquire_packed_ref_cache(refs->packed);
		refs->packed->root = create_dir_entry(refs, "", 0, 0);
		f = fopen(packed_refs_file, "r");
		if (f) {
			stat_validity_update(&refs->packed->validity, fileno(f));
			read_packed_refs(f, get_ref_dir(refs->packed->root));
			fclose(f);
		}
	}
	free(packed_refs_file);
	return refs->packed;
}

static struct ref_dir *get_packed_ref_dir(struct packed_ref_cache *packed_ref_cache)
{
	return get_ref_dir(packed_ref_cache->root);
}

static struct ref_dir *get_packed_refs(struct files_ref_store *refs)
{
	return get_packed_ref_dir(get_packed_ref_cache(refs));
}

/*
 * Add a reference to the in-memory packed reference cache.  This may
 * only be called while the packed-refs file is locked (see
 * lock_packed_refs()).  To actually write the packed-refs file, call
 * commit_packed_refs().
 */
static void add_packed_ref(struct files_ref_store *refs,
			   const char *refname, const unsigned char *sha1)
{
	struct packed_ref_cache *packed_ref_cache = get_packed_ref_cache(refs);

	if (!packed_ref_cache->lock)
		die("internal error: packed refs not locked");
	add_ref(get_packed_ref_dir(packed_ref_cache),
		create_ref_entry(refname, sha1, REF_ISPACKED, 1));
}

/*
 * Read the loose references from the namespace dirname into dir
 * (without recursing).  dirname must end with '/'.  dir must be the
 * directory entry corresponding to dirname.
 */
static void read_loose_refs(const char *dirname, struct ref_dir *dir)
{
	struct files_ref_store *refs = dir->ref_store;
	DIR *d;
	struct dirent *de;
	int dirnamelen = strlen(dirname);
	struct strbuf refname;
	struct strbuf path = STRBUF_INIT;
	size_t path_baselen;
	int err = 0;

	if (refs->submodule)
		err = strbuf_git_path_submodule(&path, refs->submodule, "%s", dirname);
	else
		strbuf_git_path(&path, "%s", dirname);
	path_baselen = path.len;

	if (err) {
		strbuf_release(&path);
		return;
	}

	d = opendir(path.buf);
	if (!d) {
		strbuf_release(&path);
		return;
	}

	strbuf_init(&refname, dirnamelen + 257);
	strbuf_add(&refname, dirname, dirnamelen);

	while ((de = readdir(d)) != NULL) {
		unsigned char sha1[20];
		struct stat st;
		int flag;

		if (de->d_name[0] == '.')
			continue;
		if (ends_with(de->d_name, ".lock"))
			continue;
		strbuf_addstr(&refname, de->d_name);
		strbuf_addstr(&path, de->d_name);
		if (stat(path.buf, &st) < 0) {
			; /* silently ignore */
		} else if (S_ISDIR(st.st_mode)) {
			strbuf_addch(&refname, '/');
			add_entry_to_dir(dir,
					 create_dir_entry(refs, refname.buf,
							  refname.len, 1));
		} else {
			if (!resolve_ref_recursively(&refs->base,
						     refname.buf,
						     RESOLVE_REF_READING,
						     sha1, &flag)) {
				hashclr(sha1);
				flag |= REF_ISBROKEN;
			} else if (is_null_sha1(sha1)) {
				/*
				 * It is so astronomically unlikely
				 * that NULL_SHA1 is the SHA-1 of an
				 * actual object that we consider its
				 * appearance in a loose reference
				 * file to be repo corruption
				 * (probably due to a software bug).
				 */
				flag |= REF_ISBROKEN;
			}

			if (check_refname_format(refname.buf,
						 REFNAME_ALLOW_ONELEVEL)) {
				if (!refname_is_safe(refname.buf))
					die("loose refname is dangerous: %s", refname.buf);
				hashclr(sha1);
				flag |= REF_BAD_NAME | REF_ISBROKEN;
			}
			add_entry_to_dir(dir,
					 create_ref_entry(refname.buf, sha1, flag, 0));
		}
		strbuf_setlen(&refname, dirnamelen);
		strbuf_setlen(&path, path_baselen);
	}
	strbuf_release(&refname);
	strbuf_release(&path);
	closedir(d);
}

static struct ref_dir *get_loose_refs(struct files_ref_store *refs)
{
	if (!refs->loose) {
		/*
		 * Mark the top-level directory complete because we
		 * are about to read the only subdirectory that can
		 * hold references:
		 */
		refs->loose = create_dir_entry(refs, "", 0, 0);
		/*
		 * Create an incomplete entry for "refs/":
		 */
		add_entry_to_dir(get_ref_dir(refs->loose),
				 create_dir_entry(refs, "refs/", 5, 1));
	}
	return get_ref_dir(refs->loose);
}

/*
 * Return the ref_entry for the given refname from the packed
 * references.  If it does not exist, return NULL.
 */
static struct ref_entry *get_packed_ref(struct files_ref_store *refs,
					const char *refname)
{
	return find_ref(get_packed_refs(refs), refname);
}

/*
 * A loose ref file doesn't exist; check for a packed ref.
 */
static int resolve_packed_ref(struct files_ref_store *refs,
			      const char *refname,
			      unsigned char *sha1, unsigned int *flags)
{
	struct ref_entry *entry;

	/*
	 * The loose reference file does not exist; check for a packed
	 * reference.
	 */
	entry = get_packed_ref(refs, refname);
	if (entry) {
		hashcpy(sha1, entry->u.value.oid.hash);
		*flags |= REF_ISPACKED;
		return 0;
	}
	/* refname is not a packed reference. */
	return -1;
}

static int files_read_raw_ref(struct ref_store *ref_store,
			      const char *refname, unsigned char *sha1,
			      struct strbuf *referent, unsigned int *type)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 1, "read_raw_ref");
	struct strbuf sb_contents = STRBUF_INIT;
	struct strbuf sb_path = STRBUF_INIT;
	const char *path;
	const char *buf;
	struct stat st;
	int fd;
	int ret = -1;
	int save_errno;
	int remaining_retries = 3;

	*type = 0;
	strbuf_reset(&sb_path);

	if (refs->submodule)
		strbuf_git_path_submodule(&sb_path, refs->submodule, "%s", refname);
	else
		strbuf_git_path(&sb_path, "%s", refname);

	path = sb_path.buf;

stat_ref:
	/*
	 * We might have to loop back here to avoid a race
	 * condition: first we lstat() the file, then we try
	 * to read it as a link or as a file.  But if somebody
	 * changes the type of the file (file <-> directory
	 * <-> symlink) between the lstat() and reading, then
	 * we don't want to report that as an error but rather
	 * try again starting with the lstat().
	 *
	 * We'll keep a count of the retries, though, just to avoid
	 * any confusing situation sending us into an infinite loop.
	 */

	if (remaining_retries-- <= 0)
		goto out;

	if (lstat(path, &st) < 0) {
		if (errno != ENOENT)
			goto out;
		if (resolve_packed_ref(refs, refname, sha1, type)) {
			errno = ENOENT;
			goto out;
		}
		ret = 0;
		goto out;
	}

	/* Follow "normalized" - ie "refs/.." symlinks by hand */
	if (S_ISLNK(st.st_mode)) {
		strbuf_reset(&sb_contents);
		if (strbuf_readlink(&sb_contents, path, 0) < 0) {
			if (errno == ENOENT || errno == EINVAL)
				/* inconsistent with lstat; retry */
				goto stat_ref;
			else
				goto out;
		}
		if (starts_with(sb_contents.buf, "refs/") &&
		    !check_refname_format(sb_contents.buf, 0)) {
			strbuf_swap(&sb_contents, referent);
			*type |= REF_ISSYMREF;
			ret = 0;
			goto out;
		}
		/*
		 * It doesn't look like a refname; fall through to just
		 * treating it like a non-symlink, and reading whatever it
		 * points to.
		 */
	}

	/* Is it a directory? */
	if (S_ISDIR(st.st_mode)) {
		/*
		 * Even though there is a directory where the loose
		 * ref is supposed to be, there could still be a
		 * packed ref:
		 */
		if (resolve_packed_ref(refs, refname, sha1, type)) {
			errno = EISDIR;
			goto out;
		}
		ret = 0;
		goto out;
	}

	/*
	 * Anything else, just open it and try to use it as
	 * a ref
	 */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT && !S_ISLNK(st.st_mode))
			/* inconsistent with lstat; retry */
			goto stat_ref;
		else
			goto out;
	}
	strbuf_reset(&sb_contents);
	if (strbuf_read(&sb_contents, fd, 256) < 0) {
		int save_errno = errno;
		close(fd);
		errno = save_errno;
		goto out;
	}
	close(fd);
	strbuf_rtrim(&sb_contents);
	buf = sb_contents.buf;
	if (starts_with(buf, "ref:")) {
		buf += 4;
		while (isspace(*buf))
			buf++;

		strbuf_reset(referent);
		strbuf_addstr(referent, buf);
		*type |= REF_ISSYMREF;
		ret = 0;
		goto out;
	}

	/*
	 * Please note that FETCH_HEAD has additional
	 * data after the sha.
	 */
	if (get_sha1_hex(buf, sha1) ||
	    (buf[40] != '\0' && !isspace(buf[40]))) {
		*type |= REF_ISBROKEN;
		errno = EINVAL;
		goto out;
	}

	ret = 0;

out:
	save_errno = errno;
	strbuf_release(&sb_path);
	strbuf_release(&sb_contents);
	errno = save_errno;
	return ret;
}

static void unlock_ref(struct ref_lock *lock)
{
	/* Do not free lock->lk -- atexit() still looks at them */
	if (lock->lk)
		rollback_lock_file(lock->lk);
	free(lock->ref_name);
	free(lock);
}

/*
 * Lock refname, without following symrefs, and set *lock_p to point
 * at a newly-allocated lock object. Fill in lock->old_oid, referent,
 * and type similarly to read_raw_ref().
 *
 * The caller must verify that refname is a "safe" reference name (in
 * the sense of refname_is_safe()) before calling this function.
 *
 * If the reference doesn't already exist, verify that refname doesn't
 * have a D/F conflict with any existing references. extras and skip
 * are passed to verify_refname_available_dir() for this check.
 *
 * If mustexist is not set and the reference is not found or is
 * broken, lock the reference anyway but clear sha1.
 *
 * Return 0 on success. On failure, write an error message to err and
 * return TRANSACTION_NAME_CONFLICT or TRANSACTION_GENERIC_ERROR.
 *
 * Implementation note: This function is basically
 *
 *     lock reference
 *     read_raw_ref()
 *
 * but it includes a lot more code to
 * - Deal with possible races with other processes
 * - Avoid calling verify_refname_available_dir() when it can be
 *   avoided, namely if we were successfully able to read the ref
 * - Generate informative error messages in the case of failure
 */
static int lock_raw_ref(struct files_ref_store *refs,
			const char *refname, int mustexist,
			const struct string_list *extras,
			const struct string_list *skip,
			struct ref_lock **lock_p,
			struct strbuf *referent,
			unsigned int *type,
			struct strbuf *err)
{
	struct ref_lock *lock;
	struct strbuf ref_file = STRBUF_INIT;
	int attempts_remaining = 3;
	int ret = TRANSACTION_GENERIC_ERROR;

	assert(err);
	files_assert_main_repository(refs, "lock_raw_ref");

	*type = 0;

	/* First lock the file so it can't change out from under us. */

	*lock_p = lock = xcalloc(1, sizeof(*lock));

	lock->ref_name = xstrdup(refname);
	strbuf_git_path(&ref_file, "%s", refname);

retry:
	switch (safe_create_leading_directories(ref_file.buf)) {
	case SCLD_OK:
		break; /* success */
	case SCLD_EXISTS:
		/*
		 * Suppose refname is "refs/foo/bar". We just failed
		 * to create the containing directory, "refs/foo",
		 * because there was a non-directory in the way. This
		 * indicates a D/F conflict, probably because of
		 * another reference such as "refs/foo". There is no
		 * reason to expect this error to be transitory.
		 */
		if (verify_refname_available(refname, extras, skip, err)) {
			if (mustexist) {
				/*
				 * To the user the relevant error is
				 * that the "mustexist" reference is
				 * missing:
				 */
				strbuf_reset(err);
				strbuf_addf(err, "unable to resolve reference '%s'",
					    refname);
			} else {
				/*
				 * The error message set by
				 * verify_refname_available_dir() is OK.
				 */
				ret = TRANSACTION_NAME_CONFLICT;
			}
		} else {
			/*
			 * The file that is in the way isn't a loose
			 * reference. Report it as a low-level
			 * failure.
			 */
			strbuf_addf(err, "unable to create lock file %s.lock; "
				    "non-directory in the way",
				    ref_file.buf);
		}
		goto error_return;
	case SCLD_VANISHED:
		/* Maybe another process was tidying up. Try again. */
		if (--attempts_remaining > 0)
			goto retry;
		/* fall through */
	default:
		strbuf_addf(err, "unable to create directory for %s",
			    ref_file.buf);
		goto error_return;
	}

	if (!lock->lk)
		lock->lk = xcalloc(1, sizeof(struct lock_file));

	if (hold_lock_file_for_update(lock->lk, ref_file.buf, LOCK_NO_DEREF) < 0) {
		if (errno == ENOENT && --attempts_remaining > 0) {
			/*
			 * Maybe somebody just deleted one of the
			 * directories leading to ref_file.  Try
			 * again:
			 */
			goto retry;
		} else {
			unable_to_lock_message(ref_file.buf, errno, err);
			goto error_return;
		}
	}

	/*
	 * Now we hold the lock and can read the reference without
	 * fear that its value will change.
	 */

	if (files_read_raw_ref(&refs->base, refname,
			       lock->old_oid.hash, referent, type)) {
		if (errno == ENOENT) {
			if (mustexist) {
				/* Garden variety missing reference. */
				strbuf_addf(err, "unable to resolve reference '%s'",
					    refname);
				goto error_return;
			} else {
				/*
				 * Reference is missing, but that's OK. We
				 * know that there is not a conflict with
				 * another loose reference because
				 * (supposing that we are trying to lock
				 * reference "refs/foo/bar"):
				 *
				 * - We were successfully able to create
				 *   the lockfile refs/foo/bar.lock, so we
				 *   know there cannot be a loose reference
				 *   named "refs/foo".
				 *
				 * - We got ENOENT and not EISDIR, so we
				 *   know that there cannot be a loose
				 *   reference named "refs/foo/bar/baz".
				 */
			}
		} else if (errno == EISDIR) {
			/*
			 * There is a directory in the way. It might have
			 * contained references that have been deleted. If
			 * we don't require that the reference already
			 * exists, try to remove the directory so that it
			 * doesn't cause trouble when we want to rename the
			 * lockfile into place later.
			 */
			if (mustexist) {
				/* Garden variety missing reference. */
				strbuf_addf(err, "unable to resolve reference '%s'",
					    refname);
				goto error_return;
			} else if (remove_dir_recursively(&ref_file,
							  REMOVE_DIR_EMPTY_ONLY)) {
				if (verify_refname_available_dir(
						    refname, extras, skip,
						    get_loose_refs(refs),
						    err)) {
					/*
					 * The error message set by
					 * verify_refname_available() is OK.
					 */
					ret = TRANSACTION_NAME_CONFLICT;
					goto error_return;
				} else {
					/*
					 * We can't delete the directory,
					 * but we also don't know of any
					 * references that it should
					 * contain.
					 */
					strbuf_addf(err, "there is a non-empty directory '%s' "
						    "blocking reference '%s'",
						    ref_file.buf, refname);
					goto error_return;
				}
			}
		} else if (errno == EINVAL && (*type & REF_ISBROKEN)) {
			strbuf_addf(err, "unable to resolve reference '%s': "
				    "reference broken", refname);
			goto error_return;
		} else {
			strbuf_addf(err, "unable to resolve reference '%s': %s",
				    refname, strerror(errno));
			goto error_return;
		}

		/*
		 * If the ref did not exist and we are creating it,
		 * make sure there is no existing packed ref whose
		 * name begins with our refname, nor a packed ref
		 * whose name is a proper prefix of our refname.
		 */
		if (verify_refname_available_dir(
				    refname, extras, skip,
				    get_packed_refs(refs),
				    err)) {
			goto error_return;
		}
	}

	ret = 0;
	goto out;

error_return:
	unlock_ref(lock);
	*lock_p = NULL;

out:
	strbuf_release(&ref_file);
	return ret;
}

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
static enum peel_status peel_entry(struct ref_entry *entry, int repeel)
{
	enum peel_status status;

	if (entry->flag & REF_KNOWS_PEELED) {
		if (repeel) {
			entry->flag &= ~REF_KNOWS_PEELED;
			oidclr(&entry->u.value.peeled);
		} else {
			return is_null_oid(&entry->u.value.peeled) ?
				PEEL_NON_TAG : PEEL_PEELED;
		}
	}
	if (entry->flag & REF_ISBROKEN)
		return PEEL_BROKEN;
	if (entry->flag & REF_ISSYMREF)
		return PEEL_IS_SYMREF;

	status = peel_object(entry->u.value.oid.hash, entry->u.value.peeled.hash);
	if (status == PEEL_PEELED || status == PEEL_NON_TAG)
		entry->flag |= REF_KNOWS_PEELED;
	return status;
}

static int files_peel_ref(struct ref_store *ref_store,
			  const char *refname, unsigned char *sha1)
{
	struct files_ref_store *refs = files_downcast(ref_store, 0, "peel_ref");
	int flag;
	unsigned char base[20];

	if (current_ref_iter && current_ref_iter->refname == refname) {
		struct object_id peeled;

		if (ref_iterator_peel(current_ref_iter, &peeled))
			return -1;
		hashcpy(sha1, peeled.hash);
		return 0;
	}

	if (read_ref_full(refname, RESOLVE_REF_READING, base, &flag))
		return -1;

	/*
	 * If the reference is packed, read its ref_entry from the
	 * cache in the hope that we already know its peeled value.
	 * We only try this optimization on packed references because
	 * (a) forcing the filling of the loose reference cache could
	 * be expensive and (b) loose references anyway usually do not
	 * have REF_KNOWS_PEELED.
	 */
	if (flag & REF_ISPACKED) {
		struct ref_entry *r = get_packed_ref(refs, refname);
		if (r) {
			if (peel_entry(r, 0))
				return -1;
			hashcpy(sha1, r->u.value.peeled.hash);
			return 0;
		}
	}

	return peel_object(base, sha1);
}

struct files_ref_iterator {
	struct ref_iterator base;

	struct packed_ref_cache *packed_ref_cache;
	struct ref_iterator *iter0;
	unsigned int flags;
};

static int files_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct files_ref_iterator *iter =
		(struct files_ref_iterator *)ref_iterator;
	int ok;

	while ((ok = ref_iterator_advance(iter->iter0)) == ITER_OK) {
		if (iter->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    ref_type(iter->iter0->refname) != REF_TYPE_PER_WORKTREE)
			continue;

		if (!(iter->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(iter->iter0->refname,
					    iter->iter0->oid,
					    iter->iter0->flags))
			continue;

		iter->base.refname = iter->iter0->refname;
		iter->base.oid = iter->iter0->oid;
		iter->base.flags = iter->iter0->flags;
		return ITER_OK;
	}

	iter->iter0 = NULL;
	if (ref_iterator_abort(ref_iterator) != ITER_DONE)
		ok = ITER_ERROR;

	return ok;
}

static int files_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct files_ref_iterator *iter =
		(struct files_ref_iterator *)ref_iterator;

	return ref_iterator_peel(iter->iter0, peeled);
}

static int files_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct files_ref_iterator *iter =
		(struct files_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	if (iter->iter0)
		ok = ref_iterator_abort(iter->iter0);

	release_packed_ref_cache(iter->packed_ref_cache);
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable files_ref_iterator_vtable = {
	files_ref_iterator_advance,
	files_ref_iterator_peel,
	files_ref_iterator_abort
};

static struct ref_iterator *files_ref_iterator_begin(
		struct ref_store *ref_store,
		const char *prefix, unsigned int flags)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 1, "ref_iterator_begin");
	struct ref_dir *loose_dir, *packed_dir;
	struct ref_iterator *loose_iter, *packed_iter;
	struct files_ref_iterator *iter;
	struct ref_iterator *ref_iterator;

	if (!refs)
		return empty_ref_iterator_begin();

	if (ref_paranoia < 0)
		ref_paranoia = git_env_bool("GIT_REF_PARANOIA", 0);
	if (ref_paranoia)
		flags |= DO_FOR_EACH_INCLUDE_BROKEN;

	iter = xcalloc(1, sizeof(*iter));
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &files_ref_iterator_vtable);

	/*
	 * We must make sure that all loose refs are read before
	 * accessing the packed-refs file; this avoids a race
	 * condition if loose refs are migrated to the packed-refs
	 * file by a simultaneous process, but our in-memory view is
	 * from before the migration. We ensure this as follows:
	 * First, we call prime_ref_dir(), which pre-reads the loose
	 * references for the subtree into the cache. (If they've
	 * already been read, that's OK; we only need to guarantee
	 * that they're read before the packed refs, not *how much*
	 * before.) After that, we call get_packed_ref_cache(), which
	 * internally checks whether the packed-ref cache is up to
	 * date with what is on disk, and re-reads it if not.
	 */

	loose_dir = get_loose_refs(refs);

	if (prefix && *prefix)
		loose_dir = find_containing_dir(loose_dir, prefix, 0);

	if (loose_dir) {
		prime_ref_dir(loose_dir);
		loose_iter = cache_ref_iterator_begin(loose_dir);
	} else {
		/* There's nothing to iterate over. */
		loose_iter = empty_ref_iterator_begin();
	}

	iter->packed_ref_cache = get_packed_ref_cache(refs);
	acquire_packed_ref_cache(iter->packed_ref_cache);
	packed_dir = get_packed_ref_dir(iter->packed_ref_cache);

	if (prefix && *prefix)
		packed_dir = find_containing_dir(packed_dir, prefix, 0);

	if (packed_dir) {
		packed_iter = cache_ref_iterator_begin(packed_dir);
	} else {
		/* There's nothing to iterate over. */
		packed_iter = empty_ref_iterator_begin();
	}

	iter->iter0 = overlay_ref_iterator_begin(loose_iter, packed_iter);
	iter->flags = flags;

	return ref_iterator;
}

/*
 * Verify that the reference locked by lock has the value old_sha1.
 * Fail if the reference doesn't exist and mustexist is set. Return 0
 * on success. On error, write an error message to err, set errno, and
 * return a negative value.
 */
static int verify_lock(struct ref_lock *lock,
		       const unsigned char *old_sha1, int mustexist,
		       struct strbuf *err)
{
	assert(err);

	if (read_ref_full(lock->ref_name,
			  mustexist ? RESOLVE_REF_READING : 0,
			  lock->old_oid.hash, NULL)) {
		if (old_sha1) {
			int save_errno = errno;
			strbuf_addf(err, "can't verify ref '%s'", lock->ref_name);
			errno = save_errno;
			return -1;
		} else {
			oidclr(&lock->old_oid);
			return 0;
		}
	}
	if (old_sha1 && hashcmp(lock->old_oid.hash, old_sha1)) {
		strbuf_addf(err, "ref '%s' is at %s but expected %s",
			    lock->ref_name,
			    oid_to_hex(&lock->old_oid),
			    sha1_to_hex(old_sha1));
		errno = EBUSY;
		return -1;
	}
	return 0;
}

static int remove_empty_directories(struct strbuf *path)
{
	/*
	 * we want to create a file but there is a directory there;
	 * if that is an empty directory (or a directory that contains
	 * only empty directories), remove them.
	 */
	return remove_dir_recursively(path, REMOVE_DIR_EMPTY_ONLY);
}

static int create_reflock(const char *path, void *cb)
{
	struct lock_file *lk = cb;

	return hold_lock_file_for_update(lk, path, LOCK_NO_DEREF) < 0 ? -1 : 0;
}

/*
 * Locks a ref returning the lock on success and NULL on failure.
 * On failure errno is set to something meaningful.
 */
static struct ref_lock *lock_ref_sha1_basic(struct files_ref_store *refs,
					    const char *refname,
					    const unsigned char *old_sha1,
					    const struct string_list *extras,
					    const struct string_list *skip,
					    unsigned int flags, int *type,
					    struct strbuf *err)
{
	struct strbuf ref_file = STRBUF_INIT;
	struct ref_lock *lock;
	int last_errno = 0;
	int mustexist = (old_sha1 && !is_null_sha1(old_sha1));
	int resolve_flags = RESOLVE_REF_NO_RECURSE;
	int resolved;

	files_assert_main_repository(refs, "lock_ref_sha1_basic");
	assert(err);

	lock = xcalloc(1, sizeof(struct ref_lock));

	if (mustexist)
		resolve_flags |= RESOLVE_REF_READING;
	if (flags & REF_DELETING)
		resolve_flags |= RESOLVE_REF_ALLOW_BAD_NAME;

	strbuf_git_path(&ref_file, "%s", refname);
	resolved = !!resolve_ref_unsafe(refname, resolve_flags,
					lock->old_oid.hash, type);
	if (!resolved && errno == EISDIR) {
		/*
		 * we are trying to lock foo but we used to
		 * have foo/bar which now does not exist;
		 * it is normal for the empty directory 'foo'
		 * to remain.
		 */
		if (remove_empty_directories(&ref_file)) {
			last_errno = errno;
			if (!verify_refname_available_dir(
					    refname, extras, skip,
					    get_loose_refs(refs), err))
				strbuf_addf(err, "there are still refs under '%s'",
					    refname);
			goto error_return;
		}
		resolved = !!resolve_ref_unsafe(refname, resolve_flags,
						lock->old_oid.hash, type);
	}
	if (!resolved) {
		last_errno = errno;
		if (last_errno != ENOTDIR ||
		    !verify_refname_available_dir(
				    refname, extras, skip,
				    get_loose_refs(refs), err))
			strbuf_addf(err, "unable to resolve reference '%s': %s",
				    refname, strerror(last_errno));

		goto error_return;
	}

	/*
	 * If the ref did not exist and we are creating it, make sure
	 * there is no existing packed ref whose name begins with our
	 * refname, nor a packed ref whose name is a proper prefix of
	 * our refname.
	 */
	if (is_null_oid(&lock->old_oid) &&
	    verify_refname_available_dir(refname, extras, skip,
					 get_packed_refs(refs),
					 err)) {
		last_errno = ENOTDIR;
		goto error_return;
	}

	lock->lk = xcalloc(1, sizeof(struct lock_file));

	lock->ref_name = xstrdup(refname);

	if (raceproof_create_file(ref_file.buf, create_reflock, lock->lk)) {
		last_errno = errno;
		unable_to_lock_message(ref_file.buf, errno, err);
		goto error_return;
	}

	if (verify_lock(lock, old_sha1, mustexist, err)) {
		last_errno = errno;
		goto error_return;
	}
	goto out;

 error_return:
	unlock_ref(lock);
	lock = NULL;

 out:
	strbuf_release(&ref_file);
	errno = last_errno;
	return lock;
}

/*
 * Write an entry to the packed-refs file for the specified refname.
 * If peeled is non-NULL, write it as the entry's peeled value.
 */
static void write_packed_entry(FILE *fh, char *refname, unsigned char *sha1,
			       unsigned char *peeled)
{
	fprintf_or_die(fh, "%s %s\n", sha1_to_hex(sha1), refname);
	if (peeled)
		fprintf_or_die(fh, "^%s\n", sha1_to_hex(peeled));
}

/*
 * An each_ref_entry_fn that writes the entry to a packed-refs file.
 */
static int write_packed_entry_fn(struct ref_entry *entry, void *cb_data)
{
	enum peel_status peel_status = peel_entry(entry, 0);

	if (peel_status != PEEL_PEELED && peel_status != PEEL_NON_TAG)
		error("internal error: %s is not a valid packed reference!",
		      entry->name);
	write_packed_entry(cb_data, entry->name, entry->u.value.oid.hash,
			   peel_status == PEEL_PEELED ?
			   entry->u.value.peeled.hash : NULL);
	return 0;
}

/*
 * Lock the packed-refs file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, set
 * errno appropriately and return a nonzero value.
 */
static int lock_packed_refs(struct files_ref_store *refs, int flags)
{
	static int timeout_configured = 0;
	static int timeout_value = 1000;
	struct packed_ref_cache *packed_ref_cache;

	files_assert_main_repository(refs, "lock_packed_refs");

	if (!timeout_configured) {
		git_config_get_int("core.packedrefstimeout", &timeout_value);
		timeout_configured = 1;
	}

	if (hold_lock_file_for_update_timeout(
			    &packlock, git_path("packed-refs"),
			    flags, timeout_value) < 0)
		return -1;
	/*
	 * Get the current packed-refs while holding the lock.  If the
	 * packed-refs file has been modified since we last read it,
	 * this will automatically invalidate the cache and re-read
	 * the packed-refs file.
	 */
	packed_ref_cache = get_packed_ref_cache(refs);
	packed_ref_cache->lock = &packlock;
	/* Increment the reference count to prevent it from being freed: */
	acquire_packed_ref_cache(packed_ref_cache);
	return 0;
}

/*
 * Write the current version of the packed refs cache from memory to
 * disk. The packed-refs file must already be locked for writing (see
 * lock_packed_refs()). Return zero on success. On errors, set errno
 * and return a nonzero value
 */
static int commit_packed_refs(struct files_ref_store *refs)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(refs);
	int error = 0;
	int save_errno = 0;
	FILE *out;

	files_assert_main_repository(refs, "commit_packed_refs");

	if (!packed_ref_cache->lock)
		die("internal error: packed-refs not locked");

	out = fdopen_lock_file(packed_ref_cache->lock, "w");
	if (!out)
		die_errno("unable to fdopen packed-refs descriptor");

	fprintf_or_die(out, "%s", PACKED_REFS_HEADER);
	do_for_each_entry_in_dir(get_packed_ref_dir(packed_ref_cache),
				 0, write_packed_entry_fn, out);

	if (commit_lock_file(packed_ref_cache->lock)) {
		save_errno = errno;
		error = -1;
	}
	packed_ref_cache->lock = NULL;
	release_packed_ref_cache(packed_ref_cache);
	errno = save_errno;
	return error;
}

/*
 * Rollback the lockfile for the packed-refs file, and discard the
 * in-memory packed reference cache.  (The packed-refs file will be
 * read anew if it is needed again after this function is called.)
 */
static void rollback_packed_refs(struct files_ref_store *refs)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(refs);

	files_assert_main_repository(refs, "rollback_packed_refs");

	if (!packed_ref_cache->lock)
		die("internal error: packed-refs not locked");
	rollback_lock_file(packed_ref_cache->lock);
	packed_ref_cache->lock = NULL;
	release_packed_ref_cache(packed_ref_cache);
	clear_packed_ref_cache(refs);
}

struct ref_to_prune {
	struct ref_to_prune *next;
	unsigned char sha1[20];
	char name[FLEX_ARRAY];
};

struct pack_refs_cb_data {
	unsigned int flags;
	struct ref_dir *packed_refs;
	struct ref_to_prune *ref_to_prune;
};

/*
 * An each_ref_entry_fn that is run over loose references only.  If
 * the loose reference can be packed, add an entry in the packed ref
 * cache.  If the reference should be pruned, also add it to
 * ref_to_prune in the pack_refs_cb_data.
 */
static int pack_if_possible_fn(struct ref_entry *entry, void *cb_data)
{
	struct pack_refs_cb_data *cb = cb_data;
	enum peel_status peel_status;
	struct ref_entry *packed_entry;
	int is_tag_ref = starts_with(entry->name, "refs/tags/");

	/* Do not pack per-worktree refs: */
	if (ref_type(entry->name) != REF_TYPE_NORMAL)
		return 0;

	/* ALWAYS pack tags */
	if (!(cb->flags & PACK_REFS_ALL) && !is_tag_ref)
		return 0;

	/* Do not pack symbolic or broken refs: */
	if ((entry->flag & REF_ISSYMREF) || !entry_resolves_to_object(entry))
		return 0;

	/* Add a packed ref cache entry equivalent to the loose entry. */
	peel_status = peel_entry(entry, 1);
	if (peel_status != PEEL_PEELED && peel_status != PEEL_NON_TAG)
		die("internal error peeling reference %s (%s)",
		    entry->name, oid_to_hex(&entry->u.value.oid));
	packed_entry = find_ref(cb->packed_refs, entry->name);
	if (packed_entry) {
		/* Overwrite existing packed entry with info from loose entry */
		packed_entry->flag = REF_ISPACKED | REF_KNOWS_PEELED;
		oidcpy(&packed_entry->u.value.oid, &entry->u.value.oid);
	} else {
		packed_entry = create_ref_entry(entry->name, entry->u.value.oid.hash,
						REF_ISPACKED | REF_KNOWS_PEELED, 0);
		add_ref(cb->packed_refs, packed_entry);
	}
	oidcpy(&packed_entry->u.value.peeled, &entry->u.value.peeled);

	/* Schedule the loose reference for pruning if requested. */
	if ((cb->flags & PACK_REFS_PRUNE)) {
		struct ref_to_prune *n;
		FLEX_ALLOC_STR(n, name, entry->name);
		hashcpy(n->sha1, entry->u.value.oid.hash);
		n->next = cb->ref_to_prune;
		cb->ref_to_prune = n;
	}
	return 0;
}

enum {
	REMOVE_EMPTY_PARENTS_REF = 0x01,
	REMOVE_EMPTY_PARENTS_REFLOG = 0x02
};

/*
 * Remove empty parent directories associated with the specified
 * reference and/or its reflog, but spare [logs/]refs/ and immediate
 * subdirs. flags is a combination of REMOVE_EMPTY_PARENTS_REF and/or
 * REMOVE_EMPTY_PARENTS_REFLOG.
 */
static void try_remove_empty_parents(const char *refname, unsigned int flags)
{
	struct strbuf buf = STRBUF_INIT;
	char *p, *q;
	int i;

	strbuf_addstr(&buf, refname);
	p = buf.buf;
	for (i = 0; i < 2; i++) { /* refs/{heads,tags,...}/ */
		while (*p && *p != '/')
			p++;
		/* tolerate duplicate slashes; see check_refname_format() */
		while (*p == '/')
			p++;
	}
	q = buf.buf + buf.len;
	while (flags & (REMOVE_EMPTY_PARENTS_REF | REMOVE_EMPTY_PARENTS_REFLOG)) {
		while (q > p && *q != '/')
			q--;
		while (q > p && *(q-1) == '/')
			q--;
		if (q == p)
			break;
		strbuf_setlen(&buf, q - buf.buf);
		if ((flags & REMOVE_EMPTY_PARENTS_REF) &&
		    rmdir(git_path("%s", buf.buf)))
			flags &= ~REMOVE_EMPTY_PARENTS_REF;
		if ((flags & REMOVE_EMPTY_PARENTS_REFLOG) &&
		    rmdir(git_path("logs/%s", buf.buf)))
			flags &= ~REMOVE_EMPTY_PARENTS_REFLOG;
	}
	strbuf_release(&buf);
}

/* make sure nobody touched the ref, and unlink */
static void prune_ref(struct ref_to_prune *r)
{
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;

	if (check_refname_format(r->name, 0))
		return;

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_delete(transaction, r->name, r->sha1,
				   REF_ISPRUNING | REF_NODEREF, NULL, &err) ||
	    ref_transaction_commit(transaction, &err)) {
		ref_transaction_free(transaction);
		error("%s", err.buf);
		strbuf_release(&err);
		return;
	}
	ref_transaction_free(transaction);
	strbuf_release(&err);
}

static void prune_refs(struct ref_to_prune *r)
{
	while (r) {
		prune_ref(r);
		r = r->next;
	}
}

static int files_pack_refs(struct ref_store *ref_store, unsigned int flags)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "pack_refs");
	struct pack_refs_cb_data cbdata;

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.flags = flags;

	lock_packed_refs(refs, LOCK_DIE_ON_ERROR);
	cbdata.packed_refs = get_packed_refs(refs);

	do_for_each_entry_in_dir(get_loose_refs(refs), 0,
				 pack_if_possible_fn, &cbdata);

	if (commit_packed_refs(refs))
		die_errno("unable to overwrite old ref-pack file");

	prune_refs(cbdata.ref_to_prune);
	return 0;
}

/*
 * Rewrite the packed-refs file, omitting any refs listed in
 * 'refnames'. On error, leave packed-refs unchanged, write an error
 * message to 'err', and return a nonzero value.
 *
 * The refs in 'refnames' needn't be sorted. `err` must not be NULL.
 */
static int repack_without_refs(struct files_ref_store *refs,
			       struct string_list *refnames, struct strbuf *err)
{
	struct ref_dir *packed;
	struct string_list_item *refname;
	int ret, needs_repacking = 0, removed = 0;

	files_assert_main_repository(refs, "repack_without_refs");
	assert(err);

	/* Look for a packed ref */
	for_each_string_list_item(refname, refnames) {
		if (get_packed_ref(refs, refname->string)) {
			needs_repacking = 1;
			break;
		}
	}

	/* Avoid locking if we have nothing to do */
	if (!needs_repacking)
		return 0; /* no refname exists in packed refs */

	if (lock_packed_refs(refs, 0)) {
		unable_to_lock_message(git_path("packed-refs"), errno, err);
		return -1;
	}
	packed = get_packed_refs(refs);

	/* Remove refnames from the cache */
	for_each_string_list_item(refname, refnames)
		if (remove_entry(packed, refname->string) != -1)
			removed = 1;
	if (!removed) {
		/*
		 * All packed entries disappeared while we were
		 * acquiring the lock.
		 */
		rollback_packed_refs(refs);
		return 0;
	}

	/* Write what remains */
	ret = commit_packed_refs(refs);
	if (ret)
		strbuf_addf(err, "unable to overwrite old ref-pack file: %s",
			    strerror(errno));
	return ret;
}

static int files_delete_refs(struct ref_store *ref_store,
			     struct string_list *refnames, unsigned int flags)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "delete_refs");
	struct strbuf err = STRBUF_INIT;
	int i, result = 0;

	if (!refnames->nr)
		return 0;

	result = repack_without_refs(refs, refnames, &err);
	if (result) {
		/*
		 * If we failed to rewrite the packed-refs file, then
		 * it is unsafe to try to remove loose refs, because
		 * doing so might expose an obsolete packed value for
		 * a reference that might even point at an object that
		 * has been garbage collected.
		 */
		if (refnames->nr == 1)
			error(_("could not delete reference %s: %s"),
			      refnames->items[0].string, err.buf);
		else
			error(_("could not delete references: %s"), err.buf);

		goto out;
	}

	for (i = 0; i < refnames->nr; i++) {
		const char *refname = refnames->items[i].string;

		if (delete_ref(refname, NULL, flags))
			result |= error(_("could not remove reference %s"), refname);
	}

out:
	strbuf_release(&err);
	return result;
}

/*
 * People using contrib's git-new-workdir have .git/logs/refs ->
 * /some/other/path/.git/logs/refs, and that may live on another device.
 *
 * IOW, to avoid cross device rename errors, the temporary renamed log must
 * live into logs/refs.
 */
#define TMP_RENAMED_LOG  "logs/refs/.tmp-renamed-log"

static int rename_tmp_log_callback(const char *path, void *cb)
{
	int *true_errno = cb;

	if (rename(git_path(TMP_RENAMED_LOG), path)) {
		/*
		 * rename(a, b) when b is an existing directory ought
		 * to result in ISDIR, but Solaris 5.8 gives ENOTDIR.
		 * Sheesh. Record the true errno for error reporting,
		 * but report EISDIR to raceproof_create_file() so
		 * that it knows to retry.
		 */
		*true_errno = errno;
		if (errno == ENOTDIR)
			errno = EISDIR;
		return -1;
	} else {
		return 0;
	}
}

static int rename_tmp_log(const char *newrefname)
{
	char *path = git_pathdup("logs/%s", newrefname);
	int ret, true_errno;

	ret = raceproof_create_file(path, rename_tmp_log_callback, &true_errno);
	if (ret) {
		if (errno == EISDIR)
			error("directory not empty: %s", path);
		else
			error("unable to move logfile %s to %s: %s",
			      git_path(TMP_RENAMED_LOG), path,
			      strerror(true_errno));
	}

	free(path);
	return ret;
}

static int files_verify_refname_available(struct ref_store *ref_store,
					  const char *newname,
					  const struct string_list *extras,
					  const struct string_list *skip,
					  struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 1, "verify_refname_available");
	struct ref_dir *packed_refs = get_packed_refs(refs);
	struct ref_dir *loose_refs = get_loose_refs(refs);

	if (verify_refname_available_dir(newname, extras, skip,
					 packed_refs, err) ||
	    verify_refname_available_dir(newname, extras, skip,
					 loose_refs, err))
		return -1;

	return 0;
}

static int write_ref_to_lockfile(struct ref_lock *lock,
				 const unsigned char *sha1, struct strbuf *err);
static int commit_ref_update(struct files_ref_store *refs,
			     struct ref_lock *lock,
			     const unsigned char *sha1, const char *logmsg,
			     struct strbuf *err);

static int files_rename_ref(struct ref_store *ref_store,
			    const char *oldrefname, const char *newrefname,
			    const char *logmsg)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "rename_ref");
	unsigned char sha1[20], orig_sha1[20];
	int flag = 0, logmoved = 0;
	struct ref_lock *lock;
	struct stat loginfo;
	int log = !lstat(git_path("logs/%s", oldrefname), &loginfo);
	struct strbuf err = STRBUF_INIT;

	if (log && S_ISLNK(loginfo.st_mode))
		return error("reflog for %s is a symlink", oldrefname);

	if (!resolve_ref_unsafe(oldrefname, RESOLVE_REF_READING | RESOLVE_REF_NO_RECURSE,
				orig_sha1, &flag))
		return error("refname %s not found", oldrefname);

	if (flag & REF_ISSYMREF)
		return error("refname %s is a symbolic ref, renaming it is not supported",
			oldrefname);
	if (!rename_ref_available(oldrefname, newrefname))
		return 1;

	if (log && rename(git_path("logs/%s", oldrefname), git_path(TMP_RENAMED_LOG)))
		return error("unable to move logfile logs/%s to "TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));

	if (delete_ref(oldrefname, orig_sha1, REF_NODEREF)) {
		error("unable to delete old %s", oldrefname);
		goto rollback;
	}

	/*
	 * Since we are doing a shallow lookup, sha1 is not the
	 * correct value to pass to delete_ref as old_sha1. But that
	 * doesn't matter, because an old_sha1 check wouldn't add to
	 * the safety anyway; we want to delete the reference whatever
	 * its current value.
	 */
	if (!read_ref_full(newrefname, RESOLVE_REF_READING | RESOLVE_REF_NO_RECURSE,
			   sha1, NULL) &&
	    delete_ref(newrefname, NULL, REF_NODEREF)) {
		if (errno == EISDIR) {
			struct strbuf path = STRBUF_INIT;
			int result;

			strbuf_git_path(&path, "%s", newrefname);
			result = remove_empty_directories(&path);
			strbuf_release(&path);

			if (result) {
				error("Directory not empty: %s", newrefname);
				goto rollback;
			}
		} else {
			error("unable to delete existing %s", newrefname);
			goto rollback;
		}
	}

	if (log && rename_tmp_log(newrefname))
		goto rollback;

	logmoved = log;

	lock = lock_ref_sha1_basic(refs, newrefname, NULL, NULL, NULL,
				   REF_NODEREF, NULL, &err);
	if (!lock) {
		error("unable to rename '%s' to '%s': %s", oldrefname, newrefname, err.buf);
		strbuf_release(&err);
		goto rollback;
	}
	hashcpy(lock->old_oid.hash, orig_sha1);

	if (write_ref_to_lockfile(lock, orig_sha1, &err) ||
	    commit_ref_update(refs, lock, orig_sha1, logmsg, &err)) {
		error("unable to write current sha1 into %s: %s", newrefname, err.buf);
		strbuf_release(&err);
		goto rollback;
	}

	return 0;

 rollback:
	lock = lock_ref_sha1_basic(refs, oldrefname, NULL, NULL, NULL,
				   REF_NODEREF, NULL, &err);
	if (!lock) {
		error("unable to lock %s for rollback: %s", oldrefname, err.buf);
		strbuf_release(&err);
		goto rollbacklog;
	}

	flag = log_all_ref_updates;
	log_all_ref_updates = LOG_REFS_NONE;
	if (write_ref_to_lockfile(lock, orig_sha1, &err) ||
	    commit_ref_update(refs, lock, orig_sha1, NULL, &err)) {
		error("unable to write current sha1 into %s: %s", oldrefname, err.buf);
		strbuf_release(&err);
	}
	log_all_ref_updates = flag;

 rollbacklog:
	if (logmoved && rename(git_path("logs/%s", newrefname), git_path("logs/%s", oldrefname)))
		error("unable to restore logfile %s from %s: %s",
			oldrefname, newrefname, strerror(errno));
	if (!logmoved && log &&
	    rename(git_path(TMP_RENAMED_LOG), git_path("logs/%s", oldrefname)))
		error("unable to restore logfile %s from "TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));

	return 1;
}

static int close_ref(struct ref_lock *lock)
{
	if (close_lock_file(lock->lk))
		return -1;
	return 0;
}

static int commit_ref(struct ref_lock *lock)
{
	char *path = get_locked_file_path(lock->lk);
	struct stat st;

	if (!lstat(path, &st) && S_ISDIR(st.st_mode)) {
		/*
		 * There is a directory at the path we want to rename
		 * the lockfile to. Hopefully it is empty; try to
		 * delete it.
		 */
		size_t len = strlen(path);
		struct strbuf sb_path = STRBUF_INIT;

		strbuf_attach(&sb_path, path, len, len);

		/*
		 * If this fails, commit_lock_file() will also fail
		 * and will report the problem.
		 */
		remove_empty_directories(&sb_path);
		strbuf_release(&sb_path);
	} else {
		free(path);
	}

	if (commit_lock_file(lock->lk))
		return -1;
	return 0;
}

static int open_or_create_logfile(const char *path, void *cb)
{
	int *fd = cb;

	*fd = open(path, O_APPEND | O_WRONLY | O_CREAT, 0666);
	return (*fd < 0) ? -1 : 0;
}

/*
 * Create a reflog for a ref. If force_create = 0, only create the
 * reflog for certain refs (those for which should_autocreate_reflog
 * returns non-zero). Otherwise, create it regardless of the reference
 * name. If the logfile already existed or was created, return 0 and
 * set *logfd to the file descriptor opened for appending to the file.
 * If no logfile exists and we decided not to create one, return 0 and
 * set *logfd to -1. On failure, fill in *err, set *logfd to -1, and
 * return -1.
 */
static int log_ref_setup(const char *refname, int force_create,
			 int *logfd, struct strbuf *err)
{
	char *logfile = git_pathdup("logs/%s", refname);

	if (force_create || should_autocreate_reflog(refname)) {
		if (raceproof_create_file(logfile, open_or_create_logfile, logfd)) {
			if (errno == ENOENT)
				strbuf_addf(err, "unable to create directory for '%s': "
					    "%s", logfile, strerror(errno));
			else if (errno == EISDIR)
				strbuf_addf(err, "there are still logs under '%s'",
					    logfile);
			else
				strbuf_addf(err, "unable to append to '%s': %s",
					    logfile, strerror(errno));

			goto error;
		}
	} else {
		*logfd = open(logfile, O_APPEND | O_WRONLY, 0666);
		if (*logfd < 0) {
			if (errno == ENOENT || errno == EISDIR) {
				/*
				 * The logfile doesn't already exist,
				 * but that is not an error; it only
				 * means that we won't write log
				 * entries to it.
				 */
				;
			} else {
				strbuf_addf(err, "unable to append to '%s': %s",
					    logfile, strerror(errno));
				goto error;
			}
		}
	}

	if (*logfd >= 0)
		adjust_shared_perm(logfile);

	free(logfile);
	return 0;

error:
	free(logfile);
	return -1;
}

static int files_create_reflog(struct ref_store *ref_store,
			       const char *refname, int force_create,
			       struct strbuf *err)
{
	int fd;

	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "create_reflog");

	if (log_ref_setup(refname, force_create, &fd, err))
		return -1;

	if (fd >= 0)
		close(fd);

	return 0;
}

static int log_ref_write_fd(int fd, const unsigned char *old_sha1,
			    const unsigned char *new_sha1,
			    const char *committer, const char *msg)
{
	int msglen, written;
	unsigned maxlen, len;
	char *logrec;

	msglen = msg ? strlen(msg) : 0;
	maxlen = strlen(committer) + msglen + 100;
	logrec = xmalloc(maxlen);
	len = xsnprintf(logrec, maxlen, "%s %s %s\n",
			sha1_to_hex(old_sha1),
			sha1_to_hex(new_sha1),
			committer);
	if (msglen)
		len += copy_reflog_msg(logrec + len - 1, msg) - 1;

	written = len <= maxlen ? write_in_full(fd, logrec, len) : -1;
	free(logrec);
	if (written != len)
		return -1;

	return 0;
}

int files_log_ref_write(const char *refname, const unsigned char *old_sha1,
			const unsigned char *new_sha1, const char *msg,
			int flags, struct strbuf *err)
{
	int logfd, result;

	if (log_all_ref_updates == LOG_REFS_UNSET)
		log_all_ref_updates = is_bare_repository() ? LOG_REFS_NONE : LOG_REFS_NORMAL;

	result = log_ref_setup(refname, flags & REF_FORCE_CREATE_REFLOG,
			       &logfd, err);

	if (result)
		return result;

	if (logfd < 0)
		return 0;
	result = log_ref_write_fd(logfd, old_sha1, new_sha1,
				  git_committer_info(0), msg);
	if (result) {
		int save_errno = errno;

		strbuf_addf(err, "unable to append to '%s': %s",
			    git_path("logs/%s", refname), strerror(save_errno));
		close(logfd);
		return -1;
	}
	if (close(logfd)) {
		int save_errno = errno;

		strbuf_addf(err, "unable to append to '%s': %s",
			    git_path("logs/%s", refname), strerror(save_errno));
		return -1;
	}
	return 0;
}

/*
 * Write sha1 into the open lockfile, then close the lockfile. On
 * errors, rollback the lockfile, fill in *err and
 * return -1.
 */
static int write_ref_to_lockfile(struct ref_lock *lock,
				 const unsigned char *sha1, struct strbuf *err)
{
	static char term = '\n';
	struct object *o;
	int fd;

	o = parse_object(sha1);
	if (!o) {
		strbuf_addf(err,
			    "trying to write ref '%s' with nonexistent object %s",
			    lock->ref_name, sha1_to_hex(sha1));
		unlock_ref(lock);
		return -1;
	}
	if (o->type != OBJ_COMMIT && is_branch(lock->ref_name)) {
		strbuf_addf(err,
			    "trying to write non-commit object %s to branch '%s'",
			    sha1_to_hex(sha1), lock->ref_name);
		unlock_ref(lock);
		return -1;
	}
	fd = get_lock_file_fd(lock->lk);
	if (write_in_full(fd, sha1_to_hex(sha1), 40) != 40 ||
	    write_in_full(fd, &term, 1) != 1 ||
	    close_ref(lock) < 0) {
		strbuf_addf(err,
			    "couldn't write '%s'", get_lock_file_path(lock->lk));
		unlock_ref(lock);
		return -1;
	}
	return 0;
}

/*
 * Commit a change to a loose reference that has already been written
 * to the loose reference lockfile. Also update the reflogs if
 * necessary, using the specified lockmsg (which can be NULL).
 */
static int commit_ref_update(struct files_ref_store *refs,
			     struct ref_lock *lock,
			     const unsigned char *sha1, const char *logmsg,
			     struct strbuf *err)
{
	files_assert_main_repository(refs, "commit_ref_update");

	clear_loose_ref_cache(refs);
	if (files_log_ref_write(lock->ref_name, lock->old_oid.hash, sha1,
				logmsg, 0, err)) {
		char *old_msg = strbuf_detach(err, NULL);
		strbuf_addf(err, "cannot update the ref '%s': %s",
			    lock->ref_name, old_msg);
		free(old_msg);
		unlock_ref(lock);
		return -1;
	}

	if (strcmp(lock->ref_name, "HEAD") != 0) {
		/*
		 * Special hack: If a branch is updated directly and HEAD
		 * points to it (may happen on the remote side of a push
		 * for example) then logically the HEAD reflog should be
		 * updated too.
		 * A generic solution implies reverse symref information,
		 * but finding all symrefs pointing to the given branch
		 * would be rather costly for this rare event (the direct
		 * update of a branch) to be worth it.  So let's cheat and
		 * check with HEAD only which should cover 99% of all usage
		 * scenarios (even 100% of the default ones).
		 */
		unsigned char head_sha1[20];
		int head_flag;
		const char *head_ref;

		head_ref = resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
					      head_sha1, &head_flag);
		if (head_ref && (head_flag & REF_ISSYMREF) &&
		    !strcmp(head_ref, lock->ref_name)) {
			struct strbuf log_err = STRBUF_INIT;
			if (files_log_ref_write("HEAD", lock->old_oid.hash, sha1,
					  logmsg, 0, &log_err)) {
				error("%s", log_err.buf);
				strbuf_release(&log_err);
			}
		}
	}

	if (commit_ref(lock)) {
		strbuf_addf(err, "couldn't set '%s'", lock->ref_name);
		unlock_ref(lock);
		return -1;
	}

	unlock_ref(lock);
	return 0;
}

static int create_ref_symlink(struct ref_lock *lock, const char *target)
{
	int ret = -1;
#ifndef NO_SYMLINK_HEAD
	char *ref_path = get_locked_file_path(lock->lk);
	unlink(ref_path);
	ret = symlink(target, ref_path);
	free(ref_path);

	if (ret)
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
#endif
	return ret;
}

static void update_symref_reflog(struct ref_lock *lock, const char *refname,
				 const char *target, const char *logmsg)
{
	struct strbuf err = STRBUF_INIT;
	unsigned char new_sha1[20];
	if (logmsg && !read_ref(target, new_sha1) &&
	    files_log_ref_write(refname, lock->old_oid.hash, new_sha1,
				logmsg, 0, &err)) {
		error("%s", err.buf);
		strbuf_release(&err);
	}
}

static int create_symref_locked(struct ref_lock *lock, const char *refname,
				const char *target, const char *logmsg)
{
	if (prefer_symlink_refs && !create_ref_symlink(lock, target)) {
		update_symref_reflog(lock, refname, target, logmsg);
		return 0;
	}

	if (!fdopen_lock_file(lock->lk, "w"))
		return error("unable to fdopen %s: %s",
			     lock->lk->tempfile.filename.buf, strerror(errno));

	update_symref_reflog(lock, refname, target, logmsg);

	/* no error check; commit_ref will check ferror */
	fprintf(lock->lk->tempfile.fp, "ref: %s\n", target);
	if (commit_ref(lock) < 0)
		return error("unable to write symref for %s: %s", refname,
			     strerror(errno));
	return 0;
}

static int files_create_symref(struct ref_store *ref_store,
			       const char *refname, const char *target,
			       const char *logmsg)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "create_symref");
	struct strbuf err = STRBUF_INIT;
	struct ref_lock *lock;
	int ret;

	lock = lock_ref_sha1_basic(refs, refname, NULL,
				   NULL, NULL, REF_NODEREF, NULL,
				   &err);
	if (!lock) {
		error("%s", err.buf);
		strbuf_release(&err);
		return -1;
	}

	ret = create_symref_locked(lock, refname, target, logmsg);
	unlock_ref(lock);
	return ret;
}

int set_worktree_head_symref(const char *gitdir, const char *target)
{
	static struct lock_file head_lock;
	struct ref_lock *lock;
	struct strbuf head_path = STRBUF_INIT;
	const char *head_rel;
	int ret;

	strbuf_addf(&head_path, "%s/HEAD", absolute_path(gitdir));
	if (hold_lock_file_for_update(&head_lock, head_path.buf,
				      LOCK_NO_DEREF) < 0) {
		struct strbuf err = STRBUF_INIT;
		unable_to_lock_message(head_path.buf, errno, &err);
		error("%s", err.buf);
		strbuf_release(&err);
		strbuf_release(&head_path);
		return -1;
	}

	/* head_rel will be "HEAD" for the main tree, "worktrees/wt/HEAD" for
	   linked trees */
	head_rel = remove_leading_path(head_path.buf,
				       absolute_path(get_git_common_dir()));
	/* to make use of create_symref_locked(), initialize ref_lock */
	lock = xcalloc(1, sizeof(struct ref_lock));
	lock->lk = &head_lock;
	lock->ref_name = xstrdup(head_rel);

	ret = create_symref_locked(lock, head_rel, target, NULL);

	unlock_ref(lock); /* will free lock */
	strbuf_release(&head_path);
	return ret;
}

static int files_reflog_exists(struct ref_store *ref_store,
			       const char *refname)
{
	struct stat st;

	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "reflog_exists");

	return !lstat(git_path("logs/%s", refname), &st) &&
		S_ISREG(st.st_mode);
}

static int files_delete_reflog(struct ref_store *ref_store,
			       const char *refname)
{
	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "delete_reflog");

	return remove_path(git_path("logs/%s", refname));
}

static int show_one_reflog_ent(struct strbuf *sb, each_reflog_ent_fn fn, void *cb_data)
{
	unsigned char osha1[20], nsha1[20];
	char *email_end, *message;
	unsigned long timestamp;
	int tz;

	/* old SP new SP name <email> SP time TAB msg LF */
	if (sb->len < 83 || sb->buf[sb->len - 1] != '\n' ||
	    get_sha1_hex(sb->buf, osha1) || sb->buf[40] != ' ' ||
	    get_sha1_hex(sb->buf + 41, nsha1) || sb->buf[81] != ' ' ||
	    !(email_end = strchr(sb->buf + 82, '>')) ||
	    email_end[1] != ' ' ||
	    !(timestamp = strtoul(email_end + 2, &message, 10)) ||
	    !message || message[0] != ' ' ||
	    (message[1] != '+' && message[1] != '-') ||
	    !isdigit(message[2]) || !isdigit(message[3]) ||
	    !isdigit(message[4]) || !isdigit(message[5]))
		return 0; /* corrupt? */
	email_end[1] = '\0';
	tz = strtol(message + 1, NULL, 10);
	if (message[6] != '\t')
		message += 6;
	else
		message += 7;
	return fn(osha1, nsha1, sb->buf + 82, timestamp, tz, message, cb_data);
}

static char *find_beginning_of_line(char *bob, char *scan)
{
	while (bob < scan && *(--scan) != '\n')
		; /* keep scanning backwards */
	/*
	 * Return either beginning of the buffer, or LF at the end of
	 * the previous line.
	 */
	return scan;
}

static int files_for_each_reflog_ent_reverse(struct ref_store *ref_store,
					     const char *refname,
					     each_reflog_ent_fn fn,
					     void *cb_data)
{
	struct strbuf sb = STRBUF_INIT;
	FILE *logfp;
	long pos;
	int ret = 0, at_tail = 1;

	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "for_each_reflog_ent_reverse");

	logfp = fopen(git_path("logs/%s", refname), "r");
	if (!logfp)
		return -1;

	/* Jump to the end */
	if (fseek(logfp, 0, SEEK_END) < 0)
		return error("cannot seek back reflog for %s: %s",
			     refname, strerror(errno));
	pos = ftell(logfp);
	while (!ret && 0 < pos) {
		int cnt;
		size_t nread;
		char buf[BUFSIZ];
		char *endp, *scanp;

		/* Fill next block from the end */
		cnt = (sizeof(buf) < pos) ? sizeof(buf) : pos;
		if (fseek(logfp, pos - cnt, SEEK_SET))
			return error("cannot seek back reflog for %s: %s",
				     refname, strerror(errno));
		nread = fread(buf, cnt, 1, logfp);
		if (nread != 1)
			return error("cannot read %d bytes from reflog for %s: %s",
				     cnt, refname, strerror(errno));
		pos -= cnt;

		scanp = endp = buf + cnt;
		if (at_tail && scanp[-1] == '\n')
			/* Looking at the final LF at the end of the file */
			scanp--;
		at_tail = 0;

		while (buf < scanp) {
			/*
			 * terminating LF of the previous line, or the beginning
			 * of the buffer.
			 */
			char *bp;

			bp = find_beginning_of_line(buf, scanp);

			if (*bp == '\n') {
				/*
				 * The newline is the end of the previous line,
				 * so we know we have complete line starting
				 * at (bp + 1). Prefix it onto any prior data
				 * we collected for the line and process it.
				 */
				strbuf_splice(&sb, 0, 0, bp + 1, endp - (bp + 1));
				scanp = bp;
				endp = bp + 1;
				ret = show_one_reflog_ent(&sb, fn, cb_data);
				strbuf_reset(&sb);
				if (ret)
					break;
			} else if (!pos) {
				/*
				 * We are at the start of the buffer, and the
				 * start of the file; there is no previous
				 * line, and we have everything for this one.
				 * Process it, and we can end the loop.
				 */
				strbuf_splice(&sb, 0, 0, buf, endp - buf);
				ret = show_one_reflog_ent(&sb, fn, cb_data);
				strbuf_reset(&sb);
				break;
			}

			if (bp == buf) {
				/*
				 * We are at the start of the buffer, and there
				 * is more file to read backwards. Which means
				 * we are in the middle of a line. Note that we
				 * may get here even if *bp was a newline; that
				 * just means we are at the exact end of the
				 * previous line, rather than some spot in the
				 * middle.
				 *
				 * Save away what we have to be combined with
				 * the data from the next read.
				 */
				strbuf_splice(&sb, 0, 0, buf, endp - buf);
				break;
			}
		}

	}
	if (!ret && sb.len)
		die("BUG: reverse reflog parser had leftover data");

	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

static int files_for_each_reflog_ent(struct ref_store *ref_store,
				     const char *refname,
				     each_reflog_ent_fn fn, void *cb_data)
{
	FILE *logfp;
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "for_each_reflog_ent");

	logfp = fopen(git_path("logs/%s", refname), "r");
	if (!logfp)
		return -1;

	while (!ret && !strbuf_getwholeline(&sb, logfp, '\n'))
		ret = show_one_reflog_ent(&sb, fn, cb_data);
	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

struct files_reflog_iterator {
	struct ref_iterator base;

	struct dir_iterator *dir_iterator;
	struct object_id oid;
};

static int files_reflog_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct files_reflog_iterator *iter =
		(struct files_reflog_iterator *)ref_iterator;
	struct dir_iterator *diter = iter->dir_iterator;
	int ok;

	while ((ok = dir_iterator_advance(diter)) == ITER_OK) {
		int flags;

		if (!S_ISREG(diter->st.st_mode))
			continue;
		if (diter->basename[0] == '.')
			continue;
		if (ends_with(diter->basename, ".lock"))
			continue;

		if (read_ref_full(diter->relative_path, 0,
				  iter->oid.hash, &flags)) {
			error("bad ref for %s", diter->path.buf);
			continue;
		}

		iter->base.refname = diter->relative_path;
		iter->base.oid = &iter->oid;
		iter->base.flags = flags;
		return ITER_OK;
	}

	iter->dir_iterator = NULL;
	if (ref_iterator_abort(ref_iterator) == ITER_ERROR)
		ok = ITER_ERROR;
	return ok;
}

static int files_reflog_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	die("BUG: ref_iterator_peel() called for reflog_iterator");
}

static int files_reflog_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct files_reflog_iterator *iter =
		(struct files_reflog_iterator *)ref_iterator;
	int ok = ITER_DONE;

	if (iter->dir_iterator)
		ok = dir_iterator_abort(iter->dir_iterator);

	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable files_reflog_iterator_vtable = {
	files_reflog_iterator_advance,
	files_reflog_iterator_peel,
	files_reflog_iterator_abort
};

static struct ref_iterator *files_reflog_iterator_begin(struct ref_store *ref_store)
{
	struct files_reflog_iterator *iter = xcalloc(1, sizeof(*iter));
	struct ref_iterator *ref_iterator = &iter->base;

	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "reflog_iterator_begin");

	base_ref_iterator_init(ref_iterator, &files_reflog_iterator_vtable);
	iter->dir_iterator = dir_iterator_begin(git_path("logs"));
	return ref_iterator;
}

static int ref_update_reject_duplicates(struct string_list *refnames,
					struct strbuf *err)
{
	int i, n = refnames->nr;

	assert(err);

	for (i = 1; i < n; i++)
		if (!strcmp(refnames->items[i - 1].string, refnames->items[i].string)) {
			strbuf_addf(err,
				    "multiple updates for ref '%s' not allowed.",
				    refnames->items[i].string);
			return 1;
		}
	return 0;
}

/*
 * If update is a direct update of head_ref (the reference pointed to
 * by HEAD), then add an extra REF_LOG_ONLY update for HEAD.
 */
static int split_head_update(struct ref_update *update,
			     struct ref_transaction *transaction,
			     const char *head_ref,
			     struct string_list *affected_refnames,
			     struct strbuf *err)
{
	struct string_list_item *item;
	struct ref_update *new_update;

	if ((update->flags & REF_LOG_ONLY) ||
	    (update->flags & REF_ISPRUNING) ||
	    (update->flags & REF_UPDATE_VIA_HEAD))
		return 0;

	if (strcmp(update->refname, head_ref))
		return 0;

	/*
	 * First make sure that HEAD is not already in the
	 * transaction. This insertion is O(N) in the transaction
	 * size, but it happens at most once per transaction.
	 */
	item = string_list_insert(affected_refnames, "HEAD");
	if (item->util) {
		/* An entry already existed */
		strbuf_addf(err,
			    "multiple updates for 'HEAD' (including one "
			    "via its referent '%s') are not allowed",
			    update->refname);
		return TRANSACTION_NAME_CONFLICT;
	}

	new_update = ref_transaction_add_update(
			transaction, "HEAD",
			update->flags | REF_LOG_ONLY | REF_NODEREF,
			update->new_sha1, update->old_sha1,
			update->msg);

	item->util = new_update;

	return 0;
}

/*
 * update is for a symref that points at referent and doesn't have
 * REF_NODEREF set. Split it into two updates:
 * - The original update, but with REF_LOG_ONLY and REF_NODEREF set
 * - A new, separate update for the referent reference
 * Note that the new update will itself be subject to splitting when
 * the iteration gets to it.
 */
static int split_symref_update(struct files_ref_store *refs,
			       struct ref_update *update,
			       const char *referent,
			       struct ref_transaction *transaction,
			       struct string_list *affected_refnames,
			       struct strbuf *err)
{
	struct string_list_item *item;
	struct ref_update *new_update;
	unsigned int new_flags;

	/*
	 * First make sure that referent is not already in the
	 * transaction. This insertion is O(N) in the transaction
	 * size, but it happens at most once per symref in a
	 * transaction.
	 */
	item = string_list_insert(affected_refnames, referent);
	if (item->util) {
		/* An entry already existed */
		strbuf_addf(err,
			    "multiple updates for '%s' (including one "
			    "via symref '%s') are not allowed",
			    referent, update->refname);
		return TRANSACTION_NAME_CONFLICT;
	}

	new_flags = update->flags;
	if (!strcmp(update->refname, "HEAD")) {
		/*
		 * Record that the new update came via HEAD, so that
		 * when we process it, split_head_update() doesn't try
		 * to add another reflog update for HEAD. Note that
		 * this bit will be propagated if the new_update
		 * itself needs to be split.
		 */
		new_flags |= REF_UPDATE_VIA_HEAD;
	}

	new_update = ref_transaction_add_update(
			transaction, referent, new_flags,
			update->new_sha1, update->old_sha1,
			update->msg);

	new_update->parent_update = update;

	/*
	 * Change the symbolic ref update to log only. Also, it
	 * doesn't need to check its old SHA-1 value, as that will be
	 * done when new_update is processed.
	 */
	update->flags |= REF_LOG_ONLY | REF_NODEREF;
	update->flags &= ~REF_HAVE_OLD;

	item->util = new_update;

	return 0;
}

/*
 * Return the refname under which update was originally requested.
 */
static const char *original_update_refname(struct ref_update *update)
{
	while (update->parent_update)
		update = update->parent_update;

	return update->refname;
}

/*
 * Check whether the REF_HAVE_OLD and old_oid values stored in update
 * are consistent with oid, which is the reference's current value. If
 * everything is OK, return 0; otherwise, write an error message to
 * err and return -1.
 */
static int check_old_oid(struct ref_update *update, struct object_id *oid,
			 struct strbuf *err)
{
	if (!(update->flags & REF_HAVE_OLD) ||
		   !hashcmp(oid->hash, update->old_sha1))
		return 0;

	if (is_null_sha1(update->old_sha1))
		strbuf_addf(err, "cannot lock ref '%s': "
			    "reference already exists",
			    original_update_refname(update));
	else if (is_null_oid(oid))
		strbuf_addf(err, "cannot lock ref '%s': "
			    "reference is missing but expected %s",
			    original_update_refname(update),
			    sha1_to_hex(update->old_sha1));
	else
		strbuf_addf(err, "cannot lock ref '%s': "
			    "is at %s but expected %s",
			    original_update_refname(update),
			    oid_to_hex(oid),
			    sha1_to_hex(update->old_sha1));

	return -1;
}

/*
 * Prepare for carrying out update:
 * - Lock the reference referred to by update.
 * - Read the reference under lock.
 * - Check that its old SHA-1 value (if specified) is correct, and in
 *   any case record it in update->lock->old_oid for later use when
 *   writing the reflog.
 * - If it is a symref update without REF_NODEREF, split it up into a
 *   REF_LOG_ONLY update of the symref and add a separate update for
 *   the referent to transaction.
 * - If it is an update of head_ref, add a corresponding REF_LOG_ONLY
 *   update of HEAD.
 */
static int lock_ref_for_update(struct files_ref_store *refs,
			       struct ref_update *update,
			       struct ref_transaction *transaction,
			       const char *head_ref,
			       struct string_list *affected_refnames,
			       struct strbuf *err)
{
	struct strbuf referent = STRBUF_INIT;
	int mustexist = (update->flags & REF_HAVE_OLD) &&
		!is_null_sha1(update->old_sha1);
	int ret;
	struct ref_lock *lock;

	files_assert_main_repository(refs, "lock_ref_for_update");

	if ((update->flags & REF_HAVE_NEW) && is_null_sha1(update->new_sha1))
		update->flags |= REF_DELETING;

	if (head_ref) {
		ret = split_head_update(update, transaction, head_ref,
					affected_refnames, err);
		if (ret)
			return ret;
	}

	ret = lock_raw_ref(refs, update->refname, mustexist,
			   affected_refnames, NULL,
			   &lock, &referent,
			   &update->type, err);
	if (ret) {
		char *reason;

		reason = strbuf_detach(err, NULL);
		strbuf_addf(err, "cannot lock ref '%s': %s",
			    original_update_refname(update), reason);
		free(reason);
		return ret;
	}

	update->backend_data = lock;

	if (update->type & REF_ISSYMREF) {
		if (update->flags & REF_NODEREF) {
			/*
			 * We won't be reading the referent as part of
			 * the transaction, so we have to read it here
			 * to record and possibly check old_sha1:
			 */
			if (read_ref_full(referent.buf, 0,
					  lock->old_oid.hash, NULL)) {
				if (update->flags & REF_HAVE_OLD) {
					strbuf_addf(err, "cannot lock ref '%s': "
						    "error reading reference",
						    original_update_refname(update));
					return -1;
				}
			} else if (check_old_oid(update, &lock->old_oid, err)) {
				return TRANSACTION_GENERIC_ERROR;
			}
		} else {
			/*
			 * Create a new update for the reference this
			 * symref is pointing at. Also, we will record
			 * and verify old_sha1 for this update as part
			 * of processing the split-off update, so we
			 * don't have to do it here.
			 */
			ret = split_symref_update(refs, update,
						  referent.buf, transaction,
						  affected_refnames, err);
			if (ret)
				return ret;
		}
	} else {
		struct ref_update *parent_update;

		if (check_old_oid(update, &lock->old_oid, err))
			return TRANSACTION_GENERIC_ERROR;

		/*
		 * If this update is happening indirectly because of a
		 * symref update, record the old SHA-1 in the parent
		 * update:
		 */
		for (parent_update = update->parent_update;
		     parent_update;
		     parent_update = parent_update->parent_update) {
			struct ref_lock *parent_lock = parent_update->backend_data;
			oidcpy(&parent_lock->old_oid, &lock->old_oid);
		}
	}

	if ((update->flags & REF_HAVE_NEW) &&
	    !(update->flags & REF_DELETING) &&
	    !(update->flags & REF_LOG_ONLY)) {
		if (!(update->type & REF_ISSYMREF) &&
		    !hashcmp(lock->old_oid.hash, update->new_sha1)) {
			/*
			 * The reference already has the desired
			 * value, so we don't need to write it.
			 */
		} else if (write_ref_to_lockfile(lock, update->new_sha1,
						 err)) {
			char *write_err = strbuf_detach(err, NULL);

			/*
			 * The lock was freed upon failure of
			 * write_ref_to_lockfile():
			 */
			update->backend_data = NULL;
			strbuf_addf(err,
				    "cannot update ref '%s': %s",
				    update->refname, write_err);
			free(write_err);
			return TRANSACTION_GENERIC_ERROR;
		} else {
			update->flags |= REF_NEEDS_COMMIT;
		}
	}
	if (!(update->flags & REF_NEEDS_COMMIT)) {
		/*
		 * We didn't call write_ref_to_lockfile(), so
		 * the lockfile is still open. Close it to
		 * free up the file descriptor:
		 */
		if (close_ref(lock)) {
			strbuf_addf(err, "couldn't close '%s.lock'",
				    update->refname);
			return TRANSACTION_GENERIC_ERROR;
		}
	}
	return 0;
}

static int files_transaction_commit(struct ref_store *ref_store,
				    struct ref_transaction *transaction,
				    struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "ref_transaction_commit");
	int ret = 0, i;
	struct string_list refs_to_delete = STRING_LIST_INIT_NODUP;
	struct string_list_item *ref_to_delete;
	struct string_list affected_refnames = STRING_LIST_INIT_NODUP;
	char *head_ref = NULL;
	int head_type;
	struct object_id head_oid;

	assert(err);

	if (transaction->state != REF_TRANSACTION_OPEN)
		die("BUG: commit called for transaction that is not open");

	if (!transaction->nr) {
		transaction->state = REF_TRANSACTION_CLOSED;
		return 0;
	}

	/*
	 * Fail if a refname appears more than once in the
	 * transaction. (If we end up splitting up any updates using
	 * split_symref_update() or split_head_update(), those
	 * functions will check that the new updates don't have the
	 * same refname as any existing ones.)
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct string_list_item *item =
			string_list_append(&affected_refnames, update->refname);

		/*
		 * We store a pointer to update in item->util, but at
		 * the moment we never use the value of this field
		 * except to check whether it is non-NULL.
		 */
		item->util = update;
	}
	string_list_sort(&affected_refnames);
	if (ref_update_reject_duplicates(&affected_refnames, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	/*
	 * Special hack: If a branch is updated directly and HEAD
	 * points to it (may happen on the remote side of a push
	 * for example) then logically the HEAD reflog should be
	 * updated too.
	 *
	 * A generic solution would require reverse symref lookups,
	 * but finding all symrefs pointing to a given branch would be
	 * rather costly for this rare event (the direct update of a
	 * branch) to be worth it. So let's cheat and check with HEAD
	 * only, which should cover 99% of all usage scenarios (even
	 * 100% of the default ones).
	 *
	 * So if HEAD is a symbolic reference, then record the name of
	 * the reference that it points to. If we see an update of
	 * head_ref within the transaction, then split_head_update()
	 * arranges for the reflog of HEAD to be updated, too.
	 */
	head_ref = resolve_refdup("HEAD", RESOLVE_REF_NO_RECURSE,
				  head_oid.hash, &head_type);

	if (head_ref && !(head_type & REF_ISSYMREF)) {
		free(head_ref);
		head_ref = NULL;
	}

	/*
	 * Acquire all locks, verify old values if provided, check
	 * that new values are valid, and write new values to the
	 * lockfiles, ready to be activated. Only keep one lockfile
	 * open at a time to avoid running out of file descriptors.
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		ret = lock_ref_for_update(refs, update, transaction,
					  head_ref, &affected_refnames, err);
		if (ret)
			goto cleanup;
	}

	/* Perform updates first so live commits remain referenced */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (update->flags & REF_NEEDS_COMMIT ||
		    update->flags & REF_LOG_ONLY) {
			if (files_log_ref_write(lock->ref_name,
						lock->old_oid.hash,
						update->new_sha1,
						update->msg, update->flags,
						err)) {
				char *old_msg = strbuf_detach(err, NULL);

				strbuf_addf(err, "cannot update the ref '%s': %s",
					    lock->ref_name, old_msg);
				free(old_msg);
				unlock_ref(lock);
				update->backend_data = NULL;
				ret = TRANSACTION_GENERIC_ERROR;
				goto cleanup;
			}
		}
		if (update->flags & REF_NEEDS_COMMIT) {
			clear_loose_ref_cache(refs);
			if (commit_ref(lock)) {
				strbuf_addf(err, "couldn't set '%s'", lock->ref_name);
				unlock_ref(lock);
				update->backend_data = NULL;
				ret = TRANSACTION_GENERIC_ERROR;
				goto cleanup;
			}
		}
	}
	/* Perform deletes now that updates are safely completed */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (update->flags & REF_DELETING &&
		    !(update->flags & REF_LOG_ONLY)) {
			if (!(update->type & REF_ISPACKED) ||
			    update->type & REF_ISSYMREF) {
				/* It is a loose reference. */
				if (unlink_or_msg(git_path("%s", lock->ref_name), err)) {
					ret = TRANSACTION_GENERIC_ERROR;
					goto cleanup;
				}
				update->flags |= REF_DELETED_LOOSE;
			}

			if (!(update->flags & REF_ISPRUNING))
				string_list_append(&refs_to_delete,
						   lock->ref_name);
		}
	}

	if (repack_without_refs(refs, &refs_to_delete, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	/* Delete the reflogs of any references that were deleted: */
	for_each_string_list_item(ref_to_delete, &refs_to_delete) {
		if (!unlink_or_warn(git_path("logs/%s", ref_to_delete->string)))
			try_remove_empty_parents(ref_to_delete->string,
						 REMOVE_EMPTY_PARENTS_REFLOG);
	}

	clear_loose_ref_cache(refs);

cleanup:
	transaction->state = REF_TRANSACTION_CLOSED;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (lock)
			unlock_ref(lock);

		if (update->flags & REF_DELETED_LOOSE) {
			/*
			 * The loose reference was deleted. Delete any
			 * empty parent directories. (Note that this
			 * can only work because we have already
			 * removed the lockfile.)
			 */
			try_remove_empty_parents(update->refname,
						 REMOVE_EMPTY_PARENTS_REF);
		}
	}

	string_list_clear(&refs_to_delete, 0);
	free(head_ref);
	string_list_clear(&affected_refnames, 0);

	return ret;
}

static int ref_present(const char *refname,
		       const struct object_id *oid, int flags, void *cb_data)
{
	struct string_list *affected_refnames = cb_data;

	return string_list_has_string(affected_refnames, refname);
}

static int files_initial_transaction_commit(struct ref_store *ref_store,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "initial_ref_transaction_commit");
	int ret = 0, i;
	struct string_list affected_refnames = STRING_LIST_INIT_NODUP;

	assert(err);

	if (transaction->state != REF_TRANSACTION_OPEN)
		die("BUG: commit called for transaction that is not open");

	/* Fail if a refname appears more than once in the transaction: */
	for (i = 0; i < transaction->nr; i++)
		string_list_append(&affected_refnames,
				   transaction->updates[i]->refname);
	string_list_sort(&affected_refnames);
	if (ref_update_reject_duplicates(&affected_refnames, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	/*
	 * It's really undefined to call this function in an active
	 * repository or when there are existing references: we are
	 * only locking and changing packed-refs, so (1) any
	 * simultaneous processes might try to change a reference at
	 * the same time we do, and (2) any existing loose versions of
	 * the references that we are setting would have precedence
	 * over our values. But some remote helpers create the remote
	 * "HEAD" and "master" branches before calling this function,
	 * so here we really only check that none of the references
	 * that we are creating already exists.
	 */
	if (for_each_rawref(ref_present, &affected_refnames))
		die("BUG: initial ref transaction called with existing refs");

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if ((update->flags & REF_HAVE_OLD) &&
		    !is_null_sha1(update->old_sha1))
			die("BUG: initial ref transaction with old_sha1 set");
		if (verify_refname_available(update->refname,
					     &affected_refnames, NULL,
					     err)) {
			ret = TRANSACTION_NAME_CONFLICT;
			goto cleanup;
		}
	}

	if (lock_packed_refs(refs, 0)) {
		strbuf_addf(err, "unable to lock packed-refs file: %s",
			    strerror(errno));
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if ((update->flags & REF_HAVE_NEW) &&
		    !is_null_sha1(update->new_sha1))
			add_packed_ref(refs, update->refname, update->new_sha1);
	}

	if (commit_packed_refs(refs)) {
		strbuf_addf(err, "unable to commit packed-refs file: %s",
			    strerror(errno));
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

cleanup:
	transaction->state = REF_TRANSACTION_CLOSED;
	string_list_clear(&affected_refnames, 0);
	return ret;
}

struct expire_reflog_cb {
	unsigned int flags;
	reflog_expiry_should_prune_fn *should_prune_fn;
	void *policy_cb;
	FILE *newlog;
	unsigned char last_kept_sha1[20];
};

static int expire_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
			     const char *email, unsigned long timestamp, int tz,
			     const char *message, void *cb_data)
{
	struct expire_reflog_cb *cb = cb_data;
	struct expire_reflog_policy_cb *policy_cb = cb->policy_cb;

	if (cb->flags & EXPIRE_REFLOGS_REWRITE)
		osha1 = cb->last_kept_sha1;

	if ((*cb->should_prune_fn)(osha1, nsha1, email, timestamp, tz,
				   message, policy_cb)) {
		if (!cb->newlog)
			printf("would prune %s", message);
		else if (cb->flags & EXPIRE_REFLOGS_VERBOSE)
			printf("prune %s", message);
	} else {
		if (cb->newlog) {
			fprintf(cb->newlog, "%s %s %s %lu %+05d\t%s",
				sha1_to_hex(osha1), sha1_to_hex(nsha1),
				email, timestamp, tz, message);
			hashcpy(cb->last_kept_sha1, nsha1);
		}
		if (cb->flags & EXPIRE_REFLOGS_VERBOSE)
			printf("keep %s", message);
	}
	return 0;
}

static int files_reflog_expire(struct ref_store *ref_store,
			       const char *refname, const unsigned char *sha1,
			       unsigned int flags,
			       reflog_expiry_prepare_fn prepare_fn,
			       reflog_expiry_should_prune_fn should_prune_fn,
			       reflog_expiry_cleanup_fn cleanup_fn,
			       void *policy_cb_data)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "reflog_expire");
	static struct lock_file reflog_lock;
	struct expire_reflog_cb cb;
	struct ref_lock *lock;
	char *log_file;
	int status = 0;
	int type;
	struct strbuf err = STRBUF_INIT;

	memset(&cb, 0, sizeof(cb));
	cb.flags = flags;
	cb.policy_cb = policy_cb_data;
	cb.should_prune_fn = should_prune_fn;

	/*
	 * The reflog file is locked by holding the lock on the
	 * reference itself, plus we might need to update the
	 * reference if --updateref was specified:
	 */
	lock = lock_ref_sha1_basic(refs, refname, sha1,
				   NULL, NULL, REF_NODEREF,
				   &type, &err);
	if (!lock) {
		error("cannot lock ref '%s': %s", refname, err.buf);
		strbuf_release(&err);
		return -1;
	}
	if (!reflog_exists(refname)) {
		unlock_ref(lock);
		return 0;
	}

	log_file = git_pathdup("logs/%s", refname);
	if (!(flags & EXPIRE_REFLOGS_DRY_RUN)) {
		/*
		 * Even though holding $GIT_DIR/logs/$reflog.lock has
		 * no locking implications, we use the lock_file
		 * machinery here anyway because it does a lot of the
		 * work we need, including cleaning up if the program
		 * exits unexpectedly.
		 */
		if (hold_lock_file_for_update(&reflog_lock, log_file, 0) < 0) {
			struct strbuf err = STRBUF_INIT;
			unable_to_lock_message(log_file, errno, &err);
			error("%s", err.buf);
			strbuf_release(&err);
			goto failure;
		}
		cb.newlog = fdopen_lock_file(&reflog_lock, "w");
		if (!cb.newlog) {
			error("cannot fdopen %s (%s)",
			      get_lock_file_path(&reflog_lock), strerror(errno));
			goto failure;
		}
	}

	(*prepare_fn)(refname, sha1, cb.policy_cb);
	for_each_reflog_ent(refname, expire_reflog_ent, &cb);
	(*cleanup_fn)(cb.policy_cb);

	if (!(flags & EXPIRE_REFLOGS_DRY_RUN)) {
		/*
		 * It doesn't make sense to adjust a reference pointed
		 * to by a symbolic ref based on expiring entries in
		 * the symbolic reference's reflog. Nor can we update
		 * a reference if there are no remaining reflog
		 * entries.
		 */
		int update = (flags & EXPIRE_REFLOGS_UPDATE_REF) &&
			!(type & REF_ISSYMREF) &&
			!is_null_sha1(cb.last_kept_sha1);

		if (close_lock_file(&reflog_lock)) {
			status |= error("couldn't write %s: %s", log_file,
					strerror(errno));
		} else if (update &&
			   (write_in_full(get_lock_file_fd(lock->lk),
				sha1_to_hex(cb.last_kept_sha1), 40) != 40 ||
			    write_str_in_full(get_lock_file_fd(lock->lk), "\n") != 1 ||
			    close_ref(lock) < 0)) {
			status |= error("couldn't write %s",
					get_lock_file_path(lock->lk));
			rollback_lock_file(&reflog_lock);
		} else if (commit_lock_file(&reflog_lock)) {
			status |= error("unable to write reflog '%s' (%s)",
					log_file, strerror(errno));
		} else if (update && commit_ref(lock)) {
			status |= error("couldn't set %s", lock->ref_name);
		}
	}
	free(log_file);
	unlock_ref(lock);
	return status;

 failure:
	rollback_lock_file(&reflog_lock);
	free(log_file);
	unlock_ref(lock);
	return -1;
}

static int files_init_db(struct ref_store *ref_store, struct strbuf *err)
{
	/* Check validity (but we don't need the result): */
	files_downcast(ref_store, 0, "init_db");

	/*
	 * Create .git/refs/{heads,tags}
	 */
	safe_create_dir(git_path("refs/heads"), 1);
	safe_create_dir(git_path("refs/tags"), 1);
	if (get_shared_repository()) {
		adjust_shared_perm(git_path("refs/heads"));
		adjust_shared_perm(git_path("refs/tags"));
	}
	return 0;
}

struct ref_storage_be refs_be_files = {
	NULL,
	"files",
	files_ref_store_create,
	files_init_db,
	files_transaction_commit,
	files_initial_transaction_commit,

	files_pack_refs,
	files_peel_ref,
	files_create_symref,
	files_delete_refs,
	files_rename_ref,

	files_ref_iterator_begin,
	files_read_raw_ref,
	files_verify_refname_available,

	files_reflog_iterator_begin,
	files_for_each_reflog_ent,
	files_for_each_reflog_ent_reverse,
	files_reflog_exists,
	files_create_reflog,
	files_delete_reflog,
	files_reflog_expire
};
