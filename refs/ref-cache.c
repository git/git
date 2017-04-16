#include "../cache.h"
#include "../refs.h"
#include "refs-internal.h"
#include "ref-cache.h"
#include "../iterator.h"

void add_entry_to_dir(struct ref_dir *dir, struct ref_entry *entry)
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

struct ref_dir *get_ref_dir(struct ref_entry *entry)
{
	struct ref_dir *dir;
	assert(entry->flag & REF_DIR);
	dir = &entry->u.subdir;
	if (entry->flag & REF_INCOMPLETE) {
		if (!dir->cache->fill_ref_dir)
			die("BUG: incomplete ref_store without fill_ref_dir function");

		dir->cache->fill_ref_dir(dir->cache->ref_store, dir, entry->name);
		entry->flag &= ~REF_INCOMPLETE;
	}
	return dir;
}

struct ref_entry *create_ref_entry(const char *refname,
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

struct ref_cache *create_ref_cache(struct ref_store *refs,
				   fill_ref_dir_fn *fill_ref_dir)
{
	struct ref_cache *ret = xcalloc(1, sizeof(*ret));

	ret->ref_store = refs;
	ret->fill_ref_dir = fill_ref_dir;
	ret->root = create_dir_entry(ret, "", 0, 1);
	return ret;
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

void free_ref_cache(struct ref_cache *cache)
{
	free_ref_entry(cache->root);
	free(cache);
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

struct ref_entry *create_dir_entry(struct ref_cache *cache,
				   const char *dirname, size_t len,
				   int incomplete)
{
	struct ref_entry *direntry;

	FLEX_ALLOC_MEM(direntry, name, dirname, len);
	direntry->u.subdir.cache = cache;
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

int search_ref_dir(struct ref_dir *dir, const char *refname, size_t len)
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
		entry = create_dir_entry(dir->cache, subdirname, len, 0);
		add_entry_to_dir(dir, entry);
	} else {
		entry = dir->entries[entry_index];
	}
	return get_ref_dir(entry);
}

/*
 * If refname is a reference name, find the ref_dir within the dir
 * tree that should hold refname. If refname is a directory name
 * (i.e., it ends in '/'), then return that ref_dir itself. dir must
 * represent the top-level directory and must already be complete.
 * Sort ref_dirs and recurse into subdirectories as necessary. If
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

struct ref_entry *find_ref_entry(struct ref_dir *dir, const char *refname)
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

int remove_entry_from_dir(struct ref_dir *dir, const char *refname)
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

int add_ref_entry(struct ref_dir *dir, struct ref_entry *ref)
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
 * Load all of the refs from `dir` (recursively) into our in-memory
 * cache.
 */
static void prime_ref_dir(struct ref_dir *dir)
{
	/*
	 * The hard work of loading loose refs is done by get_ref_dir(), so we
	 * just need to recurse through all of the sub-directories. We do not
	 * even need to care about sorting, as traversal order does not matter
	 * to us.
	 */
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

enum peel_status peel_entry(struct ref_entry *entry, int repeel)
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

struct ref_iterator *cache_ref_iterator_begin(struct ref_cache *cache,
					      const char *prefix,
					      int prime_dir)
{
	struct ref_dir *dir;
	struct cache_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	struct cache_ref_iterator_level *level;

	dir = get_ref_dir(cache->root);
	if (prefix && *prefix)
		dir = find_containing_dir(dir, prefix, 0);
	if (!dir)
		/* There's nothing to iterate over. */
		return  empty_ref_iterator_begin();

	if (prime_dir)
		prime_ref_dir(dir);

	iter = xcalloc(1, sizeof(*iter));
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &cache_ref_iterator_vtable);
	ALLOC_GROW(iter->levels, 10, iter->levels_alloc);

	iter->levels_nr = 1;
	level = &iter->levels[0];
	level->index = -1;
	level->dir = dir;

	if (prefix && *prefix)
		ref_iterator = prefix_ref_iterator_begin(ref_iterator,
							 prefix, 0);

	return ref_iterator;
}
