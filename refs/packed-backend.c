#include "../cache.h"
#include "../refs.h"
#include "refs-internal.h"
#include "ref-cache.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../config.h"

struct packed_ref_cache {
	struct ref_cache *cache;

	/*
	 * Count of references to the data structure in this instance,
	 * including the pointer from files_ref_store::packed if any.
	 * The data will not be freed as long as the reference count
	 * is nonzero.
	 */
	unsigned int referrers;

	/* The metadata from when this packed-refs cache was read */
	struct stat_validity validity;
};

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
		free_ref_cache(packed_refs->cache);
		stat_validity_clear(&packed_refs->validity);
		free(packed_refs);
		return 1;
	} else {
		return 0;
	}
}

/*
 * A container for `packed-refs`-related data. It is not (yet) a
 * `ref_store`.
 */
struct packed_ref_store {
	unsigned int store_flags;

	/* The path of the "packed-refs" file: */
	char *path;

	/*
	 * A cache of the values read from the `packed-refs` file, if
	 * it might still be current; otherwise, NULL.
	 */
	struct packed_ref_cache *cache;

	/*
	 * Lock used for the "packed-refs" file. Note that this (and
	 * thus the enclosing `packed_ref_store`) must not be freed.
	 */
	struct lock_file lock;
};

struct packed_ref_store *packed_ref_store_create(
		const char *path, unsigned int store_flags)
{
	struct packed_ref_store *refs = xcalloc(1, sizeof(*refs));

	refs->store_flags = store_flags;
	refs->path = xstrdup(path);
	return refs;
}

/*
 * Die if refs is not the main ref store. caller is used in any
 * necessary error messages.
 */
static void packed_assert_main_repository(struct packed_ref_store *refs,
					  const char *caller)
{
	if (refs->store_flags & REF_STORE_MAIN)
		return;

	die("BUG: operation %s only allowed for main ref store", caller);
}

static void clear_packed_ref_cache(struct packed_ref_store *refs)
{
	if (refs->cache) {
		struct packed_ref_cache *cache = refs->cache;

		if (is_lock_file_locked(&refs->lock))
			die("BUG: packed-ref cache cleared while locked");
		refs->cache = NULL;
		release_packed_ref_cache(cache);
	}
}

/* The length of a peeled reference line in packed-refs, including EOL: */
#define PEELED_LINE_LENGTH 42

/*
 * Parse one line from a packed-refs file.  Write the SHA1 to sha1.
 * Return a pointer to the refname within the line (null-terminated),
 * or NULL if there was a problem.
 */
static const char *parse_ref_line(struct strbuf *line, struct object_id *oid)
{
	const char *ref;

	if (parse_oid_hex(line->buf, oid, &ref) < 0)
		return NULL;
	if (!isspace(*ref++))
		return NULL;

	if (isspace(*ref))
		return NULL;

	if (line->buf[line->len - 1] != '\n')
		return NULL;
	line->buf[--line->len] = 0;

	return ref;
}

/*
 * Read from `packed_refs_file` into a newly-allocated
 * `packed_ref_cache` and return it. The return value will already
 * have its reference count incremented.
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
static struct packed_ref_cache *read_packed_refs(const char *packed_refs_file)
{
	FILE *f;
	struct packed_ref_cache *packed_refs = xcalloc(1, sizeof(*packed_refs));
	struct ref_entry *last = NULL;
	struct strbuf line = STRBUF_INIT;
	enum { PEELED_NONE, PEELED_TAGS, PEELED_FULLY } peeled = PEELED_NONE;
	struct ref_dir *dir;

	acquire_packed_ref_cache(packed_refs);
	packed_refs->cache = create_ref_cache(NULL, NULL);
	packed_refs->cache->root->flag &= ~REF_INCOMPLETE;

	f = fopen(packed_refs_file, "r");
	if (!f) {
		if (errno == ENOENT) {
			/*
			 * This is OK; it just means that no
			 * "packed-refs" file has been written yet,
			 * which is equivalent to it being empty.
			 */
			return packed_refs;
		} else {
			die_errno("couldn't read %s", packed_refs_file);
		}
	}

	stat_validity_update(&packed_refs->validity, fileno(f));

	dir = get_ref_dir(packed_refs->cache->root);
	while (strbuf_getwholeline(&line, f, '\n') != EOF) {
		struct object_id oid;
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

		refname = parse_ref_line(&line, &oid);
		if (refname) {
			int flag = REF_ISPACKED;

			if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
				if (!refname_is_safe(refname))
					die("packed refname is dangerous: %s", refname);
				oidclr(&oid);
				flag |= REF_BAD_NAME | REF_ISBROKEN;
			}
			last = create_ref_entry(refname, &oid, flag);
			if (peeled == PEELED_FULLY ||
			    (peeled == PEELED_TAGS && starts_with(refname, "refs/tags/")))
				last->flag |= REF_KNOWS_PEELED;
			add_ref_entry(dir, last);
			continue;
		}
		if (last &&
		    line.buf[0] == '^' &&
		    line.len == PEELED_LINE_LENGTH &&
		    line.buf[PEELED_LINE_LENGTH - 1] == '\n' &&
		    !get_oid_hex(line.buf + 1, &oid)) {
			oidcpy(&last->u.value.peeled, &oid);
			/*
			 * Regardless of what the file header said,
			 * we definitely know the value of *this*
			 * reference:
			 */
			last->flag |= REF_KNOWS_PEELED;
		}
	}

	fclose(f);
	strbuf_release(&line);

	return packed_refs;
}

/*
 * Check that the packed refs cache (if any) still reflects the
 * contents of the file. If not, clear the cache.
 */
static void validate_packed_ref_cache(struct packed_ref_store *refs)
{
	if (refs->cache &&
	    !stat_validity_check(&refs->cache->validity, refs->path))
		clear_packed_ref_cache(refs);
}

/*
 * Get the packed_ref_cache for the specified packed_ref_store,
 * creating and populating it if it hasn't been read before or if the
 * file has been changed (according to its `validity` field) since it
 * was last read. On the other hand, if we hold the lock, then assume
 * that the file hasn't been changed out from under us, so skip the
 * extra `stat()` call in `stat_validity_check()`.
 */
static struct packed_ref_cache *get_packed_ref_cache(struct packed_ref_store *refs)
{
	if (!is_lock_file_locked(&refs->lock))
		validate_packed_ref_cache(refs);

	if (!refs->cache)
		refs->cache = read_packed_refs(refs->path);

	return refs->cache;
}

static struct ref_dir *get_packed_ref_dir(struct packed_ref_cache *packed_ref_cache)
{
	return get_ref_dir(packed_ref_cache->cache->root);
}

static struct ref_dir *get_packed_refs(struct packed_ref_store *refs)
{
	return get_packed_ref_dir(get_packed_ref_cache(refs));
}

/*
 * Add or overwrite a reference in the in-memory packed reference
 * cache. This may only be called while the packed-refs file is locked
 * (see lock_packed_refs()). To actually write the packed-refs file,
 * call commit_packed_refs().
 */
void add_packed_ref(struct packed_ref_store *refs,
		    const char *refname, const struct object_id *oid)
{
	struct ref_dir *packed_refs;
	struct ref_entry *packed_entry;

	if (!is_lock_file_locked(&refs->lock))
		die("BUG: packed refs not locked");

	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		die("Reference has invalid format: '%s'", refname);

	packed_refs = get_packed_refs(refs);
	packed_entry = find_ref_entry(packed_refs, refname);
	if (packed_entry) {
		/* Overwrite the existing entry: */
		oidcpy(&packed_entry->u.value.oid, oid);
		packed_entry->flag = REF_ISPACKED;
		oidclr(&packed_entry->u.value.peeled);
	} else {
		packed_entry = create_ref_entry(refname, oid, REF_ISPACKED);
		add_ref_entry(packed_refs, packed_entry);
	}
}

/*
 * Return the ref_entry for the given refname from the packed
 * references.  If it does not exist, return NULL.
 */
static struct ref_entry *get_packed_ref(struct packed_ref_store *refs,
					const char *refname)
{
	return find_ref_entry(get_packed_refs(refs), refname);
}

int packed_read_raw_ref(struct packed_ref_store *refs,
			const char *refname, unsigned char *sha1,
			struct strbuf *referent, unsigned int *type)
{
	struct ref_entry *entry;

	*type = 0;

	entry = get_packed_ref(refs, refname);
	if (!entry) {
		errno = ENOENT;
		return -1;
	}

	hashcpy(sha1, entry->u.value.oid.hash);
	*type = REF_ISPACKED;
	return 0;
}

int packed_peel_ref(struct packed_ref_store *refs,
		    const char *refname, unsigned char *sha1)
{
	struct ref_entry *r = get_packed_ref(refs, refname);

	if (!r || peel_entry(r, 0))
		return -1;

	hashcpy(sha1, r->u.value.peeled.hash);
	return 0;
}

struct packed_ref_iterator {
	struct ref_iterator base;

	struct packed_ref_cache *cache;
	struct ref_iterator *iter0;
	unsigned int flags;
};

static int packed_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;
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

static int packed_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;

	return ref_iterator_peel(iter->iter0, peeled);
}

static int packed_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	if (iter->iter0)
		ok = ref_iterator_abort(iter->iter0);

	release_packed_ref_cache(iter->cache);
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable packed_ref_iterator_vtable = {
	packed_ref_iterator_advance,
	packed_ref_iterator_peel,
	packed_ref_iterator_abort
};

struct ref_iterator *packed_ref_iterator_begin(
		struct packed_ref_store *refs,
		const char *prefix, unsigned int flags)
{
	struct packed_ref_iterator *iter;
	struct ref_iterator *ref_iterator;

	iter = xcalloc(1, sizeof(*iter));
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &packed_ref_iterator_vtable);

	/*
	 * Note that get_packed_ref_cache() internally checks whether
	 * the packed-ref cache is up to date with what is on disk,
	 * and re-reads it if not.
	 */

	iter->cache = get_packed_ref_cache(refs);
	acquire_packed_ref_cache(iter->cache);
	iter->iter0 = cache_ref_iterator_begin(iter->cache->cache, prefix, 0);

	iter->flags = flags;

	return ref_iterator;
}

/*
 * Write an entry to the packed-refs file for the specified refname.
 * If peeled is non-NULL, write it as the entry's peeled value.
 */
static void write_packed_entry(FILE *fh, const char *refname,
			       const unsigned char *sha1,
			       const unsigned char *peeled)
{
	fprintf_or_die(fh, "%s %s\n", sha1_to_hex(sha1), refname);
	if (peeled)
		fprintf_or_die(fh, "^%s\n", sha1_to_hex(peeled));
}

int lock_packed_refs(struct packed_ref_store *refs, int flags)
{
	static int timeout_configured = 0;
	static int timeout_value = 1000;
	struct packed_ref_cache *packed_ref_cache;

	packed_assert_main_repository(refs, "lock_packed_refs");

	if (!timeout_configured) {
		git_config_get_int("core.packedrefstimeout", &timeout_value);
		timeout_configured = 1;
	}

	if (hold_lock_file_for_update_timeout(
			    &refs->lock,
			    refs->path,
			    flags, timeout_value) < 0)
		return -1;

	/*
	 * Now that we hold the `packed-refs` lock, make sure that our
	 * cache matches the current version of the file. Normally
	 * `get_packed_ref_cache()` does that for us, but that
	 * function assumes that when the file is locked, any existing
	 * cache is still valid. We've just locked the file, but it
	 * might have changed the moment *before* we locked it.
	 */
	validate_packed_ref_cache(refs);

	packed_ref_cache = get_packed_ref_cache(refs);
	/* Increment the reference count to prevent it from being freed: */
	acquire_packed_ref_cache(packed_ref_cache);
	return 0;
}

/*
 * The packed-refs header line that we write out.  Perhaps other
 * traits will be added later.  The trailing space is required.
 */
static const char PACKED_REFS_HEADER[] =
	"# pack-refs with: peeled fully-peeled \n";

/*
 * Write the current version of the packed refs cache from memory to
 * disk. The packed-refs file must already be locked for writing (see
 * lock_packed_refs()). Return zero on success. On errors, set errno
 * and return a nonzero value.
 */
int commit_packed_refs(struct packed_ref_store *refs)
{
	struct packed_ref_cache *packed_ref_cache =
		get_packed_ref_cache(refs);
	int ok, error = 0;
	int save_errno = 0;
	FILE *out;
	struct ref_iterator *iter;

	packed_assert_main_repository(refs, "commit_packed_refs");

	if (!is_lock_file_locked(&refs->lock))
		die("BUG: packed-refs not locked");

	out = fdopen_lock_file(&refs->lock, "w");
	if (!out)
		die_errno("unable to fdopen packed-refs descriptor");

	fprintf_or_die(out, "%s", PACKED_REFS_HEADER);

	iter = cache_ref_iterator_begin(packed_ref_cache->cache, NULL, 0);
	while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
		struct object_id peeled;
		int peel_error = ref_iterator_peel(iter, &peeled);

		write_packed_entry(out, iter->refname, iter->oid->hash,
				   peel_error ? NULL : peeled.hash);
	}

	if (ok != ITER_DONE)
		die("error while iterating over references");

	if (commit_lock_file(&refs->lock)) {
		save_errno = errno;
		error = -1;
	}
	release_packed_ref_cache(packed_ref_cache);
	errno = save_errno;
	return error;
}

/*
 * Rollback the lockfile for the packed-refs file, and discard the
 * in-memory packed reference cache.  (The packed-refs file will be
 * read anew if it is needed again after this function is called.)
 */
static void rollback_packed_refs(struct packed_ref_store *refs)
{
	struct packed_ref_cache *packed_ref_cache = get_packed_ref_cache(refs);

	packed_assert_main_repository(refs, "rollback_packed_refs");

	if (!is_lock_file_locked(&refs->lock))
		die("BUG: packed-refs not locked");
	rollback_lock_file(&refs->lock);
	release_packed_ref_cache(packed_ref_cache);
	clear_packed_ref_cache(refs);
}

/*
 * Rewrite the packed-refs file, omitting any refs listed in
 * 'refnames'. On error, leave packed-refs unchanged, write an error
 * message to 'err', and return a nonzero value.
 *
 * The refs in 'refnames' needn't be sorted. `err` must not be NULL.
 */
int repack_without_refs(struct packed_ref_store *refs,
			struct string_list *refnames, struct strbuf *err)
{
	struct ref_dir *packed;
	struct string_list_item *refname;
	int ret, needs_repacking = 0, removed = 0;

	packed_assert_main_repository(refs, "repack_without_refs");
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
		unable_to_lock_message(refs->path, errno, err);
		return -1;
	}
	packed = get_packed_refs(refs);

	/* Remove refnames from the cache */
	for_each_string_list_item(refname, refnames)
		if (remove_entry_from_dir(packed, refname->string) != -1)
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
