#include "cache.h"
#include "refs.h"
#include "object.h"
#include "tag.h"
#include "dir.h"
#include "string-list.h"

/*
 * How to handle various characters in refnames:
 * 0: An acceptable character for refs
 * 1: End-of-component
 * 2: ., look for a preceding . to reject .. in refs
 * 3: {, look for a preceding @ to reject @{ in refs
 * 4: A bad character: ASCII control characters, "~", "^", ":" or SP
 */
static unsigned char refname_disposition[256] = {
	1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 2, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 4, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 4, 4
};

/*
 * Try to read one refname component from the front of refname.
 * Return the length of the component found, or -1 if the component is
 * not legal.  It is legal if it is something reasonable to have under
 * ".git/refs/"; We do not like it if:
 *
 * - any path component of it begins with ".", or
 * - it has double dots "..", or
 * - it has ASCII control character, "~", "^", ":" or SP, anywhere, or
 * - it ends with a "/".
 * - it ends with ".lock"
 * - it contains a "\" (backslash)
 */
static int check_refname_component(const char *refname, int flags)
{
	const char *cp;
	char last = '\0';

	for (cp = refname; ; cp++) {
		int ch = *cp & 255;
		unsigned char disp = refname_disposition[ch];
		switch (disp) {
		case 1:
			goto out;
		case 2:
			if (last == '.')
				return -1; /* Refname contains "..". */
			break;
		case 3:
			if (last == '@')
				return -1; /* Refname contains "@{". */
			break;
		case 4:
			return -1;
		}
		last = ch;
	}
out:
	if (cp == refname)
		return 0; /* Component has zero length. */
	if (refname[0] == '.') {
		if (!(flags & REFNAME_DOT_COMPONENT))
			return -1; /* Component starts with '.'. */
		/*
		 * Even if leading dots are allowed, don't allow "."
		 * as a component (".." is prevented by a rule above).
		 */
		if (refname[1] == '\0')
			return -1; /* Component equals ".". */
	}
	if (cp - refname >= 5 && !memcmp(cp - 5, ".lock", 5))
		return -1; /* Refname ends with ".lock". */
	return cp - refname;
}

int check_refname_format(const char *refname, int flags)
{
	int component_len, component_count = 0;

	if (!strcmp(refname, "@"))
		/* Refname is a single character '@'. */
		return -1;

	while (1) {
		/* We are at the start of a path component. */
		component_len = check_refname_component(refname, flags);
		if (component_len <= 0) {
			if ((flags & REFNAME_REFSPEC_PATTERN) &&
					refname[0] == '*' &&
					(refname[1] == '\0' || refname[1] == '/')) {
				/* Accept one wildcard as a full refname component. */
				flags &= ~REFNAME_REFSPEC_PATTERN;
				component_len = 1;
			} else {
				return -1;
			}
		}
		component_count++;
		if (refname[component_len] == '\0')
			break;
		/* Skip to next component. */
		refname += component_len + 1;
	}

	if (refname[component_len - 1] == '.')
		return -1; /* Refname ends with '.'. */
	if (!(flags & REFNAME_ALLOW_ONELEVEL) && component_count < 2)
		return -1; /* Refname has only one component. */
	return 0;
}

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
	unsigned char sha1[20];

	/*
	 * If REF_KNOWS_PEELED, then this field holds the peeled value
	 * of this reference, or null if the reference is known not to
	 * be peelable.  See the documentation for peel_ref() for an
	 * exact definition of "peelable".
	 */
	unsigned char peeled[20];
};

struct ref_cache;

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

	/* A pointer to the ref_cache that contains this ref_dir. */
	struct ref_cache *ref_cache;

	struct ref_entry **entries;
};

/*
 * Bit values for ref_entry::flag.  REF_ISSYMREF=0x01,
 * REF_ISPACKED=0x02, and REF_ISBROKEN=0x04 are public values; see
 * refs.h.
 */

/*
 * The field ref_entry->u.value.peeled of this value entry contains
 * the correct peeled value for the reference, which might be
 * null_sha1 if the reference is not a tag or if it is broken.
 */
#define REF_KNOWS_PEELED 0x08

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
 * separate issue that is regulated by is_refname_available().)
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

static struct ref_dir *get_ref_dir(struct ref_entry *entry)
{
	struct ref_dir *dir;
	assert(entry->flag & REF_DIR);
	dir = &entry->u.subdir;
	if (entry->flag & REF_INCOMPLETE) {
		read_loose_refs(entry->name, dir);
		entry->flag &= ~REF_INCOMPLETE;
	}
	return dir;
}

static struct ref_entry *create_ref_entry(const char *refname,
					  const unsigned char *sha1, int flag,
					  int check_name)
{
	int len;
	struct ref_entry *ref;

	if (check_name &&
	    check_refname_format(refname, REFNAME_ALLOW_ONELEVEL|REFNAME_DOT_COMPONENT))
		die("Reference has invalid format: '%s'", refname);
	len = strlen(refname) + 1;
	ref = xmalloc(sizeof(struct ref_entry) + len);
	hashcpy(ref->u.value.sha1, sha1);
	hashclr(ref->u.value.peeled);
	memcpy(ref->name, refname, len);
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
static struct ref_entry *create_dir_entry(struct ref_cache *ref_cache,
					  const char *dirname, size_t len,
					  int incomplete)
{
	struct ref_entry *direntry;
	direntry = xcalloc(1, sizeof(struct ref_entry) + len + 1);
	memcpy(direntry->name, dirname, len);
	direntry->name[len] = '\0';
	direntry->u.subdir.ref_cache = ref_cache;
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
		entry = create_dir_entry(dir->ref_cache, subdirname, len, 0);
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

	if (hashcmp(ref1->u.value.sha1, ref2->u.value.sha1))
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

	qsort(dir->entries, dir->nr, sizeof(*dir->entries), ref_entry_cmp);

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

/* Include broken references in a do_for_each_ref*() iteration: */
#define DO_FOR_EACH_INCLUDE_BROKEN 0x01

/*
 * Return true iff the reference described by entry can be resolved to
 * an object in the database.  Emit a warning if the referred-to
 * object does not exist.
 */
static int ref_resolves_to_object(struct ref_entry *entry)
{
	if (entry->flag & REF_ISBROKEN)
		return 0;
	if (!has_sha1_file(entry->u.value.sha1)) {
		error("%s does not point to a valid object!", entry->name);
		return 0;
	}
	return 1;
}

/*
 * current_ref is a performance hack: when iterating over references
 * using the for_each_ref*() functions, current_ref is set to the
 * current reference's entry before calling the callback function.  If
 * the callback function calls peel_ref(), then peel_ref() first
 * checks whether the reference to be peeled is the current reference
 * (it usually is) and if so, returns that reference's peeled version
 * if it is available.  This avoids a refname lookup in a common case.
 */
static struct ref_entry *current_ref;

typedef int each_ref_entry_fn(struct ref_entry *entry, void *cb_data);

struct ref_entry_cb {
	const char *base;
	int trim;
	int flags;
	each_ref_fn *fn;
	void *cb_data;
};

/*
 * Handle one reference in a do_for_each_ref*()-style iteration,
 * calling an each_ref_fn for each entry.
 */
static int do_one_ref(struct ref_entry *entry, void *cb_data)
{
	struct ref_entry_cb *data = cb_data;
	struct ref_entry *old_current_ref;
	int retval;

	if (!starts_with(entry->name, data->base))
		return 0;

	if (!(data->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
	      !ref_resolves_to_object(entry))
		return 0;

	/* Store the old value, in case this is a recursive call: */
	old_current_ref = current_ref;
	current_ref = entry;
	retval = data->fn(entry->name + data->trim, entry->u.value.sha1,
			  entry->flag, data->cb_data);
	current_ref = old_current_ref;
	return retval;
}

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
 * Call fn for each reference in the union of dir1 and dir2, in order
 * by refname.  Recurse into subdirectories.  If a value entry appears
 * in both dir1 and dir2, then only process the version that is in
 * dir2.  The input dirs must already be sorted, but subdirs will be
 * sorted as needed.  fn is called for all references, including
 * broken ones.
 */
static int do_for_each_entry_in_dirs(struct ref_dir *dir1,
				     struct ref_dir *dir2,
				     each_ref_entry_fn fn, void *cb_data)
{
	int retval;
	int i1 = 0, i2 = 0;

	assert(dir1->sorted == dir1->nr);
	assert(dir2->sorted == dir2->nr);
	while (1) {
		struct ref_entry *e1, *e2;
		int cmp;
		if (i1 == dir1->nr) {
			return do_for_each_entry_in_dir(dir2, i2, fn, cb_data);
		}
		if (i2 == dir2->nr) {
			return do_for_each_entry_in_dir(dir1, i1, fn, cb_data);
		}
		e1 = dir1->entries[i1];
		e2 = dir2->entries[i2];
		cmp = strcmp(e1->name, e2->name);
		if (cmp == 0) {
			if ((e1->flag & REF_DIR) && (e2->flag & REF_DIR)) {
				/* Both are directories; descend them in parallel. */
				struct ref_dir *subdir1 = get_ref_dir(e1);
				struct ref_dir *subdir2 = get_ref_dir(e2);
				sort_ref_dir(subdir1);
				sort_ref_dir(subdir2);
				retval = do_for_each_entry_in_dirs(
						subdir1, subdir2, fn, cb_data);
				i1++;
				i2++;
			} else if (!(e1->flag & REF_DIR) && !(e2->flag & REF_DIR)) {
				/* Both are references; ignore the one from dir1. */
				retval = fn(e2, cb_data);
				i1++;
				i2++;
			} else {
				die("conflict between reference and directory: %s",
				    e1->name);
			}
		} else {
			struct ref_entry *e;
			if (cmp < 0) {
				e = e1;
				i1++;
			} else {
				e = e2;
				i2++;
			}
			if (e->flag & REF_DIR) {
				struct ref_dir *subdir = get_ref_dir(e);
				sort_ref_dir(subdir);
				retval = do_for_each_entry_in_dir(
						subdir, 0, fn, cb_data);
			} else {
				retval = fn(e, cb_data);
			}
		}
		if (retval)
			return retval;
	}
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
 * Return true iff refname1 and refname2 conflict with each other.
 * Two reference names conflict if one of them exactly matches the
 * leading components of the other; e.g., "foo/bar" conflicts with
 * both "foo" and with "foo/bar/baz" but not with "foo/bar" or
 * "foo/barbados".
 */
static int names_conflict(const char *refname1, const char *refname2)
{
	for (; *refname1 && *refname1 == *refname2; refname1++, refname2++)
		;
	return (*refname1 == '\0' && *refname2 == '/')
		|| (*refname1 == '/' && *refname2 == '\0');
}

struct name_conflict_cb {
	const char *refname;
	const char *oldrefname;
	const char *conflicting_refname;
};

static int name_conflict_fn(struct ref_entry *entry, void *cb_data)
{
	struct name_conflict_cb *data = (struct name_conflict_cb *)cb_data;
	if (data->oldrefname && !strcmp(data->oldrefname, entry->name))
		return 0;
	if (names_conflict(data->refname, entry->name)) {
		data->conflicting_refname = entry->name;
		return 1;
	}
	return 0;
}

/*
 * Return true iff a reference named refname could be created without
 * conflicting with the name of an existing reference in dir.  If
 * oldrefname is non-NULL, ignore potential conflicts with oldrefname
 * (e.g., because oldrefname is scheduled for deletion in the same
 * operation).
 */
static int is_refname_available(const char *refname, const char *oldrefname,
				struct ref_dir *dir)
{
	struct name_conflict_cb data;
	data.refname = refname;
	data.oldrefname = oldrefname;
	data.conflicting_refname = NULL;

	sort_ref_dir(dir);
	if (do_for_each_entry_in_dir(dir, 0, name_conflict_fn, &data)) {
		error("'%s' exists; cannot create '%s'",
		      data.conflicting_refname, refname);
		return 0;
	}
	return 1;
}

struct packed_ref_cache {
	struct ref_entry *root;

	/*
	 * Count of references to the data structure in this instance,
	 * including the pointer from ref_cache::packed if any.  The
	 * data will not be freed as long as the reference count is
	 * nonzero.
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
static struct ref_cache {
	struct ref_cache *next;
	struct ref_entry *loose;
	struct packed_ref_cache *packed;
	/*
	 * The submodule name, or "" for the main repo.  We allocate
	 * length 1 rather than FLEX_ARRAY so that the main ref_cache
	 * is initialized correctly.
	 */
	char name[1];
} ref_cache, *submodule_ref_caches;

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

static void clear_packed_ref_cache(struct ref_cache *refs)
{
	if (refs->packed) {
		struct packed_ref_cache *packed_refs = refs->packed;

		if (packed_refs->lock)
			die("internal error: packed-ref cache cleared while locked");
		refs->packed = NULL;
		release_packed_ref_cache(packed_refs);
	}
}

static void clear_loose_ref_cache(struct ref_cache *refs)
{
	if (refs->loose) {
		free_ref_entry(refs->loose);
		refs->loose = NULL;
	}
}

static struct ref_cache *create_ref_cache(const char *submodule)
{
	int len;
	struct ref_cache *refs;
	if (!submodule)
		submodule = "";
	len = strlen(submodule) + 1;
	refs = xcalloc(1, sizeof(struct ref_cache) + len);
	memcpy(refs->name, submodule, len);
	return refs;
}

/*
 * Return a pointer to a ref_cache for the specified submodule. For
 * the main repository, use submodule==NULL. The returned structure
 * will be allocated and initialized but not necessarily populated; it
 * should not be freed.
 */
static struct ref_cache *get_ref_cache(const char *submodule)
{
	struct ref_cache *refs;

	if (!submodule || !*submodule)
		return &ref_cache;

	for (refs = submodule_ref_caches; refs; refs = refs->next)
		if (!strcmp(submodule, refs->name))
			return refs;

	refs = create_ref_cache(submodule);
	refs->next = submodule_ref_caches;
	submodule_ref_caches = refs;
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
static const char *parse_ref_line(char *line, unsigned char *sha1)
{
	/*
	 * 42: the answer to everything.
	 *
	 * In this case, it happens to be the answer to
	 *  40 (length of sha1 hex representation)
	 *  +1 (space in between hex and name)
	 *  +1 (newline at the end of the line)
	 */
	int len = strlen(line) - 42;

	if (len <= 0)
		return NULL;
	if (get_sha1_hex(line, sha1) < 0)
		return NULL;
	if (!isspace(line[40]))
		return NULL;
	line += 41;
	if (isspace(*line))
		return NULL;
	if (line[len] != '\n')
		return NULL;
	line[len] = 0;

	return line;
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
	char refline[PATH_MAX];
	enum { PEELED_NONE, PEELED_TAGS, PEELED_FULLY } peeled = PEELED_NONE;

	while (fgets(refline, sizeof(refline), f)) {
		unsigned char sha1[20];
		const char *refname;
		static const char header[] = "# pack-refs with:";

		if (!strncmp(refline, header, sizeof(header)-1)) {
			const char *traits = refline + sizeof(header) - 1;
			if (strstr(traits, " fully-peeled "))
				peeled = PEELED_FULLY;
			else if (strstr(traits, " peeled "))
				peeled = PEELED_TAGS;
			/* perhaps other traits later as well */
			continue;
		}

		refname = parse_ref_line(refline, sha1);
		if (refname) {
			last = create_ref_entry(refname, sha1, REF_ISPACKED, 1);
			if (peeled == PEELED_FULLY ||
			    (peeled == PEELED_TAGS && starts_with(refname, "refs/tags/")))
				last->flag |= REF_KNOWS_PEELED;
			add_ref(dir, last);
			continue;
		}
		if (last &&
		    refline[0] == '^' &&
		    strlen(refline) == PEELED_LINE_LENGTH &&
		    refline[PEELED_LINE_LENGTH - 1] == '\n' &&
		    !get_sha1_hex(refline + 1, sha1)) {
			hashcpy(last->u.value.peeled, sha1);
			/*
			 * Regardless of what the file header said,
			 * we definitely know the value of *this*
			 * reference:
			 */
			last->flag |= REF_KNOWS_PEELED;
		}
	}
}

/*
 * Get the packed_ref_cache for the specified ref_cache, creating it
 * if necessary.
 */
static struct packed_ref_cache *get_packed_ref_cache(struct ref_cache *refs)
{
	const char *packed_refs_file;

	if (*refs->name)
		packed_refs_file = git_path_submodule(refs->name, "packed-refs");
	else
		packed_refs_file = git_path("packed-refs");

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
	return refs->packed;
}

static struct ref_dir *get_packed_ref_dir(struct packed_ref_cache *packed_ref_cache)
{
	return get_ref_dir(packed_ref_cache->root);
}

static struct ref_dir *get_packed_refs(struct ref_cache *refs)
{
	return get_packed_ref_dir(get_packed_ref_cache(refs));
}

void add_packed_ref(const char *refname, const unsigned char *sha1)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(&ref_cache);

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
	struct ref_cache *refs = dir->ref_cache;
	DIR *d;
	const char *path;
	struct dirent *de;
	int dirnamelen = strlen(dirname);
	struct strbuf refname;

	if (*refs->name)
		path = git_path_submodule(refs->name, "%s", dirname);
	else
		path = git_path("%s", dirname);

	d = opendir(path);
	if (!d)
		return;

	strbuf_init(&refname, dirnamelen + 257);
	strbuf_add(&refname, dirname, dirnamelen);

	while ((de = readdir(d)) != NULL) {
		unsigned char sha1[20];
		struct stat st;
		int flag;
		const char *refdir;

		if (de->d_name[0] == '.')
			continue;
		if (ends_with(de->d_name, ".lock"))
			continue;
		strbuf_addstr(&refname, de->d_name);
		refdir = *refs->name
			? git_path_submodule(refs->name, "%s", refname.buf)
			: git_path("%s", refname.buf);
		if (stat(refdir, &st) < 0) {
			; /* silently ignore */
		} else if (S_ISDIR(st.st_mode)) {
			strbuf_addch(&refname, '/');
			add_entry_to_dir(dir,
					 create_dir_entry(refs, refname.buf,
							  refname.len, 1));
		} else {
			if (*refs->name) {
				hashclr(sha1);
				flag = 0;
				if (resolve_gitlink_ref(refs->name, refname.buf, sha1) < 0) {
					hashclr(sha1);
					flag |= REF_ISBROKEN;
				}
			} else if (read_ref_full(refname.buf, sha1, 1, &flag)) {
				hashclr(sha1);
				flag |= REF_ISBROKEN;
			}
			add_entry_to_dir(dir,
					 create_ref_entry(refname.buf, sha1, flag, 1));
		}
		strbuf_setlen(&refname, dirnamelen);
	}
	strbuf_release(&refname);
	closedir(d);
}

static struct ref_dir *get_loose_refs(struct ref_cache *refs)
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

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5
#define MAXREFLEN (1024)

/*
 * Called by resolve_gitlink_ref_recursive() after it failed to read
 * from the loose refs in ref_cache refs. Find <refname> in the
 * packed-refs file for the submodule.
 */
static int resolve_gitlink_packed_ref(struct ref_cache *refs,
				      const char *refname, unsigned char *sha1)
{
	struct ref_entry *ref;
	struct ref_dir *dir = get_packed_refs(refs);

	ref = find_ref(dir, refname);
	if (ref == NULL)
		return -1;

	hashcpy(sha1, ref->u.value.sha1);
	return 0;
}

static int resolve_gitlink_ref_recursive(struct ref_cache *refs,
					 const char *refname, unsigned char *sha1,
					 int recursion)
{
	int fd, len;
	char buffer[128], *p;
	char *path;

	if (recursion > MAXDEPTH || strlen(refname) > MAXREFLEN)
		return -1;
	path = *refs->name
		? git_path_submodule(refs->name, "%s", refname)
		: git_path("%s", refname);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return resolve_gitlink_packed_ref(refs, refname, sha1);

	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	if (len < 0)
		return -1;
	while (len && isspace(buffer[len-1]))
		len--;
	buffer[len] = 0;

	/* Was it a detached head or an old-fashioned symlink? */
	if (!get_sha1_hex(buffer, sha1))
		return 0;

	/* Symref? */
	if (strncmp(buffer, "ref:", 4))
		return -1;
	p = buffer + 4;
	while (isspace(*p))
		p++;

	return resolve_gitlink_ref_recursive(refs, p, sha1, recursion+1);
}

int resolve_gitlink_ref(const char *path, const char *refname, unsigned char *sha1)
{
	int len = strlen(path), retval;
	char *submodule;
	struct ref_cache *refs;

	while (len && path[len-1] == '/')
		len--;
	if (!len)
		return -1;
	submodule = xstrndup(path, len);
	refs = get_ref_cache(submodule);
	free(submodule);

	retval = resolve_gitlink_ref_recursive(refs, refname, sha1, 0);
	return retval;
}

/*
 * Return the ref_entry for the given refname from the packed
 * references.  If it does not exist, return NULL.
 */
static struct ref_entry *get_packed_ref(const char *refname)
{
	return find_ref(get_packed_refs(&ref_cache), refname);
}

/*
 * A loose ref file doesn't exist; check for a packed ref.  The
 * options are forwarded from resolve_safe_unsafe().
 */
static const char *handle_missing_loose_ref(const char *refname,
					    unsigned char *sha1,
					    int reading,
					    int *flag)
{
	struct ref_entry *entry;

	/*
	 * The loose reference file does not exist; check for a packed
	 * reference.
	 */
	entry = get_packed_ref(refname);
	if (entry) {
		hashcpy(sha1, entry->u.value.sha1);
		if (flag)
			*flag |= REF_ISPACKED;
		return refname;
	}
	/* The reference is not a packed reference, either. */
	if (reading) {
		return NULL;
	} else {
		hashclr(sha1);
		return refname;
	}
}

/* This function needs to return a meaningful errno on failure */
const char *resolve_ref_unsafe(const char *refname, unsigned char *sha1, int reading, int *flag)
{
	int depth = MAXDEPTH;
	ssize_t len;
	char buffer[256];
	static char refname_buffer[256];

	if (flag)
		*flag = 0;

	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
		errno = EINVAL;
		return NULL;
	}

	for (;;) {
		char path[PATH_MAX];
		struct stat st;
		char *buf;
		int fd;

		if (--depth < 0) {
			errno = ELOOP;
			return NULL;
		}

		git_snpath(path, sizeof(path), "%s", refname);

		/*
		 * We might have to loop back here to avoid a race
		 * condition: first we lstat() the file, then we try
		 * to read it as a link or as a file.  But if somebody
		 * changes the type of the file (file <-> directory
		 * <-> symlink) between the lstat() and reading, then
		 * we don't want to report that as an error but rather
		 * try again starting with the lstat().
		 */
	stat_ref:
		if (lstat(path, &st) < 0) {
			if (errno == ENOENT)
				return handle_missing_loose_ref(refname, sha1,
								reading, flag);
			else
				return NULL;
		}

		/* Follow "normalized" - ie "refs/.." symlinks by hand */
		if (S_ISLNK(st.st_mode)) {
			len = readlink(path, buffer, sizeof(buffer)-1);
			if (len < 0) {
				if (errno == ENOENT || errno == EINVAL)
					/* inconsistent with lstat; retry */
					goto stat_ref;
				else
					return NULL;
			}
			buffer[len] = 0;
			if (starts_with(buffer, "refs/") &&
					!check_refname_format(buffer, 0)) {
				strcpy(refname_buffer, buffer);
				refname = refname_buffer;
				if (flag)
					*flag |= REF_ISSYMREF;
				continue;
			}
		}

		/* Is it a directory? */
		if (S_ISDIR(st.st_mode)) {
			errno = EISDIR;
			return NULL;
		}

		/*
		 * Anything else, just open it and try to use it as
		 * a ref
		 */
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			if (errno == ENOENT)
				/* inconsistent with lstat; retry */
				goto stat_ref;
			else
				return NULL;
		}
		len = read_in_full(fd, buffer, sizeof(buffer)-1);
		if (len < 0) {
			int save_errno = errno;
			close(fd);
			errno = save_errno;
			return NULL;
		}
		close(fd);
		while (len && isspace(buffer[len-1]))
			len--;
		buffer[len] = '\0';

		/*
		 * Is it a symbolic ref?
		 */
		if (!starts_with(buffer, "ref:")) {
			/*
			 * Please note that FETCH_HEAD has a second
			 * line containing other data.
			 */
			if (get_sha1_hex(buffer, sha1) ||
			    (buffer[40] != '\0' && !isspace(buffer[40]))) {
				if (flag)
					*flag |= REF_ISBROKEN;
				errno = EINVAL;
				return NULL;
			}
			return refname;
		}
		if (flag)
			*flag |= REF_ISSYMREF;
		buf = buffer + 4;
		while (isspace(*buf))
			buf++;
		if (check_refname_format(buf, REFNAME_ALLOW_ONELEVEL)) {
			if (flag)
				*flag |= REF_ISBROKEN;
			errno = EINVAL;
			return NULL;
		}
		refname = strcpy(refname_buffer, buf);
	}
}

char *resolve_refdup(const char *ref, unsigned char *sha1, int reading, int *flag)
{
	const char *ret = resolve_ref_unsafe(ref, sha1, reading, flag);
	return ret ? xstrdup(ret) : NULL;
}

/* The argument to filter_refs */
struct ref_filter {
	const char *pattern;
	each_ref_fn *fn;
	void *cb_data;
};

int read_ref_full(const char *refname, unsigned char *sha1, int reading, int *flags)
{
	if (resolve_ref_unsafe(refname, sha1, reading, flags))
		return 0;
	return -1;
}

int read_ref(const char *refname, unsigned char *sha1)
{
	return read_ref_full(refname, sha1, 1, NULL);
}

int ref_exists(const char *refname)
{
	unsigned char sha1[20];
	return !!resolve_ref_unsafe(refname, sha1, 1, NULL);
}

static int filter_refs(const char *refname, const unsigned char *sha1, int flags,
		       void *data)
{
	struct ref_filter *filter = (struct ref_filter *)data;
	if (wildmatch(filter->pattern, refname, 0, NULL))
		return 0;
	return filter->fn(refname, sha1, flags, filter->cb_data);
}

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
static enum peel_status peel_object(const unsigned char *name, unsigned char *sha1)
{
	struct object *o = lookup_unknown_object(name);

	if (o->type == OBJ_NONE) {
		int type = sha1_object_info(name, NULL);
		if (type < 0 || !object_as_type(o, type, 0))
			return PEEL_INVALID;
	}

	if (o->type != OBJ_TAG)
		return PEEL_NON_TAG;

	o = deref_tag_noverify(o);
	if (!o)
		return PEEL_INVALID;

	hashcpy(sha1, o->sha1);
	return PEEL_PEELED;
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
			hashclr(entry->u.value.peeled);
		} else {
			return is_null_sha1(entry->u.value.peeled) ?
				PEEL_NON_TAG : PEEL_PEELED;
		}
	}
	if (entry->flag & REF_ISBROKEN)
		return PEEL_BROKEN;
	if (entry->flag & REF_ISSYMREF)
		return PEEL_IS_SYMREF;

	status = peel_object(entry->u.value.sha1, entry->u.value.peeled);
	if (status == PEEL_PEELED || status == PEEL_NON_TAG)
		entry->flag |= REF_KNOWS_PEELED;
	return status;
}

int peel_ref(const char *refname, unsigned char *sha1)
{
	int flag;
	unsigned char base[20];

	if (current_ref && (current_ref->name == refname
			    || !strcmp(current_ref->name, refname))) {
		if (peel_entry(current_ref, 0))
			return -1;
		hashcpy(sha1, current_ref->u.value.peeled);
		return 0;
	}

	if (read_ref_full(refname, base, 1, &flag))
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
		struct ref_entry *r = get_packed_ref(refname);
		if (r) {
			if (peel_entry(r, 0))
				return -1;
			hashcpy(sha1, r->u.value.peeled);
			return 0;
		}
	}

	return peel_object(base, sha1);
}

struct warn_if_dangling_data {
	FILE *fp;
	const char *refname;
	const struct string_list *refnames;
	const char *msg_fmt;
};

static int warn_if_dangling_symref(const char *refname, const unsigned char *sha1,
				   int flags, void *cb_data)
{
	struct warn_if_dangling_data *d = cb_data;
	const char *resolves_to;
	unsigned char junk[20];

	if (!(flags & REF_ISSYMREF))
		return 0;

	resolves_to = resolve_ref_unsafe(refname, junk, 0, NULL);
	if (!resolves_to
	    || (d->refname
		? strcmp(resolves_to, d->refname)
		: !string_list_has_string(d->refnames, resolves_to))) {
		return 0;
	}

	fprintf(d->fp, d->msg_fmt, refname);
	fputc('\n', d->fp);
	return 0;
}

void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname)
{
	struct warn_if_dangling_data data;

	data.fp = fp;
	data.refname = refname;
	data.refnames = NULL;
	data.msg_fmt = msg_fmt;
	for_each_rawref(warn_if_dangling_symref, &data);
}

void warn_dangling_symrefs(FILE *fp, const char *msg_fmt, const struct string_list *refnames)
{
	struct warn_if_dangling_data data;

	data.fp = fp;
	data.refname = NULL;
	data.refnames = refnames;
	data.msg_fmt = msg_fmt;
	for_each_rawref(warn_if_dangling_symref, &data);
}

/*
 * Call fn for each reference in the specified ref_cache, omitting
 * references not in the containing_dir of base.  fn is called for all
 * references, including broken ones.  If fn ever returns a non-zero
 * value, stop the iteration and return that value; otherwise, return
 * 0.
 */
static int do_for_each_entry(struct ref_cache *refs, const char *base,
			     each_ref_entry_fn fn, void *cb_data)
{
	struct packed_ref_cache *packed_ref_cache;
	struct ref_dir *loose_dir;
	struct ref_dir *packed_dir;
	int retval = 0;

	/*
	 * We must make sure that all loose refs are read before accessing the
	 * packed-refs file; this avoids a race condition in which loose refs
	 * are migrated to the packed-refs file by a simultaneous process, but
	 * our in-memory view is from before the migration. get_packed_ref_cache()
	 * takes care of making sure our view is up to date with what is on
	 * disk.
	 */
	loose_dir = get_loose_refs(refs);
	if (base && *base) {
		loose_dir = find_containing_dir(loose_dir, base, 0);
	}
	if (loose_dir)
		prime_ref_dir(loose_dir);

	packed_ref_cache = get_packed_ref_cache(refs);
	acquire_packed_ref_cache(packed_ref_cache);
	packed_dir = get_packed_ref_dir(packed_ref_cache);
	if (base && *base) {
		packed_dir = find_containing_dir(packed_dir, base, 0);
	}

	if (packed_dir && loose_dir) {
		sort_ref_dir(packed_dir);
		sort_ref_dir(loose_dir);
		retval = do_for_each_entry_in_dirs(
				packed_dir, loose_dir, fn, cb_data);
	} else if (packed_dir) {
		sort_ref_dir(packed_dir);
		retval = do_for_each_entry_in_dir(
				packed_dir, 0, fn, cb_data);
	} else if (loose_dir) {
		sort_ref_dir(loose_dir);
		retval = do_for_each_entry_in_dir(
				loose_dir, 0, fn, cb_data);
	}

	release_packed_ref_cache(packed_ref_cache);
	return retval;
}

/*
 * Call fn for each reference in the specified ref_cache for which the
 * refname begins with base.  If trim is non-zero, then trim that many
 * characters off the beginning of each refname before passing the
 * refname to fn.  flags can be DO_FOR_EACH_INCLUDE_BROKEN to include
 * broken references in the iteration.  If fn ever returns a non-zero
 * value, stop the iteration and return that value; otherwise, return
 * 0.
 */
static int do_for_each_ref(struct ref_cache *refs, const char *base,
			   each_ref_fn fn, int trim, int flags, void *cb_data)
{
	struct ref_entry_cb data;
	data.base = base;
	data.trim = trim;
	data.flags = flags;
	data.fn = fn;
	data.cb_data = cb_data;

	return do_for_each_entry(refs, base, do_one_ref, &data);
}

static int do_head_ref(const char *submodule, each_ref_fn fn, void *cb_data)
{
	unsigned char sha1[20];
	int flag;

	if (submodule) {
		if (resolve_gitlink_ref(submodule, "HEAD", sha1) == 0)
			return fn("HEAD", sha1, 0, cb_data);

		return 0;
	}

	if (!read_ref_full("HEAD", sha1, 1, &flag))
		return fn("HEAD", sha1, flag, cb_data);

	return 0;
}

int head_ref(each_ref_fn fn, void *cb_data)
{
	return do_head_ref(NULL, fn, cb_data);
}

int head_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return do_head_ref(submodule, fn, cb_data);
}

int for_each_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(&ref_cache, "", fn, 0, 0, cb_data);
}

int for_each_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(get_ref_cache(submodule), "", fn, 0, 0, cb_data);
}

int for_each_ref_in(const char *prefix, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(&ref_cache, prefix, fn, strlen(prefix), 0, cb_data);
}

int for_each_ref_in_submodule(const char *submodule, const char *prefix,
		each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(get_ref_cache(submodule), prefix, fn, strlen(prefix), 0, cb_data);
}

int for_each_tag_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/tags/", fn, cb_data);
}

int for_each_tag_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/tags/", fn, cb_data);
}

int for_each_branch_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/heads/", fn, cb_data);
}

int for_each_branch_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/heads/", fn, cb_data);
}

int for_each_remote_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/remotes/", fn, cb_data);
}

int for_each_remote_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/remotes/", fn, cb_data);
}

int for_each_replace_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(&ref_cache, "refs/replace/", fn, 13, 0, cb_data);
}

int head_ref_namespaced(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;
	unsigned char sha1[20];
	int flag;

	strbuf_addf(&buf, "%sHEAD", get_git_namespace());
	if (!read_ref_full(buf.buf, sha1, 1, &flag))
		ret = fn(buf.buf, sha1, flag, cb_data);
	strbuf_release(&buf);

	return ret;
}

int for_each_namespaced_ref(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret;
	strbuf_addf(&buf, "%srefs/", get_git_namespace());
	ret = do_for_each_ref(&ref_cache, buf.buf, fn, 0, 0, cb_data);
	strbuf_release(&buf);
	return ret;
}

int for_each_glob_ref_in(each_ref_fn fn, const char *pattern,
	const char *prefix, void *cb_data)
{
	struct strbuf real_pattern = STRBUF_INIT;
	struct ref_filter filter;
	int ret;

	if (!prefix && !starts_with(pattern, "refs/"))
		strbuf_addstr(&real_pattern, "refs/");
	else if (prefix)
		strbuf_addstr(&real_pattern, prefix);
	strbuf_addstr(&real_pattern, pattern);

	if (!has_glob_specials(pattern)) {
		/* Append implied '/' '*' if not present. */
		if (real_pattern.buf[real_pattern.len - 1] != '/')
			strbuf_addch(&real_pattern, '/');
		/* No need to check for '*', there is none. */
		strbuf_addch(&real_pattern, '*');
	}

	filter.pattern = real_pattern.buf;
	filter.fn = fn;
	filter.cb_data = cb_data;
	ret = for_each_ref(filter_refs, &filter);

	strbuf_release(&real_pattern);
	return ret;
}

int for_each_glob_ref(each_ref_fn fn, const char *pattern, void *cb_data)
{
	return for_each_glob_ref_in(fn, pattern, NULL, cb_data);
}

int for_each_rawref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(&ref_cache, "", fn, 0,
			       DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

const char *prettify_refname(const char *name)
{
	return name + (
		starts_with(name, "refs/heads/") ? 11 :
		starts_with(name, "refs/tags/") ? 10 :
		starts_with(name, "refs/remotes/") ? 13 :
		0);
}

static const char *ref_rev_parse_rules[] = {
	"%.*s",
	"refs/%.*s",
	"refs/tags/%.*s",
	"refs/heads/%.*s",
	"refs/remotes/%.*s",
	"refs/remotes/%.*s/HEAD",
	NULL
};

int refname_match(const char *abbrev_name, const char *full_name)
{
	const char **p;
	const int abbrev_name_len = strlen(abbrev_name);

	for (p = ref_rev_parse_rules; *p; p++) {
		if (!strcmp(full_name, mkpath(*p, abbrev_name_len, abbrev_name))) {
			return 1;
		}
	}

	return 0;
}

/* This function should make sure errno is meaningful on error */
static struct ref_lock *verify_lock(struct ref_lock *lock,
	const unsigned char *old_sha1, int mustexist)
{
	if (read_ref_full(lock->ref_name, lock->old_sha1, mustexist, NULL)) {
		int save_errno = errno;
		error("Can't verify ref %s", lock->ref_name);
		unlock_ref(lock);
		errno = save_errno;
		return NULL;
	}
	if (hashcmp(lock->old_sha1, old_sha1)) {
		error("Ref %s is at %s but expected %s", lock->ref_name,
			sha1_to_hex(lock->old_sha1), sha1_to_hex(old_sha1));
		unlock_ref(lock);
		errno = EBUSY;
		return NULL;
	}
	return lock;
}

static int remove_empty_directories(const char *file)
{
	/* we want to create a file but there is a directory there;
	 * if that is an empty directory (or a directory that contains
	 * only empty directories), remove them.
	 */
	struct strbuf path;
	int result, save_errno;

	strbuf_init(&path, 20);
	strbuf_addstr(&path, file);

	result = remove_dir_recursively(&path, REMOVE_DIR_EMPTY_ONLY);
	save_errno = errno;

	strbuf_release(&path);
	errno = save_errno;

	return result;
}

/*
 * *string and *len will only be substituted, and *string returned (for
 * later free()ing) if the string passed in is a magic short-hand form
 * to name a branch.
 */
static char *substitute_branch_name(const char **string, int *len)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = interpret_branch_name(*string, *len, &buf);

	if (ret == *len) {
		size_t size;
		*string = strbuf_detach(&buf, &size);
		*len = size;
		return (char *)*string;
	}

	return NULL;
}

int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p, *r;
	int refs_found = 0;

	*ref = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		char fullref[PATH_MAX];
		unsigned char sha1_from_ref[20];
		unsigned char *this_result;
		int flag;

		this_result = refs_found ? sha1_from_ref : sha1;
		mksnpath(fullref, sizeof(fullref), *p, len, str);
		r = resolve_ref_unsafe(fullref, this_result, 1, &flag);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!warn_ambiguous_refs)
				break;
		} else if ((flag & REF_ISSYMREF) && strcmp(fullref, "HEAD")) {
			warning("ignoring dangling symref %s.", fullref);
		} else if ((flag & REF_ISBROKEN) && strchr(fullref, '/')) {
			warning("ignoring broken ref %s.", fullref);
		}
	}
	free(last_branch);
	return refs_found;
}

int dwim_log(const char *str, int len, unsigned char *sha1, char **log)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p;
	int logs_found = 0;

	*log = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		unsigned char hash[20];
		char path[PATH_MAX];
		const char *ref, *it;

		mksnpath(path, sizeof(path), *p, len, str);
		ref = resolve_ref_unsafe(path, hash, 1, NULL);
		if (!ref)
			continue;
		if (reflog_exists(path))
			it = path;
		else if (strcmp(ref, path) && reflog_exists(ref))
			it = ref;
		else
			continue;
		if (!logs_found++) {
			*log = xstrdup(it);
			hashcpy(sha1, hash);
		}
		if (!warn_ambiguous_refs)
			break;
	}
	free(last_branch);
	return logs_found;
}

/* This function should make sure errno is meaningful on error */
static struct ref_lock *lock_ref_sha1_basic(const char *refname,
					    const unsigned char *old_sha1,
					    int flags, int *type_p)
{
	char *ref_file;
	const char *orig_refname = refname;
	struct ref_lock *lock;
	int last_errno = 0;
	int type, lflags;
	int mustexist = (old_sha1 && !is_null_sha1(old_sha1));
	int missing = 0;
	int attempts_remaining = 3;

	lock = xcalloc(1, sizeof(struct ref_lock));
	lock->lock_fd = -1;

	refname = resolve_ref_unsafe(refname, lock->old_sha1, mustexist, &type);
	if (!refname && errno == EISDIR) {
		/* we are trying to lock foo but we used to
		 * have foo/bar which now does not exist;
		 * it is normal for the empty directory 'foo'
		 * to remain.
		 */
		ref_file = git_path("%s", orig_refname);
		if (remove_empty_directories(ref_file)) {
			last_errno = errno;
			error("there are still refs under '%s'", orig_refname);
			goto error_return;
		}
		refname = resolve_ref_unsafe(orig_refname, lock->old_sha1, mustexist, &type);
	}
	if (type_p)
	    *type_p = type;
	if (!refname) {
		last_errno = errno;
		error("unable to resolve reference %s: %s",
			orig_refname, strerror(errno));
		goto error_return;
	}
	missing = is_null_sha1(lock->old_sha1);
	/* When the ref did not exist and we are creating it,
	 * make sure there is no existing ref that is packed
	 * whose name begins with our refname, nor a ref whose
	 * name is a proper prefix of our refname.
	 */
	if (missing &&
	     !is_refname_available(refname, NULL, get_packed_refs(&ref_cache))) {
		last_errno = ENOTDIR;
		goto error_return;
	}

	lock->lk = xcalloc(1, sizeof(struct lock_file));

	lflags = 0;
	if (flags & REF_NODEREF) {
		refname = orig_refname;
		lflags |= LOCK_NODEREF;
	}
	lock->ref_name = xstrdup(refname);
	lock->orig_ref_name = xstrdup(orig_refname);
	ref_file = git_path("%s", refname);
	if (missing)
		lock->force_write = 1;
	if ((flags & REF_NODEREF) && (type & REF_ISSYMREF))
		lock->force_write = 1;

 retry:
	switch (safe_create_leading_directories(ref_file)) {
	case SCLD_OK:
		break; /* success */
	case SCLD_VANISHED:
		if (--attempts_remaining > 0)
			goto retry;
		/* fall through */
	default:
		last_errno = errno;
		error("unable to create directory for %s", ref_file);
		goto error_return;
	}

	lock->lock_fd = hold_lock_file_for_update(lock->lk, ref_file, lflags);
	if (lock->lock_fd < 0) {
		if (errno == ENOENT && --attempts_remaining > 0)
			/*
			 * Maybe somebody just deleted one of the
			 * directories leading to ref_file.  Try
			 * again:
			 */
			goto retry;
		else
			unable_to_lock_index_die(ref_file, errno);
	}
	return old_sha1 ? verify_lock(lock, old_sha1, mustexist) : lock;

 error_return:
	unlock_ref(lock);
	errno = last_errno;
	return NULL;
}

struct ref_lock *lock_ref_sha1(const char *refname, const unsigned char *old_sha1)
{
	char refpath[PATH_MAX];
	if (check_refname_format(refname, 0))
		return NULL;
	strcpy(refpath, mkpath("refs/%s", refname));
	return lock_ref_sha1_basic(refpath, old_sha1, 0, NULL);
}

struct ref_lock *lock_any_ref_for_update(const char *refname,
					 const unsigned char *old_sha1,
					 int flags, int *type_p)
{
	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		return NULL;
	return lock_ref_sha1_basic(refname, old_sha1, flags, type_p);
}

/*
 * Write an entry to the packed-refs file for the specified refname.
 * If peeled is non-NULL, write it as the entry's peeled value.
 */
static void write_packed_entry(int fd, char *refname, unsigned char *sha1,
			       unsigned char *peeled)
{
	char line[PATH_MAX + 100];
	int len;

	len = snprintf(line, sizeof(line), "%s %s\n",
		       sha1_to_hex(sha1), refname);
	/* this should not happen but just being defensive */
	if (len > sizeof(line))
		die("too long a refname '%s'", refname);
	write_or_die(fd, line, len);

	if (peeled) {
		if (snprintf(line, sizeof(line), "^%s\n",
			     sha1_to_hex(peeled)) != PEELED_LINE_LENGTH)
			die("internal error");
		write_or_die(fd, line, PEELED_LINE_LENGTH);
	}
}

/*
 * An each_ref_entry_fn that writes the entry to a packed-refs file.
 */
static int write_packed_entry_fn(struct ref_entry *entry, void *cb_data)
{
	int *fd = cb_data;
	enum peel_status peel_status = peel_entry(entry, 0);

	if (peel_status != PEEL_PEELED && peel_status != PEEL_NON_TAG)
		error("internal error: %s is not a valid packed reference!",
		      entry->name);
	write_packed_entry(*fd, entry->name, entry->u.value.sha1,
			   peel_status == PEEL_PEELED ?
			   entry->u.value.peeled : NULL);
	return 0;
}

/* This should return a meaningful errno on failure */
int lock_packed_refs(int flags)
{
	struct packed_ref_cache *packed_ref_cache;

	if (hold_lock_file_for_update(&packlock, git_path("packed-refs"), flags) < 0)
		return -1;
	/*
	 * Get the current packed-refs while holding the lock.  If the
	 * packed-refs file has been modified since we last read it,
	 * this will automatically invalidate the cache and re-read
	 * the packed-refs file.
	 */
	packed_ref_cache = get_packed_ref_cache(&ref_cache);
	packed_ref_cache->lock = &packlock;
	/* Increment the reference count to prevent it from being freed: */
	acquire_packed_ref_cache(packed_ref_cache);
	return 0;
}

/*
 * Commit the packed refs changes.
 * On error we must make sure that errno contains a meaningful value.
 */
int commit_packed_refs(void)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(&ref_cache);
	int error = 0;
	int save_errno = 0;

	if (!packed_ref_cache->lock)
		die("internal error: packed-refs not locked");
	write_or_die(packed_ref_cache->lock->fd,
		     PACKED_REFS_HEADER, strlen(PACKED_REFS_HEADER));

	do_for_each_entry_in_dir(get_packed_ref_dir(packed_ref_cache),
				 0, write_packed_entry_fn,
				 &packed_ref_cache->lock->fd);
	if (commit_lock_file(packed_ref_cache->lock)) {
		save_errno = errno;
		error = -1;
	}
	packed_ref_cache->lock = NULL;
	release_packed_ref_cache(packed_ref_cache);
	errno = save_errno;
	return error;
}

void rollback_packed_refs(void)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(&ref_cache);

	if (!packed_ref_cache->lock)
		die("internal error: packed-refs not locked");
	rollback_lock_file(packed_ref_cache->lock);
	packed_ref_cache->lock = NULL;
	release_packed_ref_cache(packed_ref_cache);
	clear_packed_ref_cache(&ref_cache);
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

	/* ALWAYS pack tags */
	if (!(cb->flags & PACK_REFS_ALL) && !is_tag_ref)
		return 0;

	/* Do not pack symbolic or broken refs: */
	if ((entry->flag & REF_ISSYMREF) || !ref_resolves_to_object(entry))
		return 0;

	/* Add a packed ref cache entry equivalent to the loose entry. */
	peel_status = peel_entry(entry, 1);
	if (peel_status != PEEL_PEELED && peel_status != PEEL_NON_TAG)
		die("internal error peeling reference %s (%s)",
		    entry->name, sha1_to_hex(entry->u.value.sha1));
	packed_entry = find_ref(cb->packed_refs, entry->name);
	if (packed_entry) {
		/* Overwrite existing packed entry with info from loose entry */
		packed_entry->flag = REF_ISPACKED | REF_KNOWS_PEELED;
		hashcpy(packed_entry->u.value.sha1, entry->u.value.sha1);
	} else {
		packed_entry = create_ref_entry(entry->name, entry->u.value.sha1,
						REF_ISPACKED | REF_KNOWS_PEELED, 0);
		add_ref(cb->packed_refs, packed_entry);
	}
	hashcpy(packed_entry->u.value.peeled, entry->u.value.peeled);

	/* Schedule the loose reference for pruning if requested. */
	if ((cb->flags & PACK_REFS_PRUNE)) {
		int namelen = strlen(entry->name) + 1;
		struct ref_to_prune *n = xcalloc(1, sizeof(*n) + namelen);
		hashcpy(n->sha1, entry->u.value.sha1);
		strcpy(n->name, entry->name);
		n->next = cb->ref_to_prune;
		cb->ref_to_prune = n;
	}
	return 0;
}

/*
 * Remove empty parents, but spare refs/ and immediate subdirs.
 * Note: munges *name.
 */
static void try_remove_empty_parents(char *name)
{
	char *p, *q;
	int i;
	p = name;
	for (i = 0; i < 2; i++) { /* refs/{heads,tags,...}/ */
		while (*p && *p != '/')
			p++;
		/* tolerate duplicate slashes; see check_refname_format() */
		while (*p == '/')
			p++;
	}
	for (q = p; *q; q++)
		;
	while (1) {
		while (q > p && *q != '/')
			q--;
		while (q > p && *(q-1) == '/')
			q--;
		if (q == p)
			break;
		*q = '\0';
		if (rmdir(git_path("%s", name)))
			break;
	}
}

/* make sure nobody touched the ref, and unlink */
static void prune_ref(struct ref_to_prune *r)
{
	struct ref_lock *lock = lock_any_ref_for_update(r->name, r->sha1,
							0, NULL);

	if (lock) {
		unlink_or_warn(git_path("%s", r->name));
		unlock_ref(lock);
		try_remove_empty_parents(r->name);
	}
}

static void prune_refs(struct ref_to_prune *r)
{
	while (r) {
		prune_ref(r);
		r = r->next;
	}
}

int pack_refs(unsigned int flags)
{
	struct pack_refs_cb_data cbdata;

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.flags = flags;

	lock_packed_refs(LOCK_DIE_ON_ERROR);
	cbdata.packed_refs = get_packed_refs(&ref_cache);

	do_for_each_entry_in_dir(get_loose_refs(&ref_cache), 0,
				 pack_if_possible_fn, &cbdata);

	if (commit_packed_refs())
		die_errno("unable to overwrite old ref-pack file");

	prune_refs(cbdata.ref_to_prune);
	return 0;
}

/*
 * If entry is no longer needed in packed-refs, add it to the string
 * list pointed to by cb_data.  Reasons for deleting entries:
 *
 * - Entry is broken.
 * - Entry is overridden by a loose ref.
 * - Entry does not point at a valid object.
 *
 * In the first and third cases, also emit an error message because these
 * are indications of repository corruption.
 */
static int curate_packed_ref_fn(struct ref_entry *entry, void *cb_data)
{
	struct string_list *refs_to_delete = cb_data;

	if (entry->flag & REF_ISBROKEN) {
		/* This shouldn't happen to packed refs. */
		error("%s is broken!", entry->name);
		string_list_append(refs_to_delete, entry->name);
		return 0;
	}
	if (!has_sha1_file(entry->u.value.sha1)) {
		unsigned char sha1[20];
		int flags;

		if (read_ref_full(entry->name, sha1, 0, &flags))
			/* We should at least have found the packed ref. */
			die("Internal error");
		if ((flags & REF_ISSYMREF) || !(flags & REF_ISPACKED)) {
			/*
			 * This packed reference is overridden by a
			 * loose reference, so it is OK that its value
			 * is no longer valid; for example, it might
			 * refer to an object that has been garbage
			 * collected.  For this purpose we don't even
			 * care whether the loose reference itself is
			 * invalid, broken, symbolic, etc.  Silently
			 * remove the packed reference.
			 */
			string_list_append(refs_to_delete, entry->name);
			return 0;
		}
		/*
		 * There is no overriding loose reference, so the fact
		 * that this reference doesn't refer to a valid object
		 * indicates some kind of repository corruption.
		 * Report the problem, then omit the reference from
		 * the output.
		 */
		error("%s does not point to a valid object!", entry->name);
		string_list_append(refs_to_delete, entry->name);
		return 0;
	}

	return 0;
}

int repack_without_refs(const char **refnames, int n, struct strbuf *err)
{
	struct ref_dir *packed;
	struct string_list refs_to_delete = STRING_LIST_INIT_DUP;
	struct string_list_item *ref_to_delete;
	int i, ret, removed = 0;

	/* Look for a packed ref */
	for (i = 0; i < n; i++)
		if (get_packed_ref(refnames[i]))
			break;

	/* Avoid locking if we have nothing to do */
	if (i == n)
		return 0; /* no refname exists in packed refs */

	if (lock_packed_refs(0)) {
		if (err) {
			unable_to_lock_message(git_path("packed-refs"), errno,
					       err);
			return -1;
		}
		unable_to_lock_error(git_path("packed-refs"), errno);
		return error("cannot delete '%s' from packed refs", refnames[i]);
	}
	packed = get_packed_refs(&ref_cache);

	/* Remove refnames from the cache */
	for (i = 0; i < n; i++)
		if (remove_entry(packed, refnames[i]) != -1)
			removed = 1;
	if (!removed) {
		/*
		 * All packed entries disappeared while we were
		 * acquiring the lock.
		 */
		rollback_packed_refs();
		return 0;
	}

	/* Remove any other accumulated cruft */
	do_for_each_entry_in_dir(packed, 0, curate_packed_ref_fn, &refs_to_delete);
	for_each_string_list_item(ref_to_delete, &refs_to_delete) {
		if (remove_entry(packed, ref_to_delete->string) == -1)
			die("internal error");
	}

	/* Write what remains */
	ret = commit_packed_refs();
	if (ret && err)
		strbuf_addf(err, "unable to overwrite old ref-pack file: %s",
			    strerror(errno));
	return ret;
}

static int repack_without_ref(const char *refname)
{
	return repack_without_refs(&refname, 1, NULL);
}

static int delete_ref_loose(struct ref_lock *lock, int flag)
{
	if (!(flag & REF_ISPACKED) || flag & REF_ISSYMREF) {
		/* loose */
		int err, i = strlen(lock->lk->filename) - 5; /* .lock */

		lock->lk->filename[i] = 0;
		err = unlink_or_warn(lock->lk->filename);
		lock->lk->filename[i] = '.';
		if (err && errno != ENOENT)
			return 1;
	}
	return 0;
}

int delete_ref(const char *refname, const unsigned char *sha1, int delopt)
{
	struct ref_lock *lock;
	int ret = 0, flag = 0;

	lock = lock_ref_sha1_basic(refname, sha1, delopt, &flag);
	if (!lock)
		return 1;
	ret |= delete_ref_loose(lock, flag);

	/* removing the loose one could have resurrected an earlier
	 * packed one.  Also, if it was not loose we need to repack
	 * without it.
	 */
	ret |= repack_without_ref(lock->ref_name);

	unlink_or_warn(git_path("logs/%s", lock->ref_name));
	clear_loose_ref_cache(&ref_cache);
	unlock_ref(lock);
	return ret;
}

/*
 * People using contrib's git-new-workdir have .git/logs/refs ->
 * /some/other/path/.git/logs/refs, and that may live on another device.
 *
 * IOW, to avoid cross device rename errors, the temporary renamed log must
 * live into logs/refs.
 */
#define TMP_RENAMED_LOG  "logs/refs/.tmp-renamed-log"

static int rename_tmp_log(const char *newrefname)
{
	int attempts_remaining = 4;

 retry:
	switch (safe_create_leading_directories(git_path("logs/%s", newrefname))) {
	case SCLD_OK:
		break; /* success */
	case SCLD_VANISHED:
		if (--attempts_remaining > 0)
			goto retry;
		/* fall through */
	default:
		error("unable to create directory for %s", newrefname);
		return -1;
	}

	if (rename(git_path(TMP_RENAMED_LOG), git_path("logs/%s", newrefname))) {
		if ((errno==EISDIR || errno==ENOTDIR) && --attempts_remaining > 0) {
			/*
			 * rename(a, b) when b is an existing
			 * directory ought to result in ISDIR, but
			 * Solaris 5.8 gives ENOTDIR.  Sheesh.
			 */
			if (remove_empty_directories(git_path("logs/%s", newrefname))) {
				error("Directory not empty: logs/%s", newrefname);
				return -1;
			}
			goto retry;
		} else if (errno == ENOENT && --attempts_remaining > 0) {
			/*
			 * Maybe another process just deleted one of
			 * the directories in the path to newrefname.
			 * Try again from the beginning.
			 */
			goto retry;
		} else {
			error("unable to move logfile "TMP_RENAMED_LOG" to logs/%s: %s",
				newrefname, strerror(errno));
			return -1;
		}
	}
	return 0;
}

int rename_ref(const char *oldrefname, const char *newrefname, const char *logmsg)
{
	unsigned char sha1[20], orig_sha1[20];
	int flag = 0, logmoved = 0;
	struct ref_lock *lock;
	struct stat loginfo;
	int log = !lstat(git_path("logs/%s", oldrefname), &loginfo);
	const char *symref = NULL;

	if (log && S_ISLNK(loginfo.st_mode))
		return error("reflog for %s is a symlink", oldrefname);

	symref = resolve_ref_unsafe(oldrefname, orig_sha1, 1, &flag);
	if (flag & REF_ISSYMREF)
		return error("refname %s is a symbolic ref, renaming it is not supported",
			oldrefname);
	if (!symref)
		return error("refname %s not found", oldrefname);

	if (!is_refname_available(newrefname, oldrefname, get_packed_refs(&ref_cache)))
		return 1;

	if (!is_refname_available(newrefname, oldrefname, get_loose_refs(&ref_cache)))
		return 1;

	if (log && rename(git_path("logs/%s", oldrefname), git_path(TMP_RENAMED_LOG)))
		return error("unable to move logfile logs/%s to "TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));

	if (delete_ref(oldrefname, orig_sha1, REF_NODEREF)) {
		error("unable to delete old %s", oldrefname);
		goto rollback;
	}

	if (!read_ref_full(newrefname, sha1, 1, &flag) &&
	    delete_ref(newrefname, sha1, REF_NODEREF)) {
		if (errno==EISDIR) {
			if (remove_empty_directories(git_path("%s", newrefname))) {
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

	lock = lock_ref_sha1_basic(newrefname, NULL, 0, NULL);
	if (!lock) {
		error("unable to lock %s for update", newrefname);
		goto rollback;
	}
	lock->force_write = 1;
	hashcpy(lock->old_sha1, orig_sha1);
	if (write_ref_sha1(lock, orig_sha1, logmsg)) {
		error("unable to write current sha1 into %s", newrefname);
		goto rollback;
	}

	return 0;

 rollback:
	lock = lock_ref_sha1_basic(oldrefname, NULL, 0, NULL);
	if (!lock) {
		error("unable to lock %s for rollback", oldrefname);
		goto rollbacklog;
	}

	lock->force_write = 1;
	flag = log_all_ref_updates;
	log_all_ref_updates = 0;
	if (write_ref_sha1(lock, orig_sha1, NULL))
		error("unable to write current sha1 into %s", oldrefname);
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

int close_ref(struct ref_lock *lock)
{
	if (close_lock_file(lock->lk))
		return -1;
	lock->lock_fd = -1;
	return 0;
}

int commit_ref(struct ref_lock *lock)
{
	if (commit_lock_file(lock->lk))
		return -1;
	lock->lock_fd = -1;
	return 0;
}

void unlock_ref(struct ref_lock *lock)
{
	/* Do not free lock->lk -- atexit() still looks at them */
	if (lock->lk)
		rollback_lock_file(lock->lk);
	free(lock->ref_name);
	free(lock->orig_ref_name);
	free(lock);
}

/*
 * copy the reflog message msg to buf, which has been allocated sufficiently
 * large, while cleaning up the whitespaces.  Especially, convert LF to space,
 * because reflog file is one line per entry.
 */
static int copy_msg(char *buf, const char *msg)
{
	char *cp = buf;
	char c;
	int wasspace = 1;

	*cp++ = '\t';
	while ((c = *msg++)) {
		if (wasspace && isspace(c))
			continue;
		wasspace = isspace(c);
		if (wasspace)
			c = ' ';
		*cp++ = c;
	}
	while (buf < cp && isspace(cp[-1]))
		cp--;
	*cp++ = '\n';
	return cp - buf;
}

/* This function must set a meaningful errno on failure */
int log_ref_setup(const char *refname, char *logfile, int bufsize)
{
	int logfd, oflags = O_APPEND | O_WRONLY;

	git_snpath(logfile, bufsize, "logs/%s", refname);
	if (log_all_ref_updates &&
	    (starts_with(refname, "refs/heads/") ||
	     starts_with(refname, "refs/remotes/") ||
	     starts_with(refname, "refs/notes/") ||
	     !strcmp(refname, "HEAD"))) {
		if (safe_create_leading_directories(logfile) < 0) {
			int save_errno = errno;
			error("unable to create directory for %s", logfile);
			errno = save_errno;
			return -1;
		}
		oflags |= O_CREAT;
	}

	logfd = open(logfile, oflags, 0666);
	if (logfd < 0) {
		if (!(oflags & O_CREAT) && errno == ENOENT)
			return 0;

		if ((oflags & O_CREAT) && errno == EISDIR) {
			if (remove_empty_directories(logfile)) {
				int save_errno = errno;
				error("There are still logs under '%s'",
				      logfile);
				errno = save_errno;
				return -1;
			}
			logfd = open(logfile, oflags, 0666);
		}

		if (logfd < 0) {
			int save_errno = errno;
			error("Unable to append to %s: %s", logfile,
			      strerror(errno));
			errno = save_errno;
			return -1;
		}
	}

	adjust_shared_perm(logfile);
	close(logfd);
	return 0;
}

static int log_ref_write(const char *refname, const unsigned char *old_sha1,
			 const unsigned char *new_sha1, const char *msg)
{
	int logfd, result, written, oflags = O_APPEND | O_WRONLY;
	unsigned maxlen, len;
	int msglen;
	char log_file[PATH_MAX];
	char *logrec;
	const char *committer;

	if (log_all_ref_updates < 0)
		log_all_ref_updates = !is_bare_repository();

	result = log_ref_setup(refname, log_file, sizeof(log_file));
	if (result)
		return result;

	logfd = open(log_file, oflags);
	if (logfd < 0)
		return 0;
	msglen = msg ? strlen(msg) : 0;
	committer = git_committer_info(0);
	maxlen = strlen(committer) + msglen + 100;
	logrec = xmalloc(maxlen);
	len = sprintf(logrec, "%s %s %s\n",
		      sha1_to_hex(old_sha1),
		      sha1_to_hex(new_sha1),
		      committer);
	if (msglen)
		len += copy_msg(logrec + len - 1, msg) - 1;
	written = len <= maxlen ? write_in_full(logfd, logrec, len) : -1;
	free(logrec);
	if (written != len) {
		int save_errno = errno;
		close(logfd);
		error("Unable to append to %s", log_file);
		errno = save_errno;
		return -1;
	}
	if (close(logfd)) {
		int save_errno = errno;
		error("Unable to append to %s", log_file);
		errno = save_errno;
		return -1;
	}
	return 0;
}

int is_branch(const char *refname)
{
	return !strcmp(refname, "HEAD") || starts_with(refname, "refs/heads/");
}

/* This function must return a meaningful errno */
int write_ref_sha1(struct ref_lock *lock,
	const unsigned char *sha1, const char *logmsg)
{
	static char term = '\n';
	struct object *o;

	if (!lock) {
		errno = EINVAL;
		return -1;
	}
	if (!lock->force_write && !hashcmp(lock->old_sha1, sha1)) {
		unlock_ref(lock);
		return 0;
	}
	o = parse_object(sha1);
	if (!o) {
		error("Trying to write ref %s with nonexistent object %s",
			lock->ref_name, sha1_to_hex(sha1));
		unlock_ref(lock);
		errno = EINVAL;
		return -1;
	}
	if (o->type != OBJ_COMMIT && is_branch(lock->ref_name)) {
		error("Trying to write non-commit object %s to branch %s",
			sha1_to_hex(sha1), lock->ref_name);
		unlock_ref(lock);
		errno = EINVAL;
		return -1;
	}
	if (write_in_full(lock->lock_fd, sha1_to_hex(sha1), 40) != 40 ||
	    write_in_full(lock->lock_fd, &term, 1) != 1 ||
	    close_ref(lock) < 0) {
		int save_errno = errno;
		error("Couldn't write %s", lock->lk->filename);
		unlock_ref(lock);
		errno = save_errno;
		return -1;
	}
	clear_loose_ref_cache(&ref_cache);
	if (log_ref_write(lock->ref_name, lock->old_sha1, sha1, logmsg) < 0 ||
	    (strcmp(lock->ref_name, lock->orig_ref_name) &&
	     log_ref_write(lock->orig_ref_name, lock->old_sha1, sha1, logmsg) < 0)) {
		unlock_ref(lock);
		return -1;
	}
	if (strcmp(lock->orig_ref_name, "HEAD") != 0) {
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
		head_ref = resolve_ref_unsafe("HEAD", head_sha1, 1, &head_flag);
		if (head_ref && (head_flag & REF_ISSYMREF) &&
		    !strcmp(head_ref, lock->ref_name))
			log_ref_write("HEAD", lock->old_sha1, sha1, logmsg);
	}
	if (commit_ref(lock)) {
		error("Couldn't set %s", lock->ref_name);
		unlock_ref(lock);
		return -1;
	}
	unlock_ref(lock);
	return 0;
}

int create_symref(const char *ref_target, const char *refs_heads_master,
		  const char *logmsg)
{
	const char *lockpath;
	char ref[1000];
	int fd, len, written;
	char *git_HEAD = git_pathdup("%s", ref_target);
	unsigned char old_sha1[20], new_sha1[20];

	if (logmsg && read_ref(ref_target, old_sha1))
		hashclr(old_sha1);

	if (safe_create_leading_directories(git_HEAD) < 0)
		return error("unable to create directory for %s", git_HEAD);

#ifndef NO_SYMLINK_HEAD
	if (prefer_symlink_refs) {
		unlink(git_HEAD);
		if (!symlink(refs_heads_master, git_HEAD))
			goto done;
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
	}
#endif

	len = snprintf(ref, sizeof(ref), "ref: %s\n", refs_heads_master);
	if (sizeof(ref) <= len) {
		error("refname too long: %s", refs_heads_master);
		goto error_free_return;
	}
	lockpath = mkpath("%s.lock", git_HEAD);
	fd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0) {
		error("Unable to open %s for writing", lockpath);
		goto error_free_return;
	}
	written = write_in_full(fd, ref, len);
	if (close(fd) != 0 || written != len) {
		error("Unable to write to %s", lockpath);
		goto error_unlink_return;
	}
	if (rename(lockpath, git_HEAD) < 0) {
		error("Unable to create %s", git_HEAD);
		goto error_unlink_return;
	}
	if (adjust_shared_perm(git_HEAD)) {
		error("Unable to fix permissions on %s", lockpath);
	error_unlink_return:
		unlink_or_warn(lockpath);
	error_free_return:
		free(git_HEAD);
		return -1;
	}

#ifndef NO_SYMLINK_HEAD
	done:
#endif
	if (logmsg && !read_ref(refs_heads_master, new_sha1))
		log_ref_write(ref_target, old_sha1, new_sha1, logmsg);

	free(git_HEAD);
	return 0;
}

struct read_ref_at_cb {
	const char *refname;
	unsigned long at_time;
	int cnt;
	int reccnt;
	unsigned char *sha1;
	int found_it;

	unsigned char osha1[20];
	unsigned char nsha1[20];
	int tz;
	unsigned long date;
	char **msg;
	unsigned long *cutoff_time;
	int *cutoff_tz;
	int *cutoff_cnt;
};

static int read_ref_at_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	cb->reccnt++;
	cb->tz = tz;
	cb->date = timestamp;

	if (timestamp <= cb->at_time || cb->cnt == 0) {
		if (cb->msg)
			*cb->msg = xstrdup(message);
		if (cb->cutoff_time)
			*cb->cutoff_time = timestamp;
		if (cb->cutoff_tz)
			*cb->cutoff_tz = tz;
		if (cb->cutoff_cnt)
			*cb->cutoff_cnt = cb->reccnt - 1;
		/*
		 * we have not yet updated cb->[n|o]sha1 so they still
		 * hold the values for the previous record.
		 */
		if (!is_null_sha1(cb->osha1)) {
			hashcpy(cb->sha1, nsha1);
			if (hashcmp(cb->osha1, nsha1))
				warning("Log for ref %s has gap after %s.",
					cb->refname, show_date(cb->date, cb->tz, DATE_RFC2822));
		}
		else if (cb->date == cb->at_time)
			hashcpy(cb->sha1, nsha1);
		else if (hashcmp(nsha1, cb->sha1))
			warning("Log for ref %s unexpectedly ended on %s.",
				cb->refname, show_date(cb->date, cb->tz,
						   DATE_RFC2822));
		hashcpy(cb->osha1, osha1);
		hashcpy(cb->nsha1, nsha1);
		cb->found_it = 1;
		return 1;
	}
	hashcpy(cb->osha1, osha1);
	hashcpy(cb->nsha1, nsha1);
	if (cb->cnt > 0)
		cb->cnt--;
	return 0;
}

static int read_ref_at_ent_oldest(unsigned char *osha1, unsigned char *nsha1,
				  const char *email, unsigned long timestamp,
				  int tz, const char *message, void *cb_data)
{
	struct read_ref_at_cb *cb = cb_data;

	if (cb->msg)
		*cb->msg = xstrdup(message);
	if (cb->cutoff_time)
		*cb->cutoff_time = timestamp;
	if (cb->cutoff_tz)
		*cb->cutoff_tz = tz;
	if (cb->cutoff_cnt)
		*cb->cutoff_cnt = cb->reccnt;
	hashcpy(cb->sha1, osha1);
	if (is_null_sha1(cb->sha1))
		hashcpy(cb->sha1, nsha1);
	/* We just want the first entry */
	return 1;
}

int read_ref_at(const char *refname, unsigned long at_time, int cnt,
		unsigned char *sha1, char **msg,
		unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt)
{
	struct read_ref_at_cb cb;

	memset(&cb, 0, sizeof(cb));
	cb.refname = refname;
	cb.at_time = at_time;
	cb.cnt = cnt;
	cb.msg = msg;
	cb.cutoff_time = cutoff_time;
	cb.cutoff_tz = cutoff_tz;
	cb.cutoff_cnt = cutoff_cnt;
	cb.sha1 = sha1;

	for_each_reflog_ent_reverse(refname, read_ref_at_ent, &cb);

	if (!cb.reccnt)
		die("Log for %s is empty.", refname);
	if (cb.found_it)
		return 0;

	for_each_reflog_ent(refname, read_ref_at_ent_oldest, &cb);

	return 1;
}

int reflog_exists(const char *refname)
{
	struct stat st;

	return !lstat(git_path("logs/%s", refname), &st) &&
		S_ISREG(st.st_mode);
}

int delete_reflog(const char *refname)
{
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

int for_each_reflog_ent_reverse(const char *refname, each_reflog_ent_fn fn, void *cb_data)
{
	struct strbuf sb = STRBUF_INIT;
	FILE *logfp;
	long pos;
	int ret = 0, at_tail = 1;

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

			if (*bp != '\n') {
				strbuf_splice(&sb, 0, 0, buf, endp - buf);
				if (pos)
					break; /* need to fill another block */
				scanp = buf - 1; /* leave loop */
			} else {
				/*
				 * (bp + 1) thru endp is the beginning of the
				 * current line we have in sb
				 */
				strbuf_splice(&sb, 0, 0, bp + 1, endp - (bp + 1));
				scanp = bp;
				endp = bp + 1;
			}
			ret = show_one_reflog_ent(&sb, fn, cb_data);
			strbuf_reset(&sb);
			if (ret)
				break;
		}

	}
	if (!ret && sb.len)
		ret = show_one_reflog_ent(&sb, fn, cb_data);

	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn, void *cb_data)
{
	FILE *logfp;
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	logfp = fopen(git_path("logs/%s", refname), "r");
	if (!logfp)
		return -1;

	while (!ret && !strbuf_getwholeline(&sb, logfp, '\n'))
		ret = show_one_reflog_ent(&sb, fn, cb_data);
	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}
/*
 * Call fn for each reflog in the namespace indicated by name.  name
 * must be empty or end with '/'.  Name will be used as a scratch
 * space, but its contents will be restored before return.
 */
static int do_for_each_reflog(struct strbuf *name, each_ref_fn fn, void *cb_data)
{
	DIR *d = opendir(git_path("logs/%s", name->buf));
	int retval = 0;
	struct dirent *de;
	int oldlen = name->len;

	if (!d)
		return name->len ? errno : 0;

	while ((de = readdir(d)) != NULL) {
		struct stat st;

		if (de->d_name[0] == '.')
			continue;
		if (ends_with(de->d_name, ".lock"))
			continue;
		strbuf_addstr(name, de->d_name);
		if (stat(git_path("logs/%s", name->buf), &st) < 0) {
			; /* silently ignore */
		} else {
			if (S_ISDIR(st.st_mode)) {
				strbuf_addch(name, '/');
				retval = do_for_each_reflog(name, fn, cb_data);
			} else {
				unsigned char sha1[20];
				if (read_ref_full(name->buf, sha1, 0, NULL))
					retval = error("bad ref for %s", name->buf);
				else
					retval = fn(name->buf, sha1, 0, cb_data);
			}
			if (retval)
				break;
		}
		strbuf_setlen(name, oldlen);
	}
	closedir(d);
	return retval;
}

int for_each_reflog(each_ref_fn fn, void *cb_data)
{
	int retval;
	struct strbuf name;
	strbuf_init(&name, PATH_MAX);
	retval = do_for_each_reflog(&name, fn, cb_data);
	strbuf_release(&name);
	return retval;
}

static struct ref_lock *update_ref_lock(const char *refname,
					const unsigned char *oldval,
					int flags, int *type_p,
					enum action_on_err onerr)
{
	struct ref_lock *lock;
	lock = lock_any_ref_for_update(refname, oldval, flags, type_p);
	if (!lock) {
		const char *str = "Cannot lock the ref '%s'.";
		switch (onerr) {
		case UPDATE_REFS_MSG_ON_ERR: error(str, refname); break;
		case UPDATE_REFS_DIE_ON_ERR: die(str, refname); break;
		case UPDATE_REFS_QUIET_ON_ERR: break;
		}
	}
	return lock;
}

static int update_ref_write(const char *action, const char *refname,
			    const unsigned char *sha1, struct ref_lock *lock,
			    struct strbuf *err, enum action_on_err onerr)
{
	if (write_ref_sha1(lock, sha1, action) < 0) {
		const char *str = "Cannot update the ref '%s'.";
		if (err)
			strbuf_addf(err, str, refname);

		switch (onerr) {
		case UPDATE_REFS_MSG_ON_ERR: error(str, refname); break;
		case UPDATE_REFS_DIE_ON_ERR: die(str, refname); break;
		case UPDATE_REFS_QUIET_ON_ERR: break;
		}
		return 1;
	}
	return 0;
}

/**
 * Information needed for a single ref update.  Set new_sha1 to the
 * new value or to zero to delete the ref.  To check the old value
 * while locking the ref, set have_old to 1 and set old_sha1 to the
 * value or to zero to ensure the ref does not exist before update.
 */
struct ref_update {
	unsigned char new_sha1[20];
	unsigned char old_sha1[20];
	int flags; /* REF_NODEREF? */
	int have_old; /* 1 if old_sha1 is valid, 0 otherwise */
	struct ref_lock *lock;
	int type;
	const char refname[FLEX_ARRAY];
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
};

struct ref_transaction *ref_transaction_begin(void)
{
	return xcalloc(1, sizeof(struct ref_transaction));
}

void ref_transaction_free(struct ref_transaction *transaction)
{
	int i;

	if (!transaction)
		return;

	for (i = 0; i < transaction->nr; i++)
		free(transaction->updates[i]);

	free(transaction->updates);
	free(transaction);
}

static struct ref_update *add_update(struct ref_transaction *transaction,
				     const char *refname)
{
	size_t len = strlen(refname);
	struct ref_update *update = xcalloc(1, sizeof(*update) + len + 1);

	strcpy((char *)update->refname, refname);
	ALLOC_GROW(transaction->updates, transaction->nr + 1, transaction->alloc);
	transaction->updates[transaction->nr++] = update;
	return update;
}

int ref_transaction_update(struct ref_transaction *transaction,
			   const char *refname,
			   const unsigned char *new_sha1,
			   const unsigned char *old_sha1,
			   int flags, int have_old,
			   struct strbuf *err)
{
	struct ref_update *update;

	if (have_old && !old_sha1)
		die("BUG: have_old is true but old_sha1 is NULL");

	update = add_update(transaction, refname);
	hashcpy(update->new_sha1, new_sha1);
	update->flags = flags;
	update->have_old = have_old;
	if (have_old)
		hashcpy(update->old_sha1, old_sha1);
	return 0;
}

void ref_transaction_create(struct ref_transaction *transaction,
			    const char *refname,
			    const unsigned char *new_sha1,
			    int flags)
{
	struct ref_update *update = add_update(transaction, refname);

	assert(!is_null_sha1(new_sha1));
	hashcpy(update->new_sha1, new_sha1);
	hashclr(update->old_sha1);
	update->flags = flags;
	update->have_old = 1;
}

void ref_transaction_delete(struct ref_transaction *transaction,
			    const char *refname,
			    const unsigned char *old_sha1,
			    int flags, int have_old)
{
	struct ref_update *update = add_update(transaction, refname);

	update->flags = flags;
	update->have_old = have_old;
	if (have_old) {
		assert(!is_null_sha1(old_sha1));
		hashcpy(update->old_sha1, old_sha1);
	}
}

int update_ref(const char *action, const char *refname,
	       const unsigned char *sha1, const unsigned char *oldval,
	       int flags, enum action_on_err onerr)
{
	struct ref_lock *lock;
	lock = update_ref_lock(refname, oldval, flags, NULL, onerr);
	if (!lock)
		return 1;
	return update_ref_write(action, refname, sha1, lock, NULL, onerr);
}

static int ref_update_compare(const void *r1, const void *r2)
{
	const struct ref_update * const *u1 = r1;
	const struct ref_update * const *u2 = r2;
	return strcmp((*u1)->refname, (*u2)->refname);
}

static int ref_update_reject_duplicates(struct ref_update **updates, int n,
					struct strbuf *err)
{
	int i;
	for (i = 1; i < n; i++)
		if (!strcmp(updates[i - 1]->refname, updates[i]->refname)) {
			const char *str =
				"Multiple updates for ref '%s' not allowed.";
			if (err)
				strbuf_addf(err, str, updates[i]->refname);

			return 1;
		}
	return 0;
}

int ref_transaction_commit(struct ref_transaction *transaction,
			   const char *msg, struct strbuf *err)
{
	int ret = 0, delnum = 0, i;
	const char **delnames;
	int n = transaction->nr;
	struct ref_update **updates = transaction->updates;

	if (!n)
		return 0;

	/* Allocate work space */
	delnames = xmalloc(sizeof(*delnames) * n);

	/* Copy, sort, and reject duplicate refs */
	qsort(updates, n, sizeof(*updates), ref_update_compare);
	ret = ref_update_reject_duplicates(updates, n, err);
	if (ret)
		goto cleanup;

	/* Acquire all locks while verifying old values */
	for (i = 0; i < n; i++) {
		struct ref_update *update = updates[i];

		update->lock = update_ref_lock(update->refname,
					       (update->have_old ?
						update->old_sha1 : NULL),
					       update->flags,
					       &update->type,
					       UPDATE_REFS_QUIET_ON_ERR);
		if (!update->lock) {
			if (err)
				strbuf_addf(err, "Cannot lock the ref '%s'.",
					    update->refname);
			ret = 1;
			goto cleanup;
		}
	}

	/* Perform updates first so live commits remain referenced */
	for (i = 0; i < n; i++) {
		struct ref_update *update = updates[i];

		if (!is_null_sha1(update->new_sha1)) {
			ret = update_ref_write(msg,
					       update->refname,
					       update->new_sha1,
					       update->lock, err,
					       UPDATE_REFS_QUIET_ON_ERR);
			update->lock = NULL; /* freed by update_ref_write */
			if (ret)
				goto cleanup;
		}
	}

	/* Perform deletes now that updates are safely completed */
	for (i = 0; i < n; i++) {
		struct ref_update *update = updates[i];

		if (update->lock) {
			delnames[delnum++] = update->lock->ref_name;
			ret |= delete_ref_loose(update->lock, update->type);
		}
	}

	ret |= repack_without_refs(delnames, delnum, err);
	for (i = 0; i < delnum; i++)
		unlink_or_warn(git_path("logs/%s", delnames[i]));
	clear_loose_ref_cache(&ref_cache);

cleanup:
	for (i = 0; i < n; i++)
		if (updates[i]->lock)
			unlock_ref(updates[i]->lock);
	free(delnames);
	return ret;
}

char *shorten_unambiguous_ref(const char *refname, int strict)
{
	int i;
	static char **scanf_fmts;
	static int nr_rules;
	char *short_name;

	if (!nr_rules) {
		/*
		 * Pre-generate scanf formats from ref_rev_parse_rules[].
		 * Generate a format suitable for scanf from a
		 * ref_rev_parse_rules rule by interpolating "%s" at the
		 * location of the "%.*s".
		 */
		size_t total_len = 0;
		size_t offset = 0;

		/* the rule list is NULL terminated, count them first */
		for (nr_rules = 0; ref_rev_parse_rules[nr_rules]; nr_rules++)
			/* -2 for strlen("%.*s") - strlen("%s"); +1 for NUL */
			total_len += strlen(ref_rev_parse_rules[nr_rules]) - 2 + 1;

		scanf_fmts = xmalloc(nr_rules * sizeof(char *) + total_len);

		offset = 0;
		for (i = 0; i < nr_rules; i++) {
			assert(offset < total_len);
			scanf_fmts[i] = (char *)&scanf_fmts[nr_rules] + offset;
			offset += snprintf(scanf_fmts[i], total_len - offset,
					   ref_rev_parse_rules[i], 2, "%s") + 1;
		}
	}

	/* bail out if there are no rules */
	if (!nr_rules)
		return xstrdup(refname);

	/* buffer for scanf result, at most refname must fit */
	short_name = xstrdup(refname);

	/* skip first rule, it will always match */
	for (i = nr_rules - 1; i > 0 ; --i) {
		int j;
		int rules_to_fail = i;
		int short_name_len;

		if (1 != sscanf(refname, scanf_fmts[i], short_name))
			continue;

		short_name_len = strlen(short_name);

		/*
		 * in strict mode, all (except the matched one) rules
		 * must fail to resolve to a valid non-ambiguous ref
		 */
		if (strict)
			rules_to_fail = nr_rules;

		/*
		 * check if the short name resolves to a valid ref,
		 * but use only rules prior to the matched one
		 */
		for (j = 0; j < rules_to_fail; j++) {
			const char *rule = ref_rev_parse_rules[j];
			char refname[PATH_MAX];

			/* skip matched rule */
			if (i == j)
				continue;

			/*
			 * the short name is ambiguous, if it resolves
			 * (with this previous rule) to a valid ref
			 * read_ref() returns 0 on success
			 */
			mksnpath(refname, sizeof(refname),
				 rule, short_name_len, short_name);
			if (ref_exists(refname))
				break;
		}

		/*
		 * short name is non-ambiguous if all previous rules
		 * haven't resolved to a valid ref
		 */
		if (j == rules_to_fail)
			return short_name;
	}

	free(short_name);
	return xstrdup(refname);
}

static struct string_list *hide_refs;

int parse_hide_refs_config(const char *var, const char *value, const char *section)
{
	if (!strcmp("transfer.hiderefs", var) ||
	    /* NEEDSWORK: use parse_config_key() once both are merged */
	    (starts_with(var, section) && var[strlen(section)] == '.' &&
	     !strcmp(var + strlen(section), ".hiderefs"))) {
		char *ref;
		int len;

		if (!value)
			return config_error_nonbool(var);
		ref = xstrdup(value);
		len = strlen(ref);
		while (len && ref[len - 1] == '/')
			ref[--len] = '\0';
		if (!hide_refs) {
			hide_refs = xcalloc(1, sizeof(*hide_refs));
			hide_refs->strdup_strings = 1;
		}
		string_list_append(hide_refs, ref);
	}
	return 0;
}

int ref_is_hidden(const char *refname)
{
	struct string_list_item *item;

	if (!hide_refs)
		return 0;
	for_each_string_list_item(item, hide_refs) {
		int len;
		if (!starts_with(refname, item->string))
			continue;
		len = strlen(item->string);
		if (!refname[len] || refname[len] == '/')
			return 1;
	}
	return 0;
}
