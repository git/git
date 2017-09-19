#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "ref-cache.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"

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
	struct ref_store base;

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

	/*
	 * Temporary file used when rewriting new contents to the
	 * "packed-refs" file. Note that this (and thus the enclosing
	 * `packed_ref_store`) must not be freed.
	 */
	struct tempfile *tempfile;
};

struct ref_store *packed_ref_store_create(const char *path,
					  unsigned int store_flags)
{
	struct packed_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct ref_store *ref_store = (struct ref_store *)refs;

	base_ref_store_init(ref_store, &refs_be_packed);
	refs->store_flags = store_flags;

	refs->path = xstrdup(path);
	return ref_store;
}

/*
 * Downcast `ref_store` to `packed_ref_store`. Die if `ref_store` is
 * not a `packed_ref_store`. Also die if `packed_ref_store` doesn't
 * support at least the flags specified in `required_flags`. `caller`
 * is used in any necessary error messages.
 */
static struct packed_ref_store *packed_downcast(struct ref_store *ref_store,
						unsigned int required_flags,
						const char *caller)
{
	struct packed_ref_store *refs;

	if (ref_store->be != &refs_be_packed)
		die("BUG: ref_store is type \"%s\" not \"packed\" in %s",
		    ref_store->be->name, caller);

	refs = (struct packed_ref_store *)ref_store;

	if ((refs->store_flags & required_flags) != required_flags)
		die("BUG: unallowed operation (%s), requires %x, has %x\n",
		    caller, required_flags, refs->store_flags);

	return refs;
}

static void clear_packed_ref_cache(struct packed_ref_store *refs)
{
	if (refs->cache) {
		struct packed_ref_cache *cache = refs->cache;

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

		if (!line.len || line.buf[line.len - 1] != '\n')
			die("unterminated line in %s: %s", packed_refs_file, line.buf);

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
		} else if (last &&
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
		} else {
			strbuf_setlen(&line, line.len - 1);
			die("unexpected line in %s: %s", packed_refs_file, line.buf);
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
 * Return the ref_entry for the given refname from the packed
 * references.  If it does not exist, return NULL.
 */
static struct ref_entry *get_packed_ref(struct packed_ref_store *refs,
					const char *refname)
{
	return find_ref_entry(get_packed_refs(refs), refname);
}

static int packed_read_raw_ref(struct ref_store *ref_store,
			       const char *refname, unsigned char *sha1,
			       struct strbuf *referent, unsigned int *type)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_READ, "read_raw_ref");

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

static int packed_peel_ref(struct ref_store *ref_store,
			   const char *refname, unsigned char *sha1)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_READ | REF_STORE_ODB,
				"peel_ref");
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

static struct ref_iterator *packed_ref_iterator_begin(
		struct ref_store *ref_store,
		const char *prefix, unsigned int flags)
{
	struct packed_ref_store *refs;
	struct packed_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	unsigned int required_flags = REF_STORE_READ;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN))
		required_flags |= REF_STORE_ODB;
	refs = packed_downcast(ref_store, required_flags, "ref_iterator_begin");

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
 * If peeled is non-NULL, write it as the entry's peeled value. On
 * error, return a nonzero value and leave errno set at the value left
 * by the failing call to `fprintf()`.
 */
static int write_packed_entry(FILE *fh, const char *refname,
			      const unsigned char *sha1,
			      const unsigned char *peeled)
{
	if (fprintf(fh, "%s %s\n", sha1_to_hex(sha1), refname) < 0 ||
	    (peeled && fprintf(fh, "^%s\n", sha1_to_hex(peeled)) < 0))
		return -1;

	return 0;
}

int packed_refs_lock(struct ref_store *ref_store, int flags, struct strbuf *err)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_WRITE | REF_STORE_MAIN,
				"packed_refs_lock");
	static int timeout_configured = 0;
	static int timeout_value = 1000;

	if (!timeout_configured) {
		git_config_get_int("core.packedrefstimeout", &timeout_value);
		timeout_configured = 1;
	}

	/*
	 * Note that we close the lockfile immediately because we
	 * don't write new content to it, but rather to a separate
	 * tempfile.
	 */
	if (hold_lock_file_for_update_timeout(
			    &refs->lock,
			    refs->path,
			    flags, timeout_value) < 0) {
		unable_to_lock_message(refs->path, errno, err);
		return -1;
	}

	if (close_lock_file_gently(&refs->lock)) {
		strbuf_addf(err, "unable to close %s: %s", refs->path, strerror(errno));
		rollback_lock_file(&refs->lock);
		return -1;
	}

	/*
	 * Now that we hold the `packed-refs` lock, make sure that our
	 * cache matches the current version of the file. Normally
	 * `get_packed_ref_cache()` does that for us, but that
	 * function assumes that when the file is locked, any existing
	 * cache is still valid. We've just locked the file, but it
	 * might have changed the moment *before* we locked it.
	 */
	validate_packed_ref_cache(refs);

	/*
	 * Now make sure that the packed-refs file as it exists in the
	 * locked state is loaded into the cache:
	 */
	get_packed_ref_cache(refs);
	return 0;
}

void packed_refs_unlock(struct ref_store *ref_store)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"packed_refs_unlock");

	if (!is_lock_file_locked(&refs->lock))
		die("BUG: packed_refs_unlock() called when not locked");
	rollback_lock_file(&refs->lock);
}

int packed_refs_is_locked(struct ref_store *ref_store)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"packed_refs_is_locked");

	return is_lock_file_locked(&refs->lock);
}

/*
 * The packed-refs header line that we write out.  Perhaps other
 * traits will be added later.  The trailing space is required.
 */
static const char PACKED_REFS_HEADER[] =
	"# pack-refs with: peeled fully-peeled \n";

static int packed_init_db(struct ref_store *ref_store, struct strbuf *err)
{
	/* Nothing to do. */
	return 0;
}

/*
 * Write the packed-refs from the cache to the packed-refs tempfile,
 * incorporating any changes from `updates`. `updates` must be a
 * sorted string list whose keys are the refnames and whose util
 * values are `struct ref_update *`. On error, rollback the tempfile,
 * write an error message to `err`, and return a nonzero value.
 *
 * The packfile must be locked before calling this function and will
 * remain locked when it is done.
 */
static int write_with_updates(struct packed_ref_store *refs,
			      struct string_list *updates,
			      struct strbuf *err)
{
	struct ref_iterator *iter = NULL;
	size_t i;
	int ok;
	FILE *out;
	struct strbuf sb = STRBUF_INIT;
	char *packed_refs_path;

	if (!is_lock_file_locked(&refs->lock))
		die("BUG: write_with_updates() called while unlocked");

	/*
	 * If packed-refs is a symlink, we want to overwrite the
	 * symlinked-to file, not the symlink itself. Also, put the
	 * staging file next to it:
	 */
	packed_refs_path = get_locked_file_path(&refs->lock);
	strbuf_addf(&sb, "%s.new", packed_refs_path);
	free(packed_refs_path);
	refs->tempfile = create_tempfile(sb.buf);
	if (!refs->tempfile) {
		strbuf_addf(err, "unable to create file %s: %s",
			    sb.buf, strerror(errno));
		strbuf_release(&sb);
		return -1;
	}
	strbuf_release(&sb);

	out = fdopen_tempfile(refs->tempfile, "w");
	if (!out) {
		strbuf_addf(err, "unable to fdopen packed-refs tempfile: %s",
			    strerror(errno));
		goto error;
	}

	if (fprintf(out, "%s", PACKED_REFS_HEADER) < 0)
		goto write_error;

	/*
	 * We iterate in parallel through the current list of refs and
	 * the list of updates, processing an entry from at least one
	 * of the lists each time through the loop. When the current
	 * list of refs is exhausted, set iter to NULL. When the list
	 * of updates is exhausted, leave i set to updates->nr.
	 */
	iter = packed_ref_iterator_begin(&refs->base, "",
					 DO_FOR_EACH_INCLUDE_BROKEN);
	if ((ok = ref_iterator_advance(iter)) != ITER_OK)
		iter = NULL;

	i = 0;

	while (iter || i < updates->nr) {
		struct ref_update *update = NULL;
		int cmp;

		if (i >= updates->nr) {
			cmp = -1;
		} else {
			update = updates->items[i].util;

			if (!iter)
				cmp = +1;
			else
				cmp = strcmp(iter->refname, update->refname);
		}

		if (!cmp) {
			/*
			 * There is both an old value and an update
			 * for this reference. Check the old value if
			 * necessary:
			 */
			if ((update->flags & REF_HAVE_OLD)) {
				if (is_null_oid(&update->old_oid)) {
					strbuf_addf(err, "cannot update ref '%s': "
						    "reference already exists",
						    update->refname);
					goto error;
				} else if (oidcmp(&update->old_oid, iter->oid)) {
					strbuf_addf(err, "cannot update ref '%s': "
						    "is at %s but expected %s",
						    update->refname,
						    oid_to_hex(iter->oid),
						    oid_to_hex(&update->old_oid));
					goto error;
				}
			}

			/* Now figure out what to use for the new value: */
			if ((update->flags & REF_HAVE_NEW)) {
				/*
				 * The update takes precedence. Skip
				 * the iterator over the unneeded
				 * value.
				 */
				if ((ok = ref_iterator_advance(iter)) != ITER_OK)
					iter = NULL;
				cmp = +1;
			} else {
				/*
				 * The update doesn't actually want to
				 * change anything. We're done with it.
				 */
				i++;
				cmp = -1;
			}
		} else if (cmp > 0) {
			/*
			 * There is no old value but there is an
			 * update for this reference. Make sure that
			 * the update didn't expect an existing value:
			 */
			if ((update->flags & REF_HAVE_OLD) &&
			    !is_null_oid(&update->old_oid)) {
				strbuf_addf(err, "cannot update ref '%s': "
					    "reference is missing but expected %s",
					    update->refname,
					    oid_to_hex(&update->old_oid));
				goto error;
			}
		}

		if (cmp < 0) {
			/* Pass the old reference through. */

			struct object_id peeled;
			int peel_error = ref_iterator_peel(iter, &peeled);

			if (write_packed_entry(out, iter->refname,
					       iter->oid->hash,
					       peel_error ? NULL : peeled.hash))
				goto write_error;

			if ((ok = ref_iterator_advance(iter)) != ITER_OK)
				iter = NULL;
		} else if (is_null_oid(&update->new_oid)) {
			/*
			 * The update wants to delete the reference,
			 * and the reference either didn't exist or we
			 * have already skipped it. So we're done with
			 * the update (and don't have to write
			 * anything).
			 */
			i++;
		} else {
			struct object_id peeled;
			int peel_error = peel_object(update->new_oid.hash,
						     peeled.hash);

			if (write_packed_entry(out, update->refname,
					       update->new_oid.hash,
					       peel_error ? NULL : peeled.hash))
				goto write_error;

			i++;
		}
	}

	if (ok != ITER_DONE) {
		strbuf_addf(err, "unable to write packed-refs file: "
			    "error iterating over old contents");
		goto error;
	}

	if (close_tempfile_gently(refs->tempfile)) {
		strbuf_addf(err, "error closing file %s: %s",
			    get_tempfile_path(refs->tempfile),
			    strerror(errno));
		strbuf_release(&sb);
		delete_tempfile(&refs->tempfile);
		return -1;
	}

	return 0;

write_error:
	strbuf_addf(err, "error writing to %s: %s",
		    get_tempfile_path(refs->tempfile), strerror(errno));

error:
	if (iter)
		ref_iterator_abort(iter);

	delete_tempfile(&refs->tempfile);
	return -1;
}

struct packed_transaction_backend_data {
	/* True iff the transaction owns the packed-refs lock. */
	int own_lock;

	struct string_list updates;
};

static void packed_transaction_cleanup(struct packed_ref_store *refs,
				       struct ref_transaction *transaction)
{
	struct packed_transaction_backend_data *data = transaction->backend_data;

	if (data) {
		string_list_clear(&data->updates, 0);

		if (is_tempfile_active(refs->tempfile))
			delete_tempfile(&refs->tempfile);

		if (data->own_lock && is_lock_file_locked(&refs->lock)) {
			packed_refs_unlock(&refs->base);
			data->own_lock = 0;
		}

		free(data);
		transaction->backend_data = NULL;
	}

	transaction->state = REF_TRANSACTION_CLOSED;
}

static int packed_transaction_prepare(struct ref_store *ref_store,
				      struct ref_transaction *transaction,
				      struct strbuf *err)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_prepare");
	struct packed_transaction_backend_data *data;
	size_t i;
	int ret = TRANSACTION_GENERIC_ERROR;

	/*
	 * Note that we *don't* skip transactions with zero updates,
	 * because such a transaction might be executed for the side
	 * effect of ensuring that all of the references are peeled.
	 * If the caller wants to optimize away empty transactions, it
	 * should do so itself.
	 */

	data = xcalloc(1, sizeof(*data));
	string_list_init(&data->updates, 0);

	transaction->backend_data = data;

	/*
	 * Stick the updates in a string list by refname so that we
	 * can sort them:
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct string_list_item *item =
			string_list_append(&data->updates, update->refname);

		/* Store a pointer to update in item->util: */
		item->util = update;
	}
	string_list_sort(&data->updates);

	if (ref_update_reject_duplicates(&data->updates, err))
		goto failure;

	if (!is_lock_file_locked(&refs->lock)) {
		if (packed_refs_lock(ref_store, 0, err))
			goto failure;
		data->own_lock = 1;
	}

	if (write_with_updates(refs, &data->updates, err))
		goto failure;

	transaction->state = REF_TRANSACTION_PREPARED;
	return 0;

failure:
	packed_transaction_cleanup(refs, transaction);
	return ret;
}

static int packed_transaction_abort(struct ref_store *ref_store,
				    struct ref_transaction *transaction,
				    struct strbuf *err)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_abort");

	packed_transaction_cleanup(refs, transaction);
	return 0;
}

static int packed_transaction_finish(struct ref_store *ref_store,
				     struct ref_transaction *transaction,
				     struct strbuf *err)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_finish");
	int ret = TRANSACTION_GENERIC_ERROR;
	char *packed_refs_path;

	packed_refs_path = get_locked_file_path(&refs->lock);
	if (rename_tempfile(&refs->tempfile, packed_refs_path)) {
		strbuf_addf(err, "error replacing %s: %s",
			    refs->path, strerror(errno));
		goto cleanup;
	}

	clear_packed_ref_cache(refs);
	ret = 0;

cleanup:
	free(packed_refs_path);
	packed_transaction_cleanup(refs, transaction);
	return ret;
}

static int packed_initial_transaction_commit(struct ref_store *ref_store,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	return ref_transaction_commit(transaction, err);
}

static int packed_delete_refs(struct ref_store *ref_store, const char *msg,
			     struct string_list *refnames, unsigned int flags)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_WRITE, "delete_refs");
	struct strbuf err = STRBUF_INIT;
	struct ref_transaction *transaction;
	struct string_list_item *item;
	int ret;

	(void)refs; /* We need the check above, but don't use the variable */

	if (!refnames->nr)
		return 0;

	/*
	 * Since we don't check the references' old_oids, the
	 * individual updates can't fail, so we can pack all of the
	 * updates into a single transaction.
	 */

	transaction = ref_store_transaction_begin(ref_store, &err);
	if (!transaction)
		return -1;

	for_each_string_list_item(item, refnames) {
		if (ref_transaction_delete(transaction, item->string, NULL,
					   flags, msg, &err)) {
			warning(_("could not delete reference %s: %s"),
				item->string, err.buf);
			strbuf_reset(&err);
		}
	}

	ret = ref_transaction_commit(transaction, &err);

	if (ret) {
		if (refnames->nr == 1)
			error(_("could not delete reference %s: %s"),
			      refnames->items[0].string, err.buf);
		else
			error(_("could not delete references: %s"), err.buf);
	}

	ref_transaction_free(transaction);
	strbuf_release(&err);
	return ret;
}

static int packed_pack_refs(struct ref_store *ref_store, unsigned int flags)
{
	/*
	 * Packed refs are already packed. It might be that loose refs
	 * are packed *into* a packed refs store, but that is done by
	 * updating the packed references via a transaction.
	 */
	return 0;
}

static int packed_create_symref(struct ref_store *ref_store,
			       const char *refname, const char *target,
			       const char *logmsg)
{
	die("BUG: packed reference store does not support symrefs");
}

static int packed_rename_ref(struct ref_store *ref_store,
			    const char *oldrefname, const char *newrefname,
			    const char *logmsg)
{
	die("BUG: packed reference store does not support renaming references");
}

static struct ref_iterator *packed_reflog_iterator_begin(struct ref_store *ref_store)
{
	return empty_ref_iterator_begin();
}

static int packed_for_each_reflog_ent(struct ref_store *ref_store,
				      const char *refname,
				      each_reflog_ent_fn fn, void *cb_data)
{
	return 0;
}

static int packed_for_each_reflog_ent_reverse(struct ref_store *ref_store,
					      const char *refname,
					      each_reflog_ent_fn fn,
					      void *cb_data)
{
	return 0;
}

static int packed_reflog_exists(struct ref_store *ref_store,
			       const char *refname)
{
	return 0;
}

static int packed_create_reflog(struct ref_store *ref_store,
			       const char *refname, int force_create,
			       struct strbuf *err)
{
	die("BUG: packed reference store does not support reflogs");
}

static int packed_delete_reflog(struct ref_store *ref_store,
			       const char *refname)
{
	return 0;
}

static int packed_reflog_expire(struct ref_store *ref_store,
				const char *refname, const unsigned char *sha1,
				unsigned int flags,
				reflog_expiry_prepare_fn prepare_fn,
				reflog_expiry_should_prune_fn should_prune_fn,
				reflog_expiry_cleanup_fn cleanup_fn,
				void *policy_cb_data)
{
	return 0;
}

struct ref_storage_be refs_be_packed = {
	NULL,
	"packed",
	packed_ref_store_create,
	packed_init_db,
	packed_transaction_prepare,
	packed_transaction_finish,
	packed_transaction_abort,
	packed_initial_transaction_commit,

	packed_pack_refs,
	packed_peel_ref,
	packed_create_symref,
	packed_delete_refs,
	packed_rename_ref,

	packed_ref_iterator_begin,
	packed_read_raw_ref,

	packed_reflog_iterator_begin,
	packed_for_each_reflog_ent,
	packed_for_each_reflog_ent_reverse,
	packed_reflog_exists,
	packed_create_reflog,
	packed_delete_reflog,
	packed_reflog_expire
};
