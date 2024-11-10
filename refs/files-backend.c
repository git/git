#define USE_THE_REPOSITORY_VARIABLE

#include "../git-compat-util.h"
#include "../config.h"
#include "../copy.h"
#include "../environment.h"
#include "../gettext.h"
#include "../hash.h"
#include "../hex.h"
#include "../fsck.h"
#include "../refs.h"
#include "../repo-settings.h"
#include "refs-internal.h"
#include "ref-cache.h"
#include "packed-backend.h"
#include "../ident.h"
#include "../iterator.h"
#include "../dir-iterator.h"
#include "../lockfile.h"
#include "../object.h"
#include "../object-file.h"
#include "../path.h"
#include "../dir.h"
#include "../chdir-notify.h"
#include "../setup.h"
#include "../wrapper.h"
#include "../write-or-die.h"
#include "../revision.h"
#include <wildmatch.h>

/*
 * This backend uses the following flags in `ref_update::flags` for
 * internal bookkeeping purposes. Their numerical values must not
 * conflict with REF_NO_DEREF, REF_FORCE_CREATE_REFLOG, REF_HAVE_NEW,
 * or REF_HAVE_OLD, which are also stored in `ref_update::flags`.
 */

/*
 * Used as a flag in ref_update::flags when a loose ref is being
 * pruned. This flag must only be used when REF_NO_DEREF is set.
 */
#define REF_IS_PRUNING (1 << 4)

/*
 * Flag passed to lock_ref_sha1_basic() telling it to tolerate broken
 * refs (i.e., because the reference is about to be deleted anyway).
 */
#define REF_DELETING (1 << 5)

/*
 * Used as a flag in ref_update::flags when the lockfile needs to be
 * committed.
 */
#define REF_NEEDS_COMMIT (1 << 6)

/*
 * Used as a flag in ref_update::flags when the ref_update was via an
 * update to HEAD.
 */
#define REF_UPDATE_VIA_HEAD (1 << 8)

/*
 * Used as a flag in ref_update::flags when a reference has been
 * deleted and the ref's parent directories may need cleanup.
 */
#define REF_DELETED_RMDIR (1 << 9)

struct ref_lock {
	char *ref_name;
	struct lock_file lk;
	struct object_id old_oid;
};

struct files_ref_store {
	struct ref_store base;
	unsigned int store_flags;

	char *gitcommondir;
	enum log_refs_config log_all_ref_updates;
	int prefer_symlink_refs;

	struct ref_cache *loose;

	struct ref_store *packed_ref_store;
};

static void clear_loose_ref_cache(struct files_ref_store *refs)
{
	if (refs->loose) {
		free_ref_cache(refs->loose);
		refs->loose = NULL;
	}
}

/*
 * Create a new submodule ref cache and add it to the internal
 * set of caches.
 */
static struct ref_store *files_ref_store_init(struct repository *repo,
					      const char *gitdir,
					      unsigned int flags)
{
	struct files_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct ref_store *ref_store = (struct ref_store *)refs;
	struct strbuf sb = STRBUF_INIT;

	base_ref_store_init(ref_store, repo, gitdir, &refs_be_files);
	refs->store_flags = flags;
	get_common_dir_noenv(&sb, gitdir);
	refs->gitcommondir = strbuf_detach(&sb, NULL);
	refs->packed_ref_store =
		packed_ref_store_init(repo, refs->gitcommondir, flags);
	refs->log_all_ref_updates = repo_settings_get_log_all_ref_updates(repo);
	repo_config_get_bool(repo, "core.prefersymlinkrefs", &refs->prefer_symlink_refs);

	chdir_notify_reparent("files-backend $GIT_DIR", &refs->base.gitdir);
	chdir_notify_reparent("files-backend $GIT_COMMONDIR",
			      &refs->gitcommondir);

	return ref_store;
}

/*
 * Die if refs is not the main ref store. caller is used in any
 * necessary error messages.
 */
static void files_assert_main_repository(struct files_ref_store *refs,
					 const char *caller)
{
	if (refs->store_flags & REF_STORE_MAIN)
		return;

	BUG("operation %s only allowed for main ref store", caller);
}

/*
 * Downcast ref_store to files_ref_store. Die if ref_store is not a
 * files_ref_store. required_flags is compared with ref_store's
 * store_flags to ensure the ref_store has all required capabilities.
 * "caller" is used in any necessary error messages.
 */
static struct files_ref_store *files_downcast(struct ref_store *ref_store,
					      unsigned int required_flags,
					      const char *caller)
{
	struct files_ref_store *refs;

	if (ref_store->be != &refs_be_files)
		BUG("ref_store is type \"%s\" not \"files\" in %s",
		    ref_store->be->name, caller);

	refs = (struct files_ref_store *)ref_store;

	if ((refs->store_flags & required_flags) != required_flags)
		BUG("operation %s requires abilities 0x%x, but only have 0x%x",
		    caller, required_flags, refs->store_flags);

	return refs;
}

static void files_ref_store_release(struct ref_store *ref_store)
{
	struct files_ref_store *refs = files_downcast(ref_store, 0, "release");
	free_ref_cache(refs->loose);
	free(refs->gitcommondir);
	ref_store_release(refs->packed_ref_store);
	free(refs->packed_ref_store);
}

static void files_reflog_path(struct files_ref_store *refs,
			      struct strbuf *sb,
			      const char *refname)
{
	const char *bare_refname;
	const char *wtname;
	int wtname_len;
	enum ref_worktree_type wt_type = parse_worktree_ref(
		refname, &wtname, &wtname_len, &bare_refname);

	switch (wt_type) {
	case REF_WORKTREE_CURRENT:
		strbuf_addf(sb, "%s/logs/%s", refs->base.gitdir, refname);
		break;
	case REF_WORKTREE_SHARED:
	case REF_WORKTREE_MAIN:
		strbuf_addf(sb, "%s/logs/%s", refs->gitcommondir, bare_refname);
		break;
	case REF_WORKTREE_OTHER:
		strbuf_addf(sb, "%s/worktrees/%.*s/logs/%s", refs->gitcommondir,
			    wtname_len, wtname, bare_refname);
		break;
	default:
		BUG("unknown ref type %d of ref %s", wt_type, refname);
	}
}

static void files_ref_path(struct files_ref_store *refs,
			   struct strbuf *sb,
			   const char *refname)
{
	const char *bare_refname;
	const char *wtname;
	int wtname_len;
	enum ref_worktree_type wt_type = parse_worktree_ref(
		refname, &wtname, &wtname_len, &bare_refname);
	switch (wt_type) {
	case REF_WORKTREE_CURRENT:
		strbuf_addf(sb, "%s/%s", refs->base.gitdir, refname);
		break;
	case REF_WORKTREE_OTHER:
		strbuf_addf(sb, "%s/worktrees/%.*s/%s", refs->gitcommondir,
			    wtname_len, wtname, bare_refname);
		break;
	case REF_WORKTREE_SHARED:
	case REF_WORKTREE_MAIN:
		strbuf_addf(sb, "%s/%s", refs->gitcommondir, bare_refname);
		break;
	default:
		BUG("unknown ref type %d of ref %s", wt_type, refname);
	}
}

/*
 * Manually add refs/bisect, refs/rewritten and refs/worktree, which, being
 * per-worktree, might not appear in the directory listing for
 * refs/ in the main repo.
 */
static void add_per_worktree_entries_to_dir(struct ref_dir *dir, const char *dirname)
{
	const char *prefixes[] = { "refs/bisect/", "refs/worktree/", "refs/rewritten/" };
	int ip;

	if (strcmp(dirname, "refs/"))
		return;

	for (ip = 0; ip < ARRAY_SIZE(prefixes); ip++) {
		const char *prefix = prefixes[ip];
		int prefix_len = strlen(prefix);
		struct ref_entry *child_entry;
		int pos;

		pos = search_ref_dir(dir, prefix, prefix_len);
		if (pos >= 0)
			continue;
		child_entry = create_dir_entry(dir->cache, prefix, prefix_len);
		add_entry_to_dir(dir, child_entry);
	}
}

static void loose_fill_ref_dir_regular_file(struct files_ref_store *refs,
					    const char *refname,
					    struct ref_dir *dir)
{
	struct object_id oid;
	int flag;
	const char *referent = refs_resolve_ref_unsafe(&refs->base,
						       refname,
						       RESOLVE_REF_READING,
						       &oid, &flag);

	if (!referent) {
		oidclr(&oid, refs->base.repo->hash_algo);
		flag |= REF_ISBROKEN;
	} else if (is_null_oid(&oid)) {
		/*
		 * It is so astronomically unlikely
		 * that null_oid is the OID of an
		 * actual object that we consider its
		 * appearance in a loose reference
		 * file to be repo corruption
		 * (probably due to a software bug).
		 */
		flag |= REF_ISBROKEN;
	}

	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!refname_is_safe(refname))
			die("loose refname is dangerous: %s", refname);
		oidclr(&oid, refs->base.repo->hash_algo);
		flag |= REF_BAD_NAME | REF_ISBROKEN;
	}

	if (!(flag & REF_ISSYMREF))
		referent = NULL;

	add_entry_to_dir(dir, create_ref_entry(refname, referent, &oid, flag));
}

/*
 * Read the loose references from the namespace dirname into dir
 * (without recursing).  dirname must end with '/'.  dir must be the
 * directory entry corresponding to dirname.
 */
static void loose_fill_ref_dir(struct ref_store *ref_store,
			       struct ref_dir *dir, const char *dirname)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ, "fill_ref_dir");
	DIR *d;
	struct dirent *de;
	int dirnamelen = strlen(dirname);
	struct strbuf refname;
	struct strbuf path = STRBUF_INIT;

	files_ref_path(refs, &path, dirname);

	d = opendir(path.buf);
	if (!d) {
		strbuf_release(&path);
		return;
	}

	strbuf_init(&refname, dirnamelen + 257);
	strbuf_add(&refname, dirname, dirnamelen);

	while ((de = readdir(d)) != NULL) {
		unsigned char dtype;

		if (de->d_name[0] == '.')
			continue;
		if (ends_with(de->d_name, ".lock"))
			continue;
		strbuf_addstr(&refname, de->d_name);

		dtype = get_dtype(de, &path, 1);
		if (dtype == DT_DIR) {
			strbuf_addch(&refname, '/');
			add_entry_to_dir(dir,
					 create_dir_entry(dir->cache, refname.buf,
							  refname.len));
		} else if (dtype == DT_REG) {
			loose_fill_ref_dir_regular_file(refs, refname.buf, dir);
		}
		strbuf_setlen(&refname, dirnamelen);
	}
	strbuf_release(&refname);
	strbuf_release(&path);
	closedir(d);

	add_per_worktree_entries_to_dir(dir, dirname);
}

static int for_each_root_ref(struct files_ref_store *refs,
			     int (*cb)(const char *refname, void *cb_data),
			     void *cb_data)
{
	struct strbuf path = STRBUF_INIT, refname = STRBUF_INIT;
	const char *dirname = refs->loose->root->name;
	struct dirent *de;
	size_t dirnamelen;
	int ret;
	DIR *d;

	files_ref_path(refs, &path, dirname);

	d = opendir(path.buf);
	if (!d) {
		strbuf_release(&path);
		return -1;
	}

	strbuf_addstr(&refname, dirname);
	dirnamelen = refname.len;

	while ((de = readdir(d)) != NULL) {
		unsigned char dtype;

		if (de->d_name[0] == '.')
			continue;
		if (ends_with(de->d_name, ".lock"))
			continue;
		strbuf_addstr(&refname, de->d_name);

		dtype = get_dtype(de, &path, 1);
		if (dtype == DT_REG && is_root_ref(de->d_name)) {
			ret = cb(refname.buf, cb_data);
			if (ret)
				goto done;
		}

		strbuf_setlen(&refname, dirnamelen);
	}

	ret = 0;

done:
	strbuf_release(&refname);
	strbuf_release(&path);
	closedir(d);
	return ret;
}

struct fill_root_ref_data {
	struct files_ref_store *refs;
	struct ref_dir *dir;
};

static int fill_root_ref(const char *refname, void *cb_data)
{
	struct fill_root_ref_data *data = cb_data;
	loose_fill_ref_dir_regular_file(data->refs, refname, data->dir);
	return 0;
}

/*
 * Add root refs to the ref dir by parsing the directory for any files which
 * follow the root ref syntax.
 */
static void add_root_refs(struct files_ref_store *refs,
			  struct ref_dir *dir)
{
	struct fill_root_ref_data data = {
		.refs = refs,
		.dir = dir,
	};

	for_each_root_ref(refs, fill_root_ref, &data);
}

static struct ref_cache *get_loose_ref_cache(struct files_ref_store *refs,
					     unsigned int flags)
{
	if (!refs->loose) {
		struct ref_dir *dir;

		/*
		 * Mark the top-level directory complete because we
		 * are about to read the only subdirectory that can
		 * hold references:
		 */
		refs->loose = create_ref_cache(&refs->base, loose_fill_ref_dir);

		/* We're going to fill the top level ourselves: */
		refs->loose->root->flag &= ~REF_INCOMPLETE;

		dir = get_ref_dir(refs->loose->root);

		if (flags & DO_FOR_EACH_INCLUDE_ROOT_REFS)
			add_root_refs(refs, dir);

		/*
		 * Add an incomplete entry for "refs/" (to be filled
		 * lazily):
		 */
		add_entry_to_dir(dir, create_dir_entry(refs->loose, "refs/", 5));
	}
	return refs->loose;
}

static int read_ref_internal(struct ref_store *ref_store, const char *refname,
			     struct object_id *oid, struct strbuf *referent,
			     unsigned int *type, int *failure_errno, int skip_packed_refs)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ, "read_raw_ref");
	struct strbuf sb_contents = STRBUF_INIT;
	struct strbuf sb_path = STRBUF_INIT;
	const char *path;
	const char *buf;
	struct stat st;
	int fd;
	int ret = -1;
	int remaining_retries = 3;
	int myerr = 0;

	*type = 0;
	strbuf_reset(&sb_path);

	files_ref_path(refs, &sb_path, refname);

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
		int ignore_errno;
		myerr = errno;
		if (myerr != ENOENT || skip_packed_refs)
			goto out;
		if (refs_read_raw_ref(refs->packed_ref_store, refname, oid,
				      referent, type, &ignore_errno)) {
			myerr = ENOENT;
			goto out;
		}
		ret = 0;
		goto out;
	}

	/* Follow "normalized" - ie "refs/.." symlinks by hand */
	if (S_ISLNK(st.st_mode)) {
		strbuf_reset(&sb_contents);
		if (strbuf_readlink(&sb_contents, path, st.st_size) < 0) {
			myerr = errno;
			if (myerr == ENOENT || myerr == EINVAL)
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
		int ignore_errno;
		/*
		 * Even though there is a directory where the loose
		 * ref is supposed to be, there could still be a
		 * packed ref:
		 */
		if (skip_packed_refs ||
		    refs_read_raw_ref(refs->packed_ref_store, refname, oid,
				      referent, type, &ignore_errno)) {
			myerr = EISDIR;
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
		myerr = errno;
		if (myerr == ENOENT && !S_ISLNK(st.st_mode))
			/* inconsistent with lstat; retry */
			goto stat_ref;
		else
			goto out;
	}
	strbuf_reset(&sb_contents);
	if (strbuf_read(&sb_contents, fd, 256) < 0) {
		myerr = errno;
		close(fd);
		goto out;
	}
	close(fd);
	strbuf_rtrim(&sb_contents);
	buf = sb_contents.buf;

	ret = parse_loose_ref_contents(ref_store->repo->hash_algo, buf,
				       oid, referent, type, &myerr);

out:
	if (ret && !myerr)
		BUG("returning non-zero %d, should have set myerr!", ret);
	*failure_errno = myerr;

	strbuf_release(&sb_path);
	strbuf_release(&sb_contents);
	errno = 0;
	return ret;
}

static int files_read_raw_ref(struct ref_store *ref_store, const char *refname,
			      struct object_id *oid, struct strbuf *referent,
			      unsigned int *type, int *failure_errno)
{
	return read_ref_internal(ref_store, refname, oid, referent, type, failure_errno, 0);
}

static int files_read_symbolic_ref(struct ref_store *ref_store, const char *refname,
				   struct strbuf *referent)
{
	struct object_id oid;
	int failure_errno, ret;
	unsigned int type;

	ret = read_ref_internal(ref_store, refname, &oid, referent, &type, &failure_errno, 1);
	if (ret)
		return ret;

	return !(type & REF_ISSYMREF);
}

int parse_loose_ref_contents(const struct git_hash_algo *algop,
			     const char *buf, struct object_id *oid,
			     struct strbuf *referent, unsigned int *type,
			     int *failure_errno)
{
	const char *p;
	if (skip_prefix(buf, "ref:", &buf)) {
		while (isspace(*buf))
			buf++;

		strbuf_reset(referent);
		strbuf_addstr(referent, buf);
		*type |= REF_ISSYMREF;
		return 0;
	}

	/*
	 * FETCH_HEAD has additional data after the sha.
	 */
	if (parse_oid_hex_algop(buf, oid, &p, algop) ||
	    (*p != '\0' && !isspace(*p))) {
		*type |= REF_ISBROKEN;
		*failure_errno = EINVAL;
		return -1;
	}
	return 0;
}

static void unlock_ref(struct ref_lock *lock)
{
	rollback_lock_file(&lock->lk);
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
 * are passed to refs_verify_refname_available() for this check.
 *
 * If mustexist is not set and the reference is not found or is
 * broken, lock the reference anyway but clear old_oid.
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
 * - Avoid calling refs_verify_refname_available() when it can be
 *   avoided, namely if we were successfully able to read the ref
 * - Generate informative error messages in the case of failure
 */
static int lock_raw_ref(struct files_ref_store *refs,
			const char *refname, int mustexist,
			const struct string_list *extras,
			struct ref_lock **lock_p,
			struct strbuf *referent,
			unsigned int *type,
			struct strbuf *err)
{
	struct ref_lock *lock;
	struct strbuf ref_file = STRBUF_INIT;
	int attempts_remaining = 3;
	int ret = TRANSACTION_GENERIC_ERROR;
	int failure_errno;

	assert(err);
	files_assert_main_repository(refs, "lock_raw_ref");

	*type = 0;

	/* First lock the file so it can't change out from under us. */

	*lock_p = CALLOC_ARRAY(lock, 1);

	lock->ref_name = xstrdup(refname);
	files_ref_path(refs, &ref_file, refname);

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
		if (refs_verify_refname_available(&refs->base, refname,
						  extras, NULL, err)) {
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
				 * refs_verify_refname_available() is
				 * OK.
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

	if (hold_lock_file_for_update_timeout(
			    &lock->lk, ref_file.buf, LOCK_NO_DEREF,
			    get_files_ref_lock_timeout_ms()) < 0) {
		int myerr = errno;
		errno = 0;
		if (myerr == ENOENT && --attempts_remaining > 0) {
			/*
			 * Maybe somebody just deleted one of the
			 * directories leading to ref_file.  Try
			 * again:
			 */
			goto retry;
		} else {
			unable_to_lock_message(ref_file.buf, myerr, err);
			goto error_return;
		}
	}

	/*
	 * Now we hold the lock and can read the reference without
	 * fear that its value will change.
	 */

	if (files_read_raw_ref(&refs->base, refname, &lock->old_oid, referent,
			       type, &failure_errno)) {
		if (failure_errno == ENOENT) {
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
		} else if (failure_errno == EISDIR) {
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
				if (refs_verify_refname_available(
						    &refs->base, refname,
						    extras, NULL, err)) {
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
		} else if (failure_errno == EINVAL && (*type & REF_ISBROKEN)) {
			strbuf_addf(err, "unable to resolve reference '%s': "
				    "reference broken", refname);
			goto error_return;
		} else {
			strbuf_addf(err, "unable to resolve reference '%s': %s",
				    refname, strerror(failure_errno));
			goto error_return;
		}

		/*
		 * If the ref did not exist and we are creating it,
		 * make sure there is no existing packed ref that
		 * conflicts with refname:
		 */
		if (refs_verify_refname_available(
				    refs->packed_ref_store, refname,
				    extras, NULL, err)) {
			ret = TRANSACTION_NAME_CONFLICT;
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

struct files_ref_iterator {
	struct ref_iterator base;

	struct ref_iterator *iter0;
	struct repository *repo;
	unsigned int flags;
};

static int files_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct files_ref_iterator *iter =
		(struct files_ref_iterator *)ref_iterator;
	int ok;

	while ((ok = ref_iterator_advance(iter->iter0)) == ITER_OK) {
		if (iter->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    parse_worktree_ref(iter->iter0->refname, NULL, NULL,
				       NULL) != REF_WORKTREE_CURRENT)
			continue;

		if ((iter->flags & DO_FOR_EACH_OMIT_DANGLING_SYMREFS) &&
		    (iter->iter0->flags & REF_ISSYMREF) &&
		    (iter->iter0->flags & REF_ISBROKEN))
			continue;

		if (!(iter->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(iter->iter0->refname,
					    iter->repo,
					    iter->iter0->oid,
					    iter->iter0->flags))
			continue;

		iter->base.refname = iter->iter0->refname;
		iter->base.oid = iter->iter0->oid;
		iter->base.flags = iter->iter0->flags;
		iter->base.referent = iter->iter0->referent;

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

	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable files_ref_iterator_vtable = {
	.advance = files_ref_iterator_advance,
	.peel = files_ref_iterator_peel,
	.abort = files_ref_iterator_abort,
};

static struct ref_iterator *files_ref_iterator_begin(
		struct ref_store *ref_store,
		const char *prefix, const char **exclude_patterns,
		unsigned int flags)
{
	struct files_ref_store *refs;
	struct ref_iterator *loose_iter, *packed_iter, *overlay_iter;
	struct files_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	unsigned int required_flags = REF_STORE_READ;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN))
		required_flags |= REF_STORE_ODB;

	refs = files_downcast(ref_store, required_flags, "ref_iterator_begin");

	/*
	 * We must make sure that all loose refs are read before
	 * accessing the packed-refs file; this avoids a race
	 * condition if loose refs are migrated to the packed-refs
	 * file by a simultaneous process, but our in-memory view is
	 * from before the migration. We ensure this as follows:
	 * First, we call start the loose refs iteration with its
	 * `prime_ref` argument set to true. This causes the loose
	 * references in the subtree to be pre-read into the cache.
	 * (If they've already been read, that's OK; we only need to
	 * guarantee that they're read before the packed refs, not
	 * *how much* before.) After that, we call
	 * packed_ref_iterator_begin(), which internally checks
	 * whether the packed-ref cache is up to date with what is on
	 * disk, and re-reads it if not.
	 */

	loose_iter = cache_ref_iterator_begin(get_loose_ref_cache(refs, flags),
					      prefix, ref_store->repo, 1);

	/*
	 * The packed-refs file might contain broken references, for
	 * example an old version of a reference that points at an
	 * object that has since been garbage-collected. This is OK as
	 * long as there is a corresponding loose reference that
	 * overrides it, and we don't want to emit an error message in
	 * this case. So ask the packed_ref_store for all of its
	 * references, and (if needed) do our own check for broken
	 * ones in files_ref_iterator_advance(), after we have merged
	 * the packed and loose references.
	 */
	packed_iter = refs_ref_iterator_begin(
			refs->packed_ref_store, prefix, exclude_patterns, 0,
			DO_FOR_EACH_INCLUDE_BROKEN);

	overlay_iter = overlay_ref_iterator_begin(loose_iter, packed_iter);

	CALLOC_ARRAY(iter, 1);
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &files_ref_iterator_vtable);
	iter->iter0 = overlay_iter;
	iter->repo = ref_store->repo;
	iter->flags = flags;

	return ref_iterator;
}

/*
 * Callback function for raceproof_create_file(). This function is
 * expected to do something that makes dirname(path) permanent despite
 * the fact that other processes might be cleaning up empty
 * directories at the same time. Usually it will create a file named
 * path, but alternatively it could create another file in that
 * directory, or even chdir() into that directory. The function should
 * return 0 if the action was completed successfully. On error, it
 * should return a nonzero result and set errno.
 * raceproof_create_file() treats two errno values specially:
 *
 * - ENOENT -- dirname(path) does not exist. In this case,
 *             raceproof_create_file() tries creating dirname(path)
 *             (and any parent directories, if necessary) and calls
 *             the function again.
 *
 * - EISDIR -- the file already exists and is a directory. In this
 *             case, raceproof_create_file() removes the directory if
 *             it is empty (and recursively any empty directories that
 *             it contains) and calls the function again.
 *
 * Any other errno causes raceproof_create_file() to fail with the
 * callback's return value and errno.
 *
 * Obviously, this function should be OK with being called again if it
 * fails with ENOENT or EISDIR. In other scenarios it will not be
 * called again.
 */
typedef int create_file_fn(const char *path, void *cb);

/*
 * Create a file in dirname(path) by calling fn, creating leading
 * directories if necessary. Retry a few times in case we are racing
 * with another process that is trying to clean up the directory that
 * contains path. See the documentation for create_file_fn for more
 * details.
 *
 * Return the value and set the errno that resulted from the most
 * recent call of fn. fn is always called at least once, and will be
 * called more than once if it returns ENOENT or EISDIR.
 */
static int raceproof_create_file(const char *path, create_file_fn fn, void *cb)
{
	/*
	 * The number of times we will try to remove empty directories
	 * in the way of path. This is only 1 because if another
	 * process is racily creating directories that conflict with
	 * us, we don't want to fight against them.
	 */
	int remove_directories_remaining = 1;

	/*
	 * The number of times that we will try to create the
	 * directories containing path. We are willing to attempt this
	 * more than once, because another process could be trying to
	 * clean up empty directories at the same time as we are
	 * trying to create them.
	 */
	int create_directories_remaining = 3;

	/* A scratch copy of path, filled lazily if we need it: */
	struct strbuf path_copy = STRBUF_INIT;

	int ret, save_errno;

	/* Sanity check: */
	assert(*path);

retry_fn:
	ret = fn(path, cb);
	save_errno = errno;
	if (!ret)
		goto out;

	if (errno == EISDIR && remove_directories_remaining-- > 0) {
		/*
		 * A directory is in the way. Maybe it is empty; try
		 * to remove it:
		 */
		if (!path_copy.len)
			strbuf_addstr(&path_copy, path);

		if (!remove_dir_recursively(&path_copy, REMOVE_DIR_EMPTY_ONLY))
			goto retry_fn;
	} else if (errno == ENOENT && create_directories_remaining-- > 0) {
		/*
		 * Maybe the containing directory didn't exist, or
		 * maybe it was just deleted by a process that is
		 * racing with us to clean up empty directories. Try
		 * to create it:
		 */
		enum scld_error scld_result;

		if (!path_copy.len)
			strbuf_addstr(&path_copy, path);

		do {
			scld_result = safe_create_leading_directories(path_copy.buf);
			if (scld_result == SCLD_OK)
				goto retry_fn;
		} while (scld_result == SCLD_VANISHED && create_directories_remaining-- > 0);
	}

out:
	strbuf_release(&path_copy);
	errno = save_errno;
	return ret;
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

	return hold_lock_file_for_update_timeout(
			lk, path, LOCK_NO_DEREF,
			get_files_ref_lock_timeout_ms()) < 0 ? -1 : 0;
}

/*
 * Locks a ref returning the lock on success and NULL on failure.
 */
static struct ref_lock *lock_ref_oid_basic(struct files_ref_store *refs,
					   const char *refname,
					   struct strbuf *err)
{
	struct strbuf ref_file = STRBUF_INIT;
	struct ref_lock *lock;

	files_assert_main_repository(refs, "lock_ref_oid_basic");
	assert(err);

	CALLOC_ARRAY(lock, 1);

	files_ref_path(refs, &ref_file, refname);

	/*
	 * If the ref did not exist and we are creating it, make sure
	 * there is no existing packed ref whose name begins with our
	 * refname, nor a packed ref whose name is a proper prefix of
	 * our refname.
	 */
	if (is_null_oid(&lock->old_oid) &&
	    refs_verify_refname_available(refs->packed_ref_store, refname,
					  NULL, NULL, err))
		goto error_return;

	lock->ref_name = xstrdup(refname);

	if (raceproof_create_file(ref_file.buf, create_reflock, &lock->lk)) {
		unable_to_lock_message(ref_file.buf, errno, err);
		goto error_return;
	}

	if (!refs_resolve_ref_unsafe(&refs->base, lock->ref_name, 0,
				     &lock->old_oid, NULL))
		oidclr(&lock->old_oid, refs->base.repo->hash_algo);
	goto out;

 error_return:
	unlock_ref(lock);
	lock = NULL;

 out:
	strbuf_release(&ref_file);
	return lock;
}

struct ref_to_prune {
	struct ref_to_prune *next;
	struct object_id oid;
	char name[FLEX_ARRAY];
};

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
static void try_remove_empty_parents(struct files_ref_store *refs,
				     const char *refname,
				     unsigned int flags)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;
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

		strbuf_reset(&sb);
		files_ref_path(refs, &sb, buf.buf);
		if ((flags & REMOVE_EMPTY_PARENTS_REF) && rmdir(sb.buf))
			flags &= ~REMOVE_EMPTY_PARENTS_REF;

		strbuf_reset(&sb);
		files_reflog_path(refs, &sb, buf.buf);
		if ((flags & REMOVE_EMPTY_PARENTS_REFLOG) && rmdir(sb.buf))
			flags &= ~REMOVE_EMPTY_PARENTS_REFLOG;
	}
	strbuf_release(&buf);
	strbuf_release(&sb);
}

/* make sure nobody touched the ref, and unlink */
static void prune_ref(struct files_ref_store *refs, struct ref_to_prune *r)
{
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	int ret = -1;

	if (check_refname_format(r->name, 0))
		return;

	transaction = ref_store_transaction_begin(&refs->base, &err);
	if (!transaction)
		goto cleanup;
	ref_transaction_add_update(
			transaction, r->name,
			REF_NO_DEREF | REF_HAVE_NEW | REF_HAVE_OLD | REF_IS_PRUNING,
			null_oid(), &r->oid, NULL, NULL, NULL);
	if (ref_transaction_commit(transaction, &err))
		goto cleanup;

	ret = 0;

cleanup:
	if (ret)
		error("%s", err.buf);
	strbuf_release(&err);
	ref_transaction_free(transaction);
	return;
}

/*
 * Prune the loose versions of the references in the linked list
 * `*refs_to_prune`, freeing the entries in the list as we go.
 */
static void prune_refs(struct files_ref_store *refs, struct ref_to_prune **refs_to_prune)
{
	while (*refs_to_prune) {
		struct ref_to_prune *r = *refs_to_prune;
		*refs_to_prune = r->next;
		prune_ref(refs, r);
		free(r);
	}
}

/*
 * Return true if the specified reference should be packed.
 */
static int should_pack_ref(struct files_ref_store *refs,
			   const char *refname,
			   const struct object_id *oid, unsigned int ref_flags,
			   struct pack_refs_opts *opts)
{
	struct string_list_item *item;

	/* Do not pack per-worktree refs: */
	if (parse_worktree_ref(refname, NULL, NULL, NULL) !=
	    REF_WORKTREE_SHARED)
		return 0;

	/* Do not pack symbolic refs: */
	if (ref_flags & REF_ISSYMREF)
		return 0;

	/* Do not pack broken refs: */
	if (!ref_resolves_to_object(refname, refs->base.repo, oid, ref_flags))
		return 0;

	if (ref_excluded(opts->exclusions, refname))
		return 0;

	for_each_string_list_item(item, opts->includes)
		if (!wildmatch(item->string, refname, 0))
			return 1;

	return 0;
}

static int should_pack_refs(struct files_ref_store *refs,
			    struct pack_refs_opts *opts)
{
	struct ref_iterator *iter;
	size_t packed_size;
	size_t refcount = 0;
	size_t limit;
	int ret;

	if (!(opts->flags & PACK_REFS_AUTO))
		return 1;

	ret = packed_refs_size(refs->packed_ref_store, &packed_size);
	if (ret < 0)
		die("cannot determine packed-refs size");

	/*
	 * Packing loose references into the packed-refs file scales with the
	 * number of references we're about to write. We thus decide whether we
	 * repack refs by weighing the current size of the packed-refs file
	 * against the number of loose references. This is done such that we do
	 * not repack too often on repositories with a huge number of
	 * references, where we can expect a lot of churn in the number of
	 * references.
	 *
	 * As a heuristic, we repack if the number of loose references in the
	 * repository exceeds `log2(nr_packed_refs) * 5`, where we estimate
	 * `nr_packed_refs = packed_size / 100`, which scales as following:
	 *
	 * - 1kB ~ 10 packed refs: 16 refs
	 * - 10kB ~ 100 packed refs: 33 refs
	 * - 100kB ~ 1k packed refs: 49 refs
	 * - 1MB ~ 10k packed refs: 66 refs
	 * - 10MB ~ 100k packed refs: 82 refs
	 * - 100MB ~ 1m packed refs: 99 refs
	 *
	 * We thus allow roughly 16 additional loose refs per factor of ten of
	 * packed refs. This heuristic may be tweaked in the future, but should
	 * serve as a sufficiently good first iteration.
	 */
	limit = log2u(packed_size / 100) * 5;
	if (limit < 16)
		limit = 16;

	iter = cache_ref_iterator_begin(get_loose_ref_cache(refs, 0), NULL,
					refs->base.repo, 0);
	while ((ret = ref_iterator_advance(iter)) == ITER_OK) {
		if (should_pack_ref(refs, iter->refname, iter->oid,
				    iter->flags, opts))
			refcount++;
		if (refcount >= limit) {
			ref_iterator_abort(iter);
			return 1;
		}
	}

	if (ret != ITER_DONE)
		die("error while iterating over references");

	return 0;
}

static int files_pack_refs(struct ref_store *ref_store,
			   struct pack_refs_opts *opts)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE | REF_STORE_ODB,
			       "pack_refs");
	struct ref_iterator *iter;
	int ok;
	struct ref_to_prune *refs_to_prune = NULL;
	struct strbuf err = STRBUF_INIT;
	struct ref_transaction *transaction;

	if (!should_pack_refs(refs, opts))
		return 0;

	transaction = ref_store_transaction_begin(refs->packed_ref_store, &err);
	if (!transaction)
		return -1;

	packed_refs_lock(refs->packed_ref_store, LOCK_DIE_ON_ERROR, &err);

	iter = cache_ref_iterator_begin(get_loose_ref_cache(refs, 0), NULL,
					refs->base.repo, 0);
	while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
		/*
		 * If the loose reference can be packed, add an entry
		 * in the packed ref cache. If the reference should be
		 * pruned, also add it to refs_to_prune.
		 */
		if (!should_pack_ref(refs, iter->refname, iter->oid, iter->flags, opts))
			continue;

		/*
		 * Add a reference creation for this reference to the
		 * packed-refs transaction:
		 */
		if (ref_transaction_update(transaction, iter->refname,
					   iter->oid, NULL, NULL, NULL,
					   REF_NO_DEREF, NULL, &err))
			die("failure preparing to create packed reference %s: %s",
			    iter->refname, err.buf);

		/* Schedule the loose reference for pruning if requested. */
		if ((opts->flags & PACK_REFS_PRUNE)) {
			struct ref_to_prune *n;
			FLEX_ALLOC_STR(n, name, iter->refname);
			oidcpy(&n->oid, iter->oid);
			n->next = refs_to_prune;
			refs_to_prune = n;
		}
	}
	if (ok != ITER_DONE)
		die("error while iterating over references");

	if (ref_transaction_commit(transaction, &err))
		die("unable to write new packed-refs: %s", err.buf);

	ref_transaction_free(transaction);

	packed_refs_unlock(refs->packed_ref_store);

	prune_refs(refs, &refs_to_prune);
	strbuf_release(&err);
	return 0;
}

/*
 * People using contrib's git-new-workdir have .git/logs/refs ->
 * /some/other/path/.git/logs/refs, and that may live on another device.
 *
 * IOW, to avoid cross device rename errors, the temporary renamed log must
 * live into logs/refs.
 */
#define TMP_RENAMED_LOG  "refs/.tmp-renamed-log"

struct rename_cb {
	const char *tmp_renamed_log;
	int true_errno;
};

static int rename_tmp_log_callback(const char *path, void *cb_data)
{
	struct rename_cb *cb = cb_data;

	if (rename(cb->tmp_renamed_log, path)) {
		/*
		 * rename(a, b) when b is an existing directory ought
		 * to result in ISDIR, but Solaris 5.8 gives ENOTDIR.
		 * Sheesh. Record the true errno for error reporting,
		 * but report EISDIR to raceproof_create_file() so
		 * that it knows to retry.
		 */
		cb->true_errno = errno;
		if (errno == ENOTDIR)
			errno = EISDIR;
		return -1;
	} else {
		return 0;
	}
}

static int rename_tmp_log(struct files_ref_store *refs, const char *newrefname)
{
	struct strbuf path = STRBUF_INIT;
	struct strbuf tmp = STRBUF_INIT;
	struct rename_cb cb;
	int ret;

	files_reflog_path(refs, &path, newrefname);
	files_reflog_path(refs, &tmp, TMP_RENAMED_LOG);
	cb.tmp_renamed_log = tmp.buf;
	ret = raceproof_create_file(path.buf, rename_tmp_log_callback, &cb);
	if (ret) {
		if (errno == EISDIR)
			error("directory not empty: %s", path.buf);
		else
			error("unable to move logfile %s to %s: %s",
			      tmp.buf, path.buf,
			      strerror(cb.true_errno));
	}

	strbuf_release(&path);
	strbuf_release(&tmp);
	return ret;
}

static int write_ref_to_lockfile(struct files_ref_store *refs,
				 struct ref_lock *lock,
				 const struct object_id *oid,
				 int skip_oid_verification, struct strbuf *err);
static int commit_ref_update(struct files_ref_store *refs,
			     struct ref_lock *lock,
			     const struct object_id *oid, const char *logmsg,
			     int flags,
			     struct strbuf *err);

/*
 * Emit a better error message than lockfile.c's
 * unable_to_lock_message() would in case there is a D/F conflict with
 * another existing reference. If there would be a conflict, emit an error
 * message and return false; otherwise, return true.
 *
 * Note that this function is not safe against all races with other
 * processes, and that's not its job. We'll emit a more verbose error on D/f
 * conflicts if we get past it into lock_ref_oid_basic().
 */
static int refs_rename_ref_available(struct ref_store *refs,
			      const char *old_refname,
			      const char *new_refname)
{
	struct string_list skip = STRING_LIST_INIT_NODUP;
	struct strbuf err = STRBUF_INIT;
	int ok;

	string_list_insert(&skip, old_refname);
	ok = !refs_verify_refname_available(refs, new_refname,
					    NULL, &skip, &err);
	if (!ok)
		error("%s", err.buf);

	string_list_clear(&skip, 0);
	strbuf_release(&err);
	return ok;
}

static int files_copy_or_rename_ref(struct ref_store *ref_store,
			    const char *oldrefname, const char *newrefname,
			    const char *logmsg, int copy)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "rename_ref");
	struct object_id orig_oid;
	int flag = 0, logmoved = 0;
	struct ref_lock *lock;
	struct stat loginfo;
	struct strbuf sb_oldref = STRBUF_INIT;
	struct strbuf sb_newref = STRBUF_INIT;
	struct strbuf tmp_renamed_log = STRBUF_INIT;
	int log, ret;
	struct strbuf err = STRBUF_INIT;

	files_reflog_path(refs, &sb_oldref, oldrefname);
	files_reflog_path(refs, &sb_newref, newrefname);
	files_reflog_path(refs, &tmp_renamed_log, TMP_RENAMED_LOG);

	log = !lstat(sb_oldref.buf, &loginfo);
	if (log && S_ISLNK(loginfo.st_mode)) {
		ret = error("reflog for %s is a symlink", oldrefname);
		goto out;
	}

	if (!refs_resolve_ref_unsafe(&refs->base, oldrefname,
				     RESOLVE_REF_READING | RESOLVE_REF_NO_RECURSE,
				     &orig_oid, &flag)) {
		ret = error("refname %s not found", oldrefname);
		goto out;
	}

	if (flag & REF_ISSYMREF) {
		if (copy)
			ret = error("refname %s is a symbolic ref, copying it is not supported",
				    oldrefname);
		else
			ret = error("refname %s is a symbolic ref, renaming it is not supported",
				    oldrefname);
		goto out;
	}
	if (!refs_rename_ref_available(&refs->base, oldrefname, newrefname)) {
		ret = 1;
		goto out;
	}

	if (!copy && log && rename(sb_oldref.buf, tmp_renamed_log.buf)) {
		ret = error("unable to move logfile logs/%s to logs/"TMP_RENAMED_LOG": %s",
			    oldrefname, strerror(errno));
		goto out;
	}

	if (copy && log && copy_file(tmp_renamed_log.buf, sb_oldref.buf, 0644)) {
		ret = error("unable to copy logfile logs/%s to logs/"TMP_RENAMED_LOG": %s",
			    oldrefname, strerror(errno));
		goto out;
	}

	if (!copy && refs_delete_ref(&refs->base, logmsg, oldrefname,
			    &orig_oid, REF_NO_DEREF)) {
		error("unable to delete old %s", oldrefname);
		goto rollback;
	}

	/*
	 * Since we are doing a shallow lookup, oid is not the
	 * correct value to pass to delete_ref as old_oid. But that
	 * doesn't matter, because an old_oid check wouldn't add to
	 * the safety anyway; we want to delete the reference whatever
	 * its current value.
	 */
	if (!copy && refs_resolve_ref_unsafe(&refs->base, newrefname,
					     RESOLVE_REF_READING | RESOLVE_REF_NO_RECURSE,
					     NULL, NULL) &&
	    refs_delete_ref(&refs->base, NULL, newrefname,
			    NULL, REF_NO_DEREF)) {
		if (errno == EISDIR) {
			struct strbuf path = STRBUF_INIT;
			int result;

			files_ref_path(refs, &path, newrefname);
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

	if (log && rename_tmp_log(refs, newrefname))
		goto rollback;

	logmoved = log;

	lock = lock_ref_oid_basic(refs, newrefname, &err);
	if (!lock) {
		if (copy)
			error("unable to copy '%s' to '%s': %s", oldrefname, newrefname, err.buf);
		else
			error("unable to rename '%s' to '%s': %s", oldrefname, newrefname, err.buf);
		strbuf_release(&err);
		goto rollback;
	}
	oidcpy(&lock->old_oid, &orig_oid);

	if (write_ref_to_lockfile(refs, lock, &orig_oid, 0, &err) ||
	    commit_ref_update(refs, lock, &orig_oid, logmsg, 0, &err)) {
		error("unable to write current sha1 into %s: %s", newrefname, err.buf);
		strbuf_release(&err);
		goto rollback;
	}

	ret = 0;
	goto out;

 rollback:
	lock = lock_ref_oid_basic(refs, oldrefname, &err);
	if (!lock) {
		error("unable to lock %s for rollback: %s", oldrefname, err.buf);
		strbuf_release(&err);
		goto rollbacklog;
	}

	if (write_ref_to_lockfile(refs, lock, &orig_oid, 0, &err) ||
	    commit_ref_update(refs, lock, &orig_oid, NULL, REF_SKIP_CREATE_REFLOG, &err)) {
		error("unable to write current sha1 into %s: %s", oldrefname, err.buf);
		strbuf_release(&err);
	}

 rollbacklog:
	if (logmoved && rename(sb_newref.buf, sb_oldref.buf))
		error("unable to restore logfile %s from %s: %s",
			oldrefname, newrefname, strerror(errno));
	if (!logmoved && log &&
	    rename(tmp_renamed_log.buf, sb_oldref.buf))
		error("unable to restore logfile %s from logs/"TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));
	ret = 1;
 out:
	strbuf_release(&sb_newref);
	strbuf_release(&sb_oldref);
	strbuf_release(&tmp_renamed_log);

	return ret;
}

static int files_rename_ref(struct ref_store *ref_store,
			    const char *oldrefname, const char *newrefname,
			    const char *logmsg)
{
	return files_copy_or_rename_ref(ref_store, oldrefname,
				 newrefname, logmsg, 0);
}

static int files_copy_ref(struct ref_store *ref_store,
			    const char *oldrefname, const char *newrefname,
			    const char *logmsg)
{
	return files_copy_or_rename_ref(ref_store, oldrefname,
				 newrefname, logmsg, 1);
}

static int close_ref_gently(struct ref_lock *lock)
{
	if (close_lock_file_gently(&lock->lk))
		return -1;
	return 0;
}

static int commit_ref(struct ref_lock *lock)
{
	char *path = get_locked_file_path(&lock->lk);
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

	if (commit_lock_file(&lock->lk))
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
static int log_ref_setup(struct files_ref_store *refs,
			 const char *refname, int force_create,
			 int *logfd, struct strbuf *err)
{
	enum log_refs_config log_refs_cfg = refs->log_all_ref_updates;
	struct strbuf logfile_sb = STRBUF_INIT;
	char *logfile;

	if (log_refs_cfg == LOG_REFS_UNSET)
		log_refs_cfg = is_bare_repository() ? LOG_REFS_NONE : LOG_REFS_NORMAL;

	files_reflog_path(refs, &logfile_sb, refname);
	logfile = strbuf_detach(&logfile_sb, NULL);

	if (force_create || should_autocreate_reflog(log_refs_cfg, refname)) {
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
		*logfd = open(logfile, O_APPEND | O_WRONLY);
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

static int files_create_reflog(struct ref_store *ref_store, const char *refname,
			       struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "create_reflog");
	int fd;

	if (log_ref_setup(refs, refname, 1, &fd, err))
		return -1;

	if (fd >= 0)
		close(fd);

	return 0;
}

static int log_ref_write_fd(int fd, const struct object_id *old_oid,
			    const struct object_id *new_oid,
			    const char *committer, const char *msg)
{
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	strbuf_addf(&sb, "%s %s %s", oid_to_hex(old_oid), oid_to_hex(new_oid), committer);
	if (msg && *msg) {
		strbuf_addch(&sb, '\t');
		strbuf_addstr(&sb, msg);
	}
	strbuf_addch(&sb, '\n');
	if (write_in_full(fd, sb.buf, sb.len) < 0)
		ret = -1;
	strbuf_release(&sb);
	return ret;
}

static int files_log_ref_write(struct files_ref_store *refs,
			       const char *refname, const struct object_id *old_oid,
			       const struct object_id *new_oid, const char *msg,
			       int flags, struct strbuf *err)
{
	int logfd, result;

	if (flags & REF_SKIP_CREATE_REFLOG)
		return 0;

	result = log_ref_setup(refs, refname,
			       flags & REF_FORCE_CREATE_REFLOG,
			       &logfd, err);

	if (result)
		return result;

	if (logfd < 0)
		return 0;
	result = log_ref_write_fd(logfd, old_oid, new_oid,
				  git_committer_info(0), msg);
	if (result) {
		struct strbuf sb = STRBUF_INIT;
		int save_errno = errno;

		files_reflog_path(refs, &sb, refname);
		strbuf_addf(err, "unable to append to '%s': %s",
			    sb.buf, strerror(save_errno));
		strbuf_release(&sb);
		close(logfd);
		return -1;
	}
	if (close(logfd)) {
		struct strbuf sb = STRBUF_INIT;
		int save_errno = errno;

		files_reflog_path(refs, &sb, refname);
		strbuf_addf(err, "unable to append to '%s': %s",
			    sb.buf, strerror(save_errno));
		strbuf_release(&sb);
		return -1;
	}
	return 0;
}

/*
 * Write oid into the open lockfile, then close the lockfile. On
 * errors, rollback the lockfile, fill in *err and return -1.
 */
static int write_ref_to_lockfile(struct files_ref_store *refs,
				 struct ref_lock *lock,
				 const struct object_id *oid,
				 int skip_oid_verification, struct strbuf *err)
{
	static char term = '\n';
	struct object *o;
	int fd;

	if (!skip_oid_verification) {
		o = parse_object(refs->base.repo, oid);
		if (!o) {
			strbuf_addf(
				err,
				"trying to write ref '%s' with nonexistent object %s",
				lock->ref_name, oid_to_hex(oid));
			unlock_ref(lock);
			return -1;
		}
		if (o->type != OBJ_COMMIT && is_branch(lock->ref_name)) {
			strbuf_addf(
				err,
				"trying to write non-commit object %s to branch '%s'",
				oid_to_hex(oid), lock->ref_name);
			unlock_ref(lock);
			return -1;
		}
	}
	fd = get_lock_file_fd(&lock->lk);
	if (write_in_full(fd, oid_to_hex(oid), refs->base.repo->hash_algo->hexsz) < 0 ||
	    write_in_full(fd, &term, 1) < 0 ||
	    fsync_component(FSYNC_COMPONENT_REFERENCE, get_lock_file_fd(&lock->lk)) < 0 ||
	    close_ref_gently(lock) < 0) {
		strbuf_addf(err,
			    "couldn't write '%s'", get_lock_file_path(&lock->lk));
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
			     const struct object_id *oid, const char *logmsg,
			     int flags,
			     struct strbuf *err)
{
	files_assert_main_repository(refs, "commit_ref_update");

	clear_loose_ref_cache(refs);
	if (files_log_ref_write(refs, lock->ref_name,
				&lock->old_oid, oid,
				logmsg, flags, err)) {
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
		int head_flag;
		const char *head_ref;

		head_ref = refs_resolve_ref_unsafe(&refs->base, "HEAD",
						   RESOLVE_REF_READING,
						   NULL, &head_flag);
		if (head_ref && (head_flag & REF_ISSYMREF) &&
		    !strcmp(head_ref, lock->ref_name)) {
			struct strbuf log_err = STRBUF_INIT;
			if (files_log_ref_write(refs, "HEAD",
						&lock->old_oid, oid,
						logmsg, flags, &log_err)) {
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

#ifdef NO_SYMLINK_HEAD
#define create_ref_symlink(a, b) (-1)
#else
static int create_ref_symlink(struct ref_lock *lock, const char *target)
{
	int ret = -1;

	char *ref_path = get_locked_file_path(&lock->lk);
	unlink(ref_path);
	ret = symlink(target, ref_path);
	free(ref_path);

	if (ret)
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
	return ret;
}
#endif

static int create_symref_lock(struct ref_lock *lock, const char *target,
			      struct strbuf *err)
{
	if (!fdopen_lock_file(&lock->lk, "w")) {
		strbuf_addf(err, "unable to fdopen %s: %s",
			     get_lock_file_path(&lock->lk), strerror(errno));
		return -1;
	}

	if (fprintf(get_lock_file_fp(&lock->lk), "ref: %s\n", target) < 0) {
		strbuf_addf(err, "unable to write to %s: %s",
			     get_lock_file_path(&lock->lk), strerror(errno));
		return -1;
	}

	return 0;
}

static int files_reflog_exists(struct ref_store *ref_store,
			       const char *refname)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ, "reflog_exists");
	struct strbuf sb = STRBUF_INIT;
	struct stat st;
	int ret;

	files_reflog_path(refs, &sb, refname);
	ret = !lstat(sb.buf, &st) && S_ISREG(st.st_mode);
	strbuf_release(&sb);
	return ret;
}

static int files_delete_reflog(struct ref_store *ref_store,
			       const char *refname)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "delete_reflog");
	struct strbuf sb = STRBUF_INIT;
	int ret;

	files_reflog_path(refs, &sb, refname);
	ret = remove_path(sb.buf);
	strbuf_release(&sb);
	return ret;
}

static int show_one_reflog_ent(struct files_ref_store *refs, struct strbuf *sb,
			       each_reflog_ent_fn fn, void *cb_data)
{
	struct object_id ooid, noid;
	char *email_end, *message;
	timestamp_t timestamp;
	int tz;
	const char *p = sb->buf;

	/* old SP new SP name <email> SP time TAB msg LF */
	if (!sb->len || sb->buf[sb->len - 1] != '\n' ||
	    parse_oid_hex_algop(p, &ooid, &p, refs->base.repo->hash_algo) || *p++ != ' ' ||
	    parse_oid_hex_algop(p, &noid, &p, refs->base.repo->hash_algo) || *p++ != ' ' ||
	    !(email_end = strchr(p, '>')) ||
	    email_end[1] != ' ' ||
	    !(timestamp = parse_timestamp(email_end + 2, &message, 10)) ||
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
	return fn(&ooid, &noid, p, timestamp, tz, message, cb_data);
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
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ,
			       "for_each_reflog_ent_reverse");
	struct strbuf sb = STRBUF_INIT;
	FILE *logfp;
	long pos;
	int ret = 0, at_tail = 1;

	files_reflog_path(refs, &sb, refname);
	logfp = fopen(sb.buf, "r");
	strbuf_release(&sb);
	if (!logfp)
		return -1;

	/* Jump to the end */
	if (fseek(logfp, 0, SEEK_END) < 0)
		ret = error("cannot seek back reflog for %s: %s",
			    refname, strerror(errno));
	pos = ftell(logfp);
	while (!ret && 0 < pos) {
		int cnt;
		size_t nread;
		char buf[BUFSIZ];
		char *endp, *scanp;

		/* Fill next block from the end */
		cnt = (sizeof(buf) < pos) ? sizeof(buf) : pos;
		if (fseek(logfp, pos - cnt, SEEK_SET)) {
			ret = error("cannot seek back reflog for %s: %s",
				    refname, strerror(errno));
			break;
		}
		nread = fread(buf, cnt, 1, logfp);
		if (nread != 1) {
			ret = error("cannot read %d bytes from reflog for %s: %s",
				    cnt, refname, strerror(errno));
			break;
		}
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
				ret = show_one_reflog_ent(refs, &sb, fn, cb_data);
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
				ret = show_one_reflog_ent(refs, &sb, fn, cb_data);
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
		BUG("reverse reflog parser had leftover data");

	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

static int files_for_each_reflog_ent(struct ref_store *ref_store,
				     const char *refname,
				     each_reflog_ent_fn fn, void *cb_data)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ,
			       "for_each_reflog_ent");
	FILE *logfp;
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	files_reflog_path(refs, &sb, refname);
	logfp = fopen(sb.buf, "r");
	strbuf_release(&sb);
	if (!logfp)
		return -1;

	while (!ret && !strbuf_getwholeline(&sb, logfp, '\n'))
		ret = show_one_reflog_ent(refs, &sb, fn, cb_data);
	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

struct files_reflog_iterator {
	struct ref_iterator base;
	struct ref_store *ref_store;
	struct dir_iterator *dir_iterator;
};

static int files_reflog_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct files_reflog_iterator *iter =
		(struct files_reflog_iterator *)ref_iterator;
	struct dir_iterator *diter = iter->dir_iterator;
	int ok;

	while ((ok = dir_iterator_advance(diter)) == ITER_OK) {
		if (!S_ISREG(diter->st.st_mode))
			continue;
		if (check_refname_format(diter->basename,
					 REFNAME_ALLOW_ONELEVEL))
			continue;

		iter->base.refname = diter->relative_path;
		return ITER_OK;
	}

	iter->dir_iterator = NULL;
	if (ref_iterator_abort(ref_iterator) == ITER_ERROR)
		ok = ITER_ERROR;
	return ok;
}

static int files_reflog_iterator_peel(struct ref_iterator *ref_iterator UNUSED,
				      struct object_id *peeled UNUSED)
{
	BUG("ref_iterator_peel() called for reflog_iterator");
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
	.advance = files_reflog_iterator_advance,
	.peel = files_reflog_iterator_peel,
	.abort = files_reflog_iterator_abort,
};

static struct ref_iterator *reflog_iterator_begin(struct ref_store *ref_store,
						  const char *gitdir)
{
	struct dir_iterator *diter;
	struct files_reflog_iterator *iter;
	struct ref_iterator *ref_iterator;
	struct strbuf sb = STRBUF_INIT;

	strbuf_addf(&sb, "%s/logs", gitdir);

	diter = dir_iterator_begin(sb.buf, DIR_ITERATOR_SORTED);
	if (!diter) {
		strbuf_release(&sb);
		return empty_ref_iterator_begin();
	}

	CALLOC_ARRAY(iter, 1);
	ref_iterator = &iter->base;

	base_ref_iterator_init(ref_iterator, &files_reflog_iterator_vtable);
	iter->dir_iterator = diter;
	iter->ref_store = ref_store;
	strbuf_release(&sb);

	return ref_iterator;
}

static struct ref_iterator *files_reflog_iterator_begin(struct ref_store *ref_store)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ,
			       "reflog_iterator_begin");

	if (!strcmp(refs->base.gitdir, refs->gitcommondir)) {
		return reflog_iterator_begin(ref_store, refs->gitcommondir);
	} else {
		return merge_ref_iterator_begin(
			reflog_iterator_begin(ref_store, refs->base.gitdir),
			reflog_iterator_begin(ref_store, refs->gitcommondir),
			ref_iterator_select, refs);
	}
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
	    (update->flags & REF_SKIP_CREATE_REFLOG) ||
	    (update->flags & REF_IS_PRUNING) ||
	    (update->flags & REF_UPDATE_VIA_HEAD))
		return 0;

	if (strcmp(update->refname, head_ref))
		return 0;

	/*
	 * First make sure that HEAD is not already in the
	 * transaction. This check is O(lg N) in the transaction
	 * size, but it happens at most once per transaction.
	 */
	if (string_list_has_string(affected_refnames, "HEAD")) {
		/* An entry already existed */
		strbuf_addf(err,
			    "multiple updates for 'HEAD' (including one "
			    "via its referent '%s') are not allowed",
			    update->refname);
		return TRANSACTION_NAME_CONFLICT;
	}

	new_update = ref_transaction_add_update(
			transaction, "HEAD",
			update->flags | REF_LOG_ONLY | REF_NO_DEREF,
			&update->new_oid, &update->old_oid,
			NULL, NULL, update->msg);

	/*
	 * Add "HEAD". This insertion is O(N) in the transaction
	 * size, but it happens at most once per transaction.
	 * Add new_update->refname instead of a literal "HEAD".
	 */
	if (strcmp(new_update->refname, "HEAD"))
		BUG("%s unexpectedly not 'HEAD'", new_update->refname);
	item = string_list_insert(affected_refnames, new_update->refname);
	item->util = new_update;

	return 0;
}

/*
 * update is for a symref that points at referent and doesn't have
 * REF_NO_DEREF set. Split it into two updates:
 * - The original update, but with REF_LOG_ONLY and REF_NO_DEREF set
 * - A new, separate update for the referent reference
 * Note that the new update will itself be subject to splitting when
 * the iteration gets to it.
 */
static int split_symref_update(struct ref_update *update,
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
	 * transaction. This check is O(lg N) in the transaction
	 * size, but it happens at most once per symref in a
	 * transaction.
	 */
	if (string_list_has_string(affected_refnames, referent)) {
		/* An entry already exists */
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
			update->new_target ? NULL : &update->new_oid,
			update->old_target ? NULL : &update->old_oid,
			update->new_target, update->old_target, update->msg);

	new_update->parent_update = update;

	/*
	 * Change the symbolic ref update to log only. Also, it
	 * doesn't need to check its old OID value, as that will be
	 * done when new_update is processed.
	 */
	update->flags |= REF_LOG_ONLY | REF_NO_DEREF;
	update->flags &= ~REF_HAVE_OLD;

	/*
	 * Add the referent. This insertion is O(N) in the transaction
	 * size, but it happens at most once per symref in a
	 * transaction. Make sure to add new_update->refname, which will
	 * be valid as long as affected_refnames is in use, and NOT
	 * referent, which might soon be freed by our caller.
	 */
	item = string_list_insert(affected_refnames, new_update->refname);
	if (item->util)
		BUG("%s unexpectedly found in affected_refnames",
		    new_update->refname);
	item->util = new_update;

	return 0;
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
		   oideq(oid, &update->old_oid))
		return 0;

	if (is_null_oid(&update->old_oid))
		strbuf_addf(err, "cannot lock ref '%s': "
			    "reference already exists",
			    ref_update_original_update_refname(update));
	else if (is_null_oid(oid))
		strbuf_addf(err, "cannot lock ref '%s': "
			    "reference is missing but expected %s",
			    ref_update_original_update_refname(update),
			    oid_to_hex(&update->old_oid));
	else
		strbuf_addf(err, "cannot lock ref '%s': "
			    "is at %s but expected %s",
			    ref_update_original_update_refname(update),
			    oid_to_hex(oid),
			    oid_to_hex(&update->old_oid));

	return -1;
}

/*
 * Prepare for carrying out update:
 * - Lock the reference referred to by update.
 * - Read the reference under lock.
 * - Check that its old OID value (if specified) is correct, and in
 *   any case record it in update->lock->old_oid for later use when
 *   writing the reflog.
 * - If it is a symref update without REF_NO_DEREF, split it up into a
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
	int mustexist = ref_update_expects_existing_old_ref(update);
	int ret = 0;
	struct ref_lock *lock;

	files_assert_main_repository(refs, "lock_ref_for_update");

	if ((update->flags & REF_HAVE_NEW) && ref_update_has_null_new_value(update))
		update->flags |= REF_DELETING;

	if (head_ref) {
		ret = split_head_update(update, transaction, head_ref,
					affected_refnames, err);
		if (ret)
			goto out;
	}

	ret = lock_raw_ref(refs, update->refname, mustexist,
			   affected_refnames,
			   &lock, &referent,
			   &update->type, err);
	if (ret) {
		char *reason;

		reason = strbuf_detach(err, NULL);
		strbuf_addf(err, "cannot lock ref '%s': %s",
			    ref_update_original_update_refname(update), reason);
		free(reason);
		goto out;
	}

	update->backend_data = lock;

	if (update->type & REF_ISSYMREF) {
		if (update->flags & REF_NO_DEREF) {
			/*
			 * We won't be reading the referent as part of
			 * the transaction, so we have to read it here
			 * to record and possibly check old_oid:
			 */
			if (!refs_resolve_ref_unsafe(&refs->base,
						     referent.buf, 0,
						     &lock->old_oid, NULL)) {
				if (update->flags & REF_HAVE_OLD) {
					strbuf_addf(err, "cannot lock ref '%s': "
						    "error reading reference",
						    ref_update_original_update_refname(update));
					ret = TRANSACTION_GENERIC_ERROR;
					goto out;
				}
			}

			if (update->old_target) {
				if (ref_update_check_old_target(referent.buf, update, err)) {
					ret = TRANSACTION_GENERIC_ERROR;
					goto out;
				}
			} else if  (check_old_oid(update, &lock->old_oid, err)) {
				ret = TRANSACTION_GENERIC_ERROR;
				goto out;
			}
		} else {
			/*
			 * Create a new update for the reference this
			 * symref is pointing at. Also, we will record
			 * and verify old_oid for this update as part
			 * of processing the split-off update, so we
			 * don't have to do it here.
			 */
			ret = split_symref_update(update,
						  referent.buf, transaction,
						  affected_refnames, err);
			if (ret)
				goto out;
		}
	} else {
		struct ref_update *parent_update;

		/*
		 * Even if the ref is a regular ref, if `old_target` is set, we
		 * fail with an error.
		 */
		if (update->old_target) {
			strbuf_addf(err, _("cannot lock ref '%s': "
					   "expected symref with target '%s': "
					   "but is a regular ref"),
				    ref_update_original_update_refname(update),
				    update->old_target);
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
		} else if  (check_old_oid(update, &lock->old_oid, err)) {
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
		}

		/*
		 * If this update is happening indirectly because of a
		 * symref update, record the old OID in the parent
		 * update:
		 */
		for (parent_update = update->parent_update;
		     parent_update;
		     parent_update = parent_update->parent_update) {
			struct ref_lock *parent_lock = parent_update->backend_data;
			oidcpy(&parent_lock->old_oid, &lock->old_oid);
		}
	}

	if (update->new_target && !(update->flags & REF_LOG_ONLY)) {
		if (create_symref_lock(lock, update->new_target, err)) {
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
		}

		if (close_ref_gently(lock)) {
			strbuf_addf(err, "couldn't close '%s.lock'",
				    update->refname);
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
		}

		/*
		 * Once we have created the symref lock, the commit
		 * phase of the transaction only needs to commit the lock.
		 */
		update->flags |= REF_NEEDS_COMMIT;
	} else if ((update->flags & REF_HAVE_NEW) &&
		   !(update->flags & REF_DELETING) &&
		   !(update->flags & REF_LOG_ONLY)) {
		if (!(update->type & REF_ISSYMREF) &&
		    oideq(&lock->old_oid, &update->new_oid)) {
			/*
			 * The reference already has the desired
			 * value, so we don't need to write it.
			 */
		} else if (write_ref_to_lockfile(
				   refs, lock, &update->new_oid,
				   update->flags & REF_SKIP_OID_VERIFICATION,
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
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
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
		if (close_ref_gently(lock)) {
			strbuf_addf(err, "couldn't close '%s.lock'",
				    update->refname);
			ret = TRANSACTION_GENERIC_ERROR;
			goto out;
		}
	}

out:
	strbuf_release(&referent);
	return ret;
}

struct files_transaction_backend_data {
	struct ref_transaction *packed_transaction;
	int packed_refs_locked;
};

/*
 * Unlock any references in `transaction` that are still locked, and
 * mark the transaction closed.
 */
static void files_transaction_cleanup(struct files_ref_store *refs,
				      struct ref_transaction *transaction)
{
	size_t i;
	struct files_transaction_backend_data *backend_data =
		transaction->backend_data;
	struct strbuf err = STRBUF_INIT;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (lock) {
			unlock_ref(lock);
			update->backend_data = NULL;
		}
	}

	if (backend_data) {
		if (backend_data->packed_transaction &&
		    ref_transaction_abort(backend_data->packed_transaction, &err)) {
			error("error aborting transaction: %s", err.buf);
			strbuf_release(&err);
		}

		if (backend_data->packed_refs_locked)
			packed_refs_unlock(refs->packed_ref_store);

		free(backend_data);
	}

	transaction->state = REF_TRANSACTION_CLOSED;
}

static int files_transaction_prepare(struct ref_store *ref_store,
				     struct ref_transaction *transaction,
				     struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE,
			       "ref_transaction_prepare");
	size_t i;
	int ret = 0;
	struct string_list affected_refnames = STRING_LIST_INIT_NODUP;
	char *head_ref = NULL;
	int head_type;
	struct files_transaction_backend_data *backend_data;
	struct ref_transaction *packed_transaction = NULL;

	assert(err);

	if (!transaction->nr)
		goto cleanup;

	CALLOC_ARRAY(backend_data, 1);
	transaction->backend_data = backend_data;

	/*
	 * Fail if a refname appears more than once in the
	 * transaction. (If we end up splitting up any updates using
	 * split_symref_update() or split_head_update(), those
	 * functions will check that the new updates don't have the
	 * same refname as any existing ones.) Also fail if any of the
	 * updates use REF_IS_PRUNING without REF_NO_DEREF.
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct string_list_item *item =
			string_list_append(&affected_refnames, update->refname);

		if ((update->flags & REF_IS_PRUNING) &&
		    !(update->flags & REF_NO_DEREF))
			BUG("REF_IS_PRUNING set without REF_NO_DEREF");

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
	head_ref = refs_resolve_refdup(ref_store, "HEAD",
				       RESOLVE_REF_NO_RECURSE,
				       NULL, &head_type);

	if (head_ref && !(head_type & REF_ISSYMREF)) {
		FREE_AND_NULL(head_ref);
	}

	/*
	 * Acquire all locks, verify old values if provided, check
	 * that new values are valid, and write new values to the
	 * lockfiles, ready to be activated. Only keep one lockfile
	 * open at a time to avoid running out of file descriptors.
	 * Note that lock_ref_for_update() might append more updates
	 * to the transaction.
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		ret = lock_ref_for_update(refs, update, transaction,
					  head_ref, &affected_refnames, err);
		if (ret)
			goto cleanup;

		if (update->flags & REF_DELETING &&
		    !(update->flags & REF_LOG_ONLY) &&
		    !(update->flags & REF_IS_PRUNING)) {
			/*
			 * This reference has to be deleted from
			 * packed-refs if it exists there.
			 */
			if (!packed_transaction) {
				packed_transaction = ref_store_transaction_begin(
						refs->packed_ref_store, err);
				if (!packed_transaction) {
					ret = TRANSACTION_GENERIC_ERROR;
					goto cleanup;
				}

				backend_data->packed_transaction =
					packed_transaction;
			}

			ref_transaction_add_update(
					packed_transaction, update->refname,
					REF_HAVE_NEW | REF_NO_DEREF,
					&update->new_oid, NULL,
					NULL, NULL, NULL);
		}
	}

	if (packed_transaction) {
		if (packed_refs_lock(refs->packed_ref_store, 0, err)) {
			ret = TRANSACTION_GENERIC_ERROR;
			goto cleanup;
		}
		backend_data->packed_refs_locked = 1;

		if (is_packed_transaction_needed(refs->packed_ref_store,
						 packed_transaction)) {
			ret = ref_transaction_prepare(packed_transaction, err);
			/*
			 * A failure during the prepare step will abort
			 * itself, but not free. Do that now, and disconnect
			 * from the files_transaction so it does not try to
			 * abort us when we hit the cleanup code below.
			 */
			if (ret) {
				ref_transaction_free(packed_transaction);
				backend_data->packed_transaction = NULL;
			}
		} else {
			/*
			 * We can skip rewriting the `packed-refs`
			 * file. But we do need to leave it locked, so
			 * that somebody else doesn't pack a reference
			 * that we are trying to delete.
			 *
			 * We need to disconnect our transaction from
			 * backend_data, since the abort (whether successful or
			 * not) will free it.
			 */
			backend_data->packed_transaction = NULL;
			if (ref_transaction_abort(packed_transaction, err)) {
				ret = TRANSACTION_GENERIC_ERROR;
				goto cleanup;
			}
		}
	}

cleanup:
	free(head_ref);
	string_list_clear(&affected_refnames, 0);

	if (ret)
		files_transaction_cleanup(refs, transaction);
	else
		transaction->state = REF_TRANSACTION_PREPARED;

	return ret;
}

static int parse_and_write_reflog(struct files_ref_store *refs,
				  struct ref_update *update,
				  struct ref_lock *lock,
				  struct strbuf *err)
{
	if (update->new_target) {
		/*
		 * We want to get the resolved OID for the target, to ensure
		 * that the correct value is added to the reflog.
		 */
		if (!refs_resolve_ref_unsafe(&refs->base, update->new_target,
					     RESOLVE_REF_READING,
					     &update->new_oid, NULL)) {
			/*
			 * TODO: currently we skip creating reflogs for dangling
			 * symref updates. It would be nice to capture this as
			 * zero oid updates however.
			 */
			return 0;
		}
	}

	if (files_log_ref_write(refs, lock->ref_name, &lock->old_oid,
				&update->new_oid, update->msg, update->flags, err)) {
		char *old_msg = strbuf_detach(err, NULL);

		strbuf_addf(err, "cannot update the ref '%s': %s",
			    lock->ref_name, old_msg);
		free(old_msg);
		unlock_ref(lock);
		update->backend_data = NULL;
		return -1;
	}

	return 0;
}

static int files_transaction_finish(struct ref_store *ref_store,
				    struct ref_transaction *transaction,
				    struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "ref_transaction_finish");
	size_t i;
	int ret = 0;
	struct strbuf sb = STRBUF_INIT;
	struct files_transaction_backend_data *backend_data;
	struct ref_transaction *packed_transaction;


	assert(err);

	if (!transaction->nr) {
		transaction->state = REF_TRANSACTION_CLOSED;
		return 0;
	}

	backend_data = transaction->backend_data;
	packed_transaction = backend_data->packed_transaction;

	/* Perform updates first so live commits remain referenced */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (update->flags & REF_NEEDS_COMMIT ||
		    update->flags & REF_LOG_ONLY) {
			if (parse_and_write_reflog(refs, update, lock, err)) {
				ret = TRANSACTION_GENERIC_ERROR;
				goto cleanup;
			}
		}

		/*
		 * We try creating a symlink, if that succeeds we continue to the
		 * next update. If not, we try and create a regular symref.
		 */
		if (update->new_target && refs->prefer_symlink_refs)
			if (!create_ref_symlink(lock, update->new_target))
				continue;

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

	/*
	 * Now that updates are safely completed, we can perform
	 * deletes. First delete the reflogs of any references that
	 * will be deleted, since (in the unexpected event of an
	 * error) leaving a reference without a reflog is less bad
	 * than leaving a reflog without a reference (the latter is a
	 * mildly invalid repository state):
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		if (update->flags & REF_DELETING &&
		    !(update->flags & REF_LOG_ONLY) &&
		    !(update->flags & REF_IS_PRUNING)) {
			strbuf_reset(&sb);
			files_reflog_path(refs, &sb, update->refname);
			if (!unlink_or_warn(sb.buf))
				try_remove_empty_parents(refs, update->refname,
							 REMOVE_EMPTY_PARENTS_REFLOG);
		}
	}

	/*
	 * Perform deletes now that updates are safely completed.
	 *
	 * First delete any packed versions of the references, while
	 * retaining the packed-refs lock:
	 */
	if (packed_transaction) {
		ret = ref_transaction_commit(packed_transaction, err);
		ref_transaction_free(packed_transaction);
		packed_transaction = NULL;
		backend_data->packed_transaction = NULL;
		if (ret)
			goto cleanup;
	}

	/* Now delete the loose versions of the references: */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct ref_lock *lock = update->backend_data;

		if (update->flags & REF_DELETING &&
		    !(update->flags & REF_LOG_ONLY)) {
			update->flags |= REF_DELETED_RMDIR;
			if (!(update->type & REF_ISPACKED) ||
			    update->type & REF_ISSYMREF) {
				/* It is a loose reference. */
				strbuf_reset(&sb);
				files_ref_path(refs, &sb, lock->ref_name);
				if (unlink_or_msg(sb.buf, err)) {
					ret = TRANSACTION_GENERIC_ERROR;
					goto cleanup;
				}
			}
		}
	}

	clear_loose_ref_cache(refs);

cleanup:
	files_transaction_cleanup(refs, transaction);

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if (update->flags & REF_DELETED_RMDIR) {
			/*
			 * The reference was deleted. Delete any
			 * empty parent directories. (Note that this
			 * can only work because we have already
			 * removed the lockfile.)
			 */
			try_remove_empty_parents(refs, update->refname,
						 REMOVE_EMPTY_PARENTS_REF);
		}
	}

	strbuf_release(&sb);
	return ret;
}

static int files_transaction_abort(struct ref_store *ref_store,
				   struct ref_transaction *transaction,
				   struct strbuf *err UNUSED)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, 0, "ref_transaction_abort");

	files_transaction_cleanup(refs, transaction);
	return 0;
}

static int ref_present(const char *refname, const char *referent UNUSED,
		       const struct object_id *oid UNUSED,
		       int flags UNUSED,
		       void *cb_data)
{
	struct string_list *affected_refnames = cb_data;

	return string_list_has_string(affected_refnames, refname);
}

static int files_initial_transaction_commit(struct ref_store *ref_store,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE,
			       "initial_ref_transaction_commit");
	size_t i;
	int ret = 0;
	struct string_list affected_refnames = STRING_LIST_INIT_NODUP;
	struct ref_transaction *packed_transaction = NULL;

	assert(err);

	if (transaction->state != REF_TRANSACTION_OPEN)
		BUG("commit called for transaction that is not open");

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
	if (refs_for_each_rawref(&refs->base, ref_present,
				 &affected_refnames))
		BUG("initial ref transaction called with existing refs");

	packed_transaction = ref_store_transaction_begin(refs->packed_ref_store, err);
	if (!packed_transaction) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if ((update->flags & REF_HAVE_OLD) &&
		    !is_null_oid(&update->old_oid))
			BUG("initial ref transaction with old_sha1 set");
		if (refs_verify_refname_available(&refs->base, update->refname,
						  &affected_refnames, NULL,
						  err)) {
			ret = TRANSACTION_NAME_CONFLICT;
			goto cleanup;
		}

		/*
		 * Add a reference creation for this reference to the
		 * packed-refs transaction:
		 */
		ref_transaction_add_update(packed_transaction, update->refname,
					   update->flags & ~REF_HAVE_OLD,
					   &update->new_oid, &update->old_oid,
					   NULL, NULL, NULL);
	}

	if (packed_refs_lock(refs->packed_ref_store, 0, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto cleanup;
	}

	if (initial_ref_transaction_commit(packed_transaction, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
	}

	packed_refs_unlock(refs->packed_ref_store);
cleanup:
	if (packed_transaction)
		ref_transaction_free(packed_transaction);
	transaction->state = REF_TRANSACTION_CLOSED;
	string_list_clear(&affected_refnames, 0);
	return ret;
}

struct expire_reflog_cb {
	reflog_expiry_should_prune_fn *should_prune_fn;
	void *policy_cb;
	FILE *newlog;
	struct object_id last_kept_oid;
	unsigned int rewrite:1,
		     dry_run:1;
};

static int expire_reflog_ent(struct object_id *ooid, struct object_id *noid,
			     const char *email, timestamp_t timestamp, int tz,
			     const char *message, void *cb_data)
{
	struct expire_reflog_cb *cb = cb_data;
	reflog_expiry_should_prune_fn *fn = cb->should_prune_fn;

	if (cb->rewrite)
		ooid = &cb->last_kept_oid;

	if (fn(ooid, noid, email, timestamp, tz, message, cb->policy_cb))
		return 0;

	if (cb->dry_run)
		return 0; /* --dry-run */

	fprintf(cb->newlog, "%s %s %s %"PRItime" %+05d\t%s", oid_to_hex(ooid),
		oid_to_hex(noid), email, timestamp, tz, message);
	oidcpy(&cb->last_kept_oid, noid);

	return 0;
}

static int files_reflog_expire(struct ref_store *ref_store,
			       const char *refname,
			       unsigned int expire_flags,
			       reflog_expiry_prepare_fn prepare_fn,
			       reflog_expiry_should_prune_fn should_prune_fn,
			       reflog_expiry_cleanup_fn cleanup_fn,
			       void *policy_cb_data)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "reflog_expire");
	struct lock_file reflog_lock = LOCK_INIT;
	struct expire_reflog_cb cb;
	struct ref_lock *lock;
	struct strbuf log_file_sb = STRBUF_INIT;
	char *log_file;
	int status = 0;
	struct strbuf err = STRBUF_INIT;
	const struct object_id *oid;

	memset(&cb, 0, sizeof(cb));
	cb.rewrite = !!(expire_flags & EXPIRE_REFLOGS_REWRITE);
	cb.dry_run = !!(expire_flags & EXPIRE_REFLOGS_DRY_RUN);
	cb.policy_cb = policy_cb_data;
	cb.should_prune_fn = should_prune_fn;

	/*
	 * The reflog file is locked by holding the lock on the
	 * reference itself, plus we might need to update the
	 * reference if --updateref was specified:
	 */
	lock = lock_ref_oid_basic(refs, refname, &err);
	if (!lock) {
		error("cannot lock ref '%s': %s", refname, err.buf);
		strbuf_release(&err);
		return -1;
	}
	oid = &lock->old_oid;

	/*
	 * When refs are deleted, their reflog is deleted before the
	 * ref itself is deleted. This is because there is no separate
	 * lock for reflog; instead we take a lock on the ref with
	 * lock_ref_oid_basic().
	 *
	 * If a race happens and the reflog doesn't exist after we've
	 * acquired the lock that's OK. We've got nothing more to do;
	 * We were asked to delete the reflog, but someone else
	 * deleted it! The caller doesn't care that we deleted it,
	 * just that it is deleted. So we can return successfully.
	 */
	if (!refs_reflog_exists(ref_store, refname)) {
		unlock_ref(lock);
		return 0;
	}

	files_reflog_path(refs, &log_file_sb, refname);
	log_file = strbuf_detach(&log_file_sb, NULL);
	if (!cb.dry_run) {
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

	(*prepare_fn)(refname, oid, cb.policy_cb);
	refs_for_each_reflog_ent(ref_store, refname, expire_reflog_ent, &cb);
	(*cleanup_fn)(cb.policy_cb);

	if (!cb.dry_run) {
		/*
		 * It doesn't make sense to adjust a reference pointed
		 * to by a symbolic ref based on expiring entries in
		 * the symbolic reference's reflog. Nor can we update
		 * a reference if there are no remaining reflog
		 * entries.
		 */
		int update = 0;

		if ((expire_flags & EXPIRE_REFLOGS_UPDATE_REF) &&
		    !is_null_oid(&cb.last_kept_oid)) {
			int type;
			const char *ref;

			ref = refs_resolve_ref_unsafe(&refs->base, refname,
						      RESOLVE_REF_NO_RECURSE,
						      NULL, &type);
			update = !!(ref && !(type & REF_ISSYMREF));
		}

		if (close_lock_file_gently(&reflog_lock)) {
			status |= error("couldn't write %s: %s", log_file,
					strerror(errno));
			rollback_lock_file(&reflog_lock);
		} else if (update &&
			   (write_in_full(get_lock_file_fd(&lock->lk),
				oid_to_hex(&cb.last_kept_oid), refs->base.repo->hash_algo->hexsz) < 0 ||
			    write_str_in_full(get_lock_file_fd(&lock->lk), "\n") < 0 ||
			    close_ref_gently(lock) < 0)) {
			status |= error("couldn't write %s",
					get_lock_file_path(&lock->lk));
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

static int files_ref_store_create_on_disk(struct ref_store *ref_store,
					  int flags,
					  struct strbuf *err UNUSED)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "create");
	struct strbuf sb = STRBUF_INIT;

	/*
	 * We need to create a "refs" dir in any case so that older versions of
	 * Git can tell that this is a repository. This serves two main purposes:
	 *
	 * - Clients will know to stop walking the parent-directory chain when
	 *   detecting the Git repository. Otherwise they may end up detecting
	 *   a Git repository in a parent directory instead.
	 *
	 * - Instead of failing to detect a repository with unknown reference
	 *   format altogether, old clients will print an error saying that
	 *   they do not understand the reference format extension.
	 */
	strbuf_addf(&sb, "%s/refs", ref_store->gitdir);
	safe_create_dir(sb.buf, 1);
	adjust_shared_perm(sb.buf);

	/*
	 * There is no need to create directories for common refs when creating
	 * a worktree ref store.
	 */
	if (!(flags & REF_STORE_CREATE_ON_DISK_IS_WORKTREE)) {
		/*
		 * Create .git/refs/{heads,tags}
		 */
		strbuf_reset(&sb);
		files_ref_path(refs, &sb, "refs/heads");
		safe_create_dir(sb.buf, 1);

		strbuf_reset(&sb);
		files_ref_path(refs, &sb, "refs/tags");
		safe_create_dir(sb.buf, 1);
	}

	strbuf_release(&sb);
	return 0;
}

struct remove_one_root_ref_data {
	const char *gitdir;
	struct strbuf *err;
};

static int remove_one_root_ref(const char *refname,
			       void *cb_data)
{
	struct remove_one_root_ref_data *data = cb_data;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;

	strbuf_addf(&buf, "%s/%s", data->gitdir, refname);

	ret = unlink(buf.buf);
	if (ret < 0)
		strbuf_addf(data->err, "could not delete %s: %s\n",
			    refname, strerror(errno));

	strbuf_release(&buf);
	return ret;
}

static int files_ref_store_remove_on_disk(struct ref_store *ref_store,
					  struct strbuf *err)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_WRITE, "remove");
	struct remove_one_root_ref_data data = {
		.gitdir = refs->base.gitdir,
		.err = err,
	};
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	strbuf_addf(&sb, "%s/refs", refs->base.gitdir);
	if (remove_dir_recursively(&sb, 0) < 0) {
		strbuf_addf(err, "could not delete refs: %s",
			    strerror(errno));
		ret = -1;
	}
	strbuf_reset(&sb);

	strbuf_addf(&sb, "%s/logs", refs->base.gitdir);
	if (remove_dir_recursively(&sb, 0) < 0) {
		strbuf_addf(err, "could not delete logs: %s",
			    strerror(errno));
		ret = -1;
	}
	strbuf_reset(&sb);

	if (for_each_root_ref(refs, remove_one_root_ref, &data) < 0)
		ret = -1;

	if (ref_store_remove_on_disk(refs->packed_ref_store, err) < 0)
		ret = -1;

	strbuf_release(&sb);
	return ret;
}

/*
 * For refs and reflogs, they share a unified interface when scanning
 * the whole directory. This function is used as the callback for each
 * regular file or symlink in the directory.
 */
typedef int (*files_fsck_refs_fn)(struct ref_store *ref_store,
				  struct fsck_options *o,
				  const char *refs_check_dir,
				  struct dir_iterator *iter);

static int files_fsck_refs_name(struct ref_store *ref_store UNUSED,
				struct fsck_options *o,
				const char *refs_check_dir,
				struct dir_iterator *iter)
{
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	/*
	 * Ignore the files ending with ".lock" as they may be lock files
	 * However, do not allow bare ".lock" files.
	 */
	if (iter->basename[0] != '.' && ends_with(iter->basename, ".lock"))
		goto cleanup;

	if (check_refname_format(iter->basename, REFNAME_ALLOW_ONELEVEL)) {
		struct fsck_ref_report report = { 0 };

		strbuf_addf(&sb, "%s/%s", refs_check_dir, iter->relative_path);
		report.path = sb.buf;
		ret = fsck_report_ref(o, &report,
				      FSCK_MSG_BAD_REF_NAME,
				      "invalid refname format");
	}

cleanup:
	strbuf_release(&sb);
	return ret;
}

static int files_fsck_refs_dir(struct ref_store *ref_store,
			       struct fsck_options *o,
			       const char *refs_check_dir,
			       files_fsck_refs_fn *fsck_refs_fn)
{
	struct strbuf sb = STRBUF_INIT;
	struct dir_iterator *iter;
	int iter_status;
	int ret = 0;

	strbuf_addf(&sb, "%s/%s", ref_store->gitdir, refs_check_dir);

	iter = dir_iterator_begin(sb.buf, 0);
	if (!iter) {
		ret = error_errno(_("cannot open directory %s"), sb.buf);
		goto out;
	}

	while ((iter_status = dir_iterator_advance(iter)) == ITER_OK) {
		if (S_ISDIR(iter->st.st_mode)) {
			continue;
		} else if (S_ISREG(iter->st.st_mode) ||
			   S_ISLNK(iter->st.st_mode)) {
			if (o->verbose)
				fprintf_ln(stderr, "Checking %s/%s",
					   refs_check_dir, iter->relative_path);
			for (size_t i = 0; fsck_refs_fn[i]; i++) {
				if (fsck_refs_fn[i](ref_store, o, refs_check_dir, iter))
					ret = -1;
			}
		} else {
			struct fsck_ref_report report = { .path = iter->basename };
			if (fsck_report_ref(o, &report,
					    FSCK_MSG_BAD_REF_FILETYPE,
					    "unexpected file type"))
				ret = -1;
		}
	}

	if (iter_status != ITER_DONE)
		ret = error(_("failed to iterate over '%s'"), sb.buf);

out:
	strbuf_release(&sb);
	return ret;
}

static int files_fsck_refs(struct ref_store *ref_store,
			   struct fsck_options *o)
{
	files_fsck_refs_fn fsck_refs_fn[]= {
		files_fsck_refs_name,
		NULL,
	};

	if (o->verbose)
		fprintf_ln(stderr, _("Checking references consistency"));
	return files_fsck_refs_dir(ref_store, o,  "refs", fsck_refs_fn);
}

static int files_fsck(struct ref_store *ref_store,
		      struct fsck_options *o)
{
	struct files_ref_store *refs =
		files_downcast(ref_store, REF_STORE_READ, "fsck");

	return files_fsck_refs(ref_store, o) |
	       refs->packed_ref_store->be->fsck(refs->packed_ref_store, o);
}

struct ref_storage_be refs_be_files = {
	.name = "files",
	.init = files_ref_store_init,
	.release = files_ref_store_release,
	.create_on_disk = files_ref_store_create_on_disk,
	.remove_on_disk = files_ref_store_remove_on_disk,

	.transaction_prepare = files_transaction_prepare,
	.transaction_finish = files_transaction_finish,
	.transaction_abort = files_transaction_abort,
	.initial_transaction_commit = files_initial_transaction_commit,

	.pack_refs = files_pack_refs,
	.rename_ref = files_rename_ref,
	.copy_ref = files_copy_ref,

	.iterator_begin = files_ref_iterator_begin,
	.read_raw_ref = files_read_raw_ref,
	.read_symbolic_ref = files_read_symbolic_ref,

	.reflog_iterator_begin = files_reflog_iterator_begin,
	.for_each_reflog_ent = files_for_each_reflog_ent,
	.for_each_reflog_ent_reverse = files_for_each_reflog_ent_reverse,
	.reflog_exists = files_reflog_exists,
	.create_reflog = files_create_reflog,
	.delete_reflog = files_delete_reflog,
	.reflog_expire = files_reflog_expire,

	.fsck = files_fsck,
};
