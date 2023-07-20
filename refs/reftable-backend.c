#include "../git-compat-util.h"
#include "../abspath.h"
#include "../chdir-notify.h"
#include "../config.h"
#include "../environment.h"
#include "../hash.h"
#include "../hex.h"
#include "../iterator.h"
#include "../ident.h"
#include "../lockfile.h"
#include "../object.h"
#include "../path.h"
#include "../refs.h"
#include "../reftable/reftable-stack.h"
#include "../reftable/reftable-record.h"
#include "../reftable/reftable-error.h"
#include "../reftable/reftable-blocksource.h"
#include "../reftable/reftable-reader.h"
#include "../reftable/reftable-iterator.h"
#include "../reftable/reftable-merged.h"
#include "../reftable/reftable-generic.h"
#include "../worktree.h"
#include "refs-internal.h"

extern struct ref_storage_be refs_be_reftable;

struct git_reftable_ref_store {
	struct ref_store base;
	unsigned int store_flags;

	int err;
	char *repo_dir;
	char *reftable_dir;

	struct reftable_stack *main_stack;
	struct reftable_stack *worktree_stack;

	struct reftable_write_options write_options;
};

/*
 * Some refs are global to the repository (refs/heads/{*}), while others are
 * local to the worktree (eg. HEAD, refs/bisect/{*}). We solve this by having
 * two separate databases (ie. two reftable/ directories), one for the
 * repository, and one for the worktree. For reading, we merge the view (see
 * git_reftable_iterator) of both, when necessary.
 *
 * Unfortunately, the worktrees can also be selected by specifying a magic
 * refname (eg. worktree/BLA/refname, even if BLA isn't the current worktree.)
 */
static struct reftable_stack *stack_for(struct git_reftable_ref_store *store,
					const char *refname)
{
	const char *wtname;
	int wtname_len;
	const char *stripped;
	enum ref_worktree_type wt_type;
	if (!refname)
		return store->main_stack;

	wt_type = parse_worktree_ref(refname, &wtname, &wtname_len, &stripped);
	if (wt_type == REF_WORKTREE_OTHER) {
		/* Woe you if you try to access worktree/BLA/REF and the current
		 * worktree from the same process.
		 */
		struct strbuf wt_dir = STRBUF_INIT;

		strbuf_addstr(&wt_dir, store->base.gitdir);
		strbuf_addstr(&wt_dir, "/worktrees/");
		strbuf_add(&wt_dir, wtname, wtname_len);
		strbuf_addstr(&wt_dir, "/reftable");

		if (store->worktree_stack)
			reftable_stack_destroy(store->worktree_stack);
		store->err = reftable_new_stack(&store->worktree_stack,
						wt_dir.buf,
						store->write_options);
		assert(store->err != REFTABLE_API_ERROR);

		return store->worktree_stack;
	}
	if (!store->worktree_stack)
		return store->main_stack;

	switch (wt_type) {
	case REF_WORKTREE_CURRENT:
		return store->worktree_stack;
	default:
	case REF_WORKTREE_MAIN:
	case REF_WORKTREE_SHARED:
		return store->main_stack;
	}
}

static int should_log(const char *refname)
{
	return log_all_ref_updates != LOG_REFS_NONE &&
	       (log_all_ref_updates == LOG_REFS_ALWAYS ||
		log_all_ref_updates == LOG_REFS_UNSET ||
		should_autocreate_reflog(refname));
}

static const char *bare_ref_name(const char *ref)
{
	const char *stripped;
	parse_worktree_ref(ref, NULL, NULL, &stripped);
	return stripped;
}

static int git_reftable_read_raw_ref(struct ref_store *ref_store,
				     const char *refname, struct object_id *oid,
				     struct strbuf *referent,
				     unsigned int *type, int *failure_errno);

static void clear_reftable_log_record(struct reftable_log_record *log)
{
	switch (log->value_type) {
	case REFTABLE_LOG_UPDATE:
		/* when we write log records, the hashes are owned by a struct
		 * oid */
		log->value.update.old_hash = NULL;
		log->value.update.new_hash = NULL;
		break;
	case REFTABLE_LOG_DELETION:
		break;
	}
	reftable_log_record_release(log);
}

static void fill_reftable_log_record(struct reftable_log_record *log)
{
	const char *info = git_committer_info(0);
	struct ident_split split = { NULL };
	int result = split_ident_line(&split, info, strlen(info));
	int sign = 1;
	assert(0 == result);

	reftable_log_record_release(log);
	log->value_type = REFTABLE_LOG_UPDATE;
	log->value.update.name =
		xstrndup(split.name_begin, split.name_end - split.name_begin);
	log->value.update.email =
		xstrndup(split.mail_begin, split.mail_end - split.mail_begin);
	log->value.update.time = atol(split.date_begin);
	if (*split.tz_begin == '-') {
		sign = -1;
		split.tz_begin++;
	}
	if (*split.tz_begin == '+') {
		sign = 1;
		split.tz_begin++;
	}

	log->value.update.tz_offset = sign * atoi(split.tz_begin);
}

static int has_suffix(struct strbuf *b, const char *suffix)
{
	size_t len = strlen(suffix);

	if (len > b->len) {
		return 0;
	}

	return 0 == strncmp(b->buf + b->len - len, suffix, len);
}

/* trims the last path component of b. Returns -1 if it is not
 * present, or 0 on success
 */
static int trim_component(struct strbuf *b)
{
	char *last;
	last = strrchr(b->buf, '/');
	if (!last)
		return -1;
	strbuf_setlen(b, last - b->buf);
	return 0;
}

/* Returns whether `b` is a worktree path. Mutates its arg, trimming it to the
 * gitdir
 */
static int is_worktree(struct strbuf *b)
{
	if (trim_component(b) < 0) {
		return 0;
	}
	if (!has_suffix(b, "/worktrees")) {
		return 0;
	}
	trim_component(b);
	return 1;
}

static struct ref_store *git_reftable_ref_store_create(struct repository *repo,
						       const char *path,
						       unsigned int store_flags)
{
	struct git_reftable_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct ref_store *ref_store = (struct ref_store *)refs;
	struct strbuf sb = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;
	struct strbuf wt_buf = STRBUF_INIT;
	int wt = 0;
	int shared = get_shared_repository();
	if (shared < 0)
		shared = -shared;

	refs->write_options.block_size = 4096;
	refs->write_options.hash_id = the_hash_algo->format_id;
	if (shared && (shared & 0600))
		refs->write_options.default_permissions = shared;

	strbuf_realpath(&wt_buf, path, /*die_on_error=*/0);

	/* this is clumsy, but the official worktree functions (eg.
	 * get_worktrees()) function will try to initialize a ref storage
	 * backend, leading to infinite recursion.  */
	wt = is_worktree(&wt_buf);
	if (wt) {
		strbuf_addbuf(&gitdir, &wt_buf);
	} else {
		strbuf_realpath(&gitdir, path, /*die_on_error=*/0);
	}

	/* XXX should this use `path` or `gitdir.buf` ? */
	base_ref_store_init(ref_store, repo, path, &refs_be_reftable);
	refs->store_flags = store_flags;
	strbuf_addf(&sb, "%s/reftable", gitdir.buf);
	refs->reftable_dir = xstrdup(sb.buf);
	refs->base.repo = repo;
	strbuf_reset(&sb);

	refs->err = reftable_new_stack(&refs->main_stack, refs->reftable_dir,
				       refs->write_options);
	assert(refs->err != REFTABLE_API_ERROR);

	if (refs->err == 0 && wt) {
		strbuf_addf(&sb, "%s/reftable", path);

		refs->err = reftable_new_stack(&refs->worktree_stack, sb.buf,
					       refs->write_options);
		assert(refs->err != REFTABLE_API_ERROR);
	}

	strbuf_release(&sb);
	strbuf_release(&wt_buf);
	strbuf_release(&gitdir);
	return ref_store;
}

static int git_reftable_init_db(struct ref_store *ref_store, struct strbuf *err)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct strbuf sb = STRBUF_INIT;

	safe_create_dir(refs->reftable_dir, 1);

	strbuf_addf(&sb, "%s/HEAD", refs->base.gitdir);
	write_file(sb.buf, "ref: refs/heads/.invalid");
	adjust_shared_perm(sb.buf);
	strbuf_reset(&sb);

	strbuf_addf(&sb, "%s/refs", refs->base.gitdir);
	safe_create_dir(sb.buf, 1);
	strbuf_reset(&sb);

	strbuf_addf(&sb, "%s/refs/heads", refs->base.gitdir);
	write_file(sb.buf, "this repository uses the reftable format");
	adjust_shared_perm(sb.buf);

	strbuf_release(&sb);
	return 0;
}

struct git_reftable_iterator {
	struct ref_iterator base;
	struct reftable_iterator iter;
	struct reftable_ref_record ref;
	struct object_id oid;
	struct ref_store *ref_store;

	/* In case we must iterate over 2 stacks, this is non-null. */
	struct reftable_merged_table *merged;
	unsigned int flags;
	int err;
	const char *prefix;
};

static int reftable_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct git_reftable_iterator *ri =
		(struct git_reftable_iterator *)ref_iterator;
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ri->ref_store;

	while (ri->err == 0) {
		int signed_flags = 0;
		ri->err = reftable_iterator_next_ref(&ri->iter, &ri->ref);
		if (ri->err) {
			break;
		}

		ri->base.flags = 0;

		if (!strcmp(ri->ref.refname, "HEAD")) {
			/*
			  HEAD should not be produced by default.Other
			  pseudorefs (FETCH_HEAD etc.) shouldn't be
			  stored in reftables at all.
			 */
			continue;
		}
		ri->base.refname = ri->ref.refname;
		if (ri->prefix &&
		    strncmp(ri->prefix, ri->ref.refname, strlen(ri->prefix))) {
			ri->err = 1;
			break;
		}
		if (ri->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    parse_worktree_ref(ri->base.refname, NULL, NULL, NULL) !=
			    REF_WORKTREE_CURRENT)
			continue;

		if (ri->flags & DO_FOR_EACH_INCLUDE_BROKEN &&
		    check_refname_format(ri->base.refname,
					 REFNAME_ALLOW_ONELEVEL)) {
			/* This is odd, as REF_BAD_NAME and REF_ISBROKEN are
			   orthogonal, but it's what the spec says and the
			   files-backend does. */
			ri->base.flags |= REF_BAD_NAME | REF_ISBROKEN;
			break;
		}

		switch (ri->ref.value_type) {
		case REFTABLE_REF_VAL1:
			oidread(&ri->oid, ri->ref.value.val1);
			break;
		case REFTABLE_REF_VAL2:
			oidread(&ri->oid, ri->ref.value.val2.value);
			break;
		case REFTABLE_REF_SYMREF:
			ri->base.flags = REF_ISSYMREF;
			break;
		default:
			abort();
		}

		ri->base.oid = &ri->oid;
		if (!(ri->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(ri->base.refname, refs->base.repo,
					    ri->base.oid, ri->base.flags)) {
			continue;
		}

		/* Arguably, resolving recursively following symlinks should be
		 * lifted to refs.c because it is shared between reftable and
		 * the files backend, but it's here now.
		 */
		if (!refs_resolve_ref_unsafe(ri->ref_store, ri->ref.refname,
					     RESOLVE_REF_READING, &ri->oid,
					     &signed_flags)) {
			ri->base.flags = signed_flags;
			if (ri->ref.value_type == REFTABLE_REF_SYMREF &&
			    ri->flags & DO_FOR_EACH_OMIT_DANGLING_SYMREFS)
				continue;

			if (ri->ref.value_type == REFTABLE_REF_SYMREF &&
			    !(ri->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
			    (ri->base.flags & REF_ISBROKEN)) {
				continue;
			}

			if (is_null_oid(&ri->oid)) {
				oidclr(&ri->oid);
				ri->base.flags |= REF_ISBROKEN;
			}
		}
		break;
	}

	if (ri->err > 0) {
		return ITER_DONE;
	}
	if (ri->err < 0) {
		return ITER_ERROR;
	}

	return ITER_OK;
}

static int reftable_ref_iterator_peel(struct ref_iterator *ref_iterator,
				      struct object_id *peeled)
{
	struct git_reftable_iterator *ri =
		(struct git_reftable_iterator *)ref_iterator;
	if (ri->ref.value_type == REFTABLE_REF_VAL2) {
		oidread(peeled, ri->ref.value.val2.target_value);
		return 0;
	}

	return -1;
}

static int reftable_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct git_reftable_iterator *ri =
		(struct git_reftable_iterator *)ref_iterator;
	reftable_ref_record_release(&ri->ref);
	reftable_iterator_destroy(&ri->iter);
	if (ri->merged) {
		reftable_merged_table_free(ri->merged);
	}
	return 0;
}

static struct ref_iterator_vtable reftable_ref_iterator_vtable = {
	reftable_ref_iterator_advance, reftable_ref_iterator_peel,
	reftable_ref_iterator_abort
};

static struct ref_iterator *
git_reftable_ref_iterator_begin(struct ref_store *ref_store, const char *prefix,
				unsigned int flags)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct git_reftable_iterator *ri = xcalloc(1, sizeof(*ri));

	if (refs->err < 0) {
		ri->err = refs->err;
	} else if (!refs->worktree_stack) {
		struct reftable_merged_table *mt =
			reftable_stack_merged_table(refs->main_stack);
		ri->err = reftable_merged_table_seek_ref(mt, &ri->iter, prefix);
	} else {
		struct reftable_merged_table *mt1 =
			reftable_stack_merged_table(refs->main_stack);
		struct reftable_merged_table *mt2 =
			reftable_stack_merged_table(refs->worktree_stack);
		struct reftable_table *tabs =
			xcalloc(2, sizeof(struct reftable_table));
		reftable_table_from_merged_table(&tabs[0], mt1);
		reftable_table_from_merged_table(&tabs[1], mt2);

		/* XXX this isn't correct. This will merge reftables, which
		 * gives precedence to the most recently updated refs. We should
		 * give precedence to refs from the worktree / main stack
		 * depending on where we are
		 */
		ri->err = reftable_new_merged_table(&ri->merged, tabs, 2,
						    the_hash_algo->format_id);
		if (ri->err == 0)
			ri->err = reftable_merged_table_seek_ref(
				ri->merged, &ri->iter, prefix);
	}

	base_ref_iterator_init(&ri->base, &reftable_ref_iterator_vtable, 1);
	ri->prefix = prefix;
	ri->base.oid = &ri->oid;
	ri->flags = flags;
	ri->ref_store = ref_store;
	return &ri->base;
}

static int fixup_symrefs(struct ref_store *ref_store,
			 struct ref_transaction *transaction)
{
	struct strbuf referent = STRBUF_INIT;
	int i = 0;
	int err = 0;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct object_id old_oid;
		int failure_errno;

		err = git_reftable_read_raw_ref(ref_store, update->refname,
						&old_oid, &referent,
						/* mutate input, like
						   files-backend.c */
						&update->type, &failure_errno);
		if (err < 0 && failure_errno == ENOENT &&
		    is_null_oid(&update->old_oid)) {
			err = 0;
		}
		if (err < 0)
			goto done;

		if (!(update->type & REF_ISSYMREF))
			continue;

		if (update->flags & REF_NO_DEREF) {
			/* what should happen here? See files-backend.c
			 * lock_ref_for_update. */
		} else {
			/*
			  If we are updating a symref (eg. HEAD), we should also
			  update the branch that the symref points to.

			  This is generic functionality, and would be better
			  done in refs.c, but the current implementation is
			  intertwined with the locking in files-backend.c.
			*/
			int new_flags = update->flags;
			struct ref_update *new_update = NULL;

			/* if this is an update for HEAD, should also record a
			   log entry for HEAD? See files-backend.c,
			   split_head_update()
			*/
			new_update = ref_transaction_add_update(
				transaction, referent.buf, new_flags,
				&update->new_oid, &update->old_oid,
				update->msg);
			new_update->parent_update = update;

			/* files-backend sets REF_LOG_ONLY here. */
			update->flags |= REF_NO_DEREF | REF_LOG_ONLY;
			update->flags &= ~REF_HAVE_OLD;
		}
	}

done:
	assert(err != REFTABLE_API_ERROR);
	strbuf_release(&referent);
	return err;
}

static int git_reftable_transaction_prepare(struct ref_store *ref_store,
					    struct ref_transaction *transaction,
					    struct strbuf *errbuf)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_addition *add = NULL;
	struct reftable_stack *stack = stack_for(
		refs,
		transaction->nr ? transaction->updates[0]->refname : NULL);
	int i;

	int err = refs->err;
	if (err < 0) {
		goto done;
	}

	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	err = reftable_stack_new_addition(&add, stack);
	if (err) {
		goto done;
	}

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *u = transaction->updates[i];
		if ((u->flags & REF_HAVE_NEW) && !is_null_oid(&u->new_oid) &&
		    !(u->flags & REF_SKIP_OID_VERIFICATION) &&
		    !(u->flags & REF_LOG_ONLY)) {
			struct object *o =
				parse_object(refs->base.repo, &u->new_oid);
			if (!o) {
				strbuf_addf(
					errbuf,
					"trying to write ref '%s' with nonexistent object %s",
					u->refname, oid_to_hex(&u->new_oid));
				err = -1;
				goto done;
			}
		}
	}

	err = fixup_symrefs(ref_store, transaction);
	if (err) {
		goto done;
	}

	transaction->backend_data = add;
	transaction->state = REF_TRANSACTION_PREPARED;

done:
	assert(err != REFTABLE_API_ERROR);
	if (err < 0) {
		if (add) {
			reftable_addition_destroy(add);
			add = NULL;
		}
		transaction->state = REF_TRANSACTION_CLOSED;
		if (!errbuf->len)
			strbuf_addf(errbuf, "reftable: transaction prepare: %s",
				    reftable_error_str(err));
	}

	return err;
}

static int git_reftable_transaction_abort(struct ref_store *ref_store,
					  struct ref_transaction *transaction,
					  struct strbuf *err)
{
	struct reftable_addition *add =
		(struct reftable_addition *)transaction->backend_data;
	reftable_addition_destroy(add);
	transaction->backend_data = NULL;

	/* XXX. Shouldn't this be handled generically in refs.c? */
	transaction->state = REF_TRANSACTION_CLOSED;
	return 0;
}

static int reftable_check_old_oid(struct ref_store *refs, const char *refname,
				  struct object_id *want_oid)
{
	struct object_id out_oid;
	int out_flags = 0;
	const char *resolved = refs_resolve_ref_unsafe(
		refs, refname, RESOLVE_REF_READING, &out_oid, &out_flags);
	if (is_null_oid(want_oid) != !resolved) {
		return REFTABLE_LOCK_ERROR;
	}

	if (resolved && !oideq(&out_oid, want_oid)) {
		return REFTABLE_LOCK_ERROR;
	}

	return 0;
}

static int ref_update_cmp(const void *a, const void *b)
{
	return strcmp((*(struct ref_update **)a)->refname,
		      (*(struct ref_update **)b)->refname);
}

static int write_transaction_table(struct reftable_writer *writer, void *arg)
{
	struct ref_transaction *transaction = (struct ref_transaction *)arg;
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)transaction->ref_store;
	struct reftable_stack *stack =
		stack_for(refs, transaction->updates[0]->refname);
	uint64_t ts = reftable_stack_next_update_index(stack);
	int err = 0;
	int i = 0;
	int log_count = 0;
	struct reftable_log_record *logs =
		calloc(transaction->nr, sizeof(*logs));
	struct ref_update **sorted =
		malloc(transaction->nr * sizeof(struct ref_update *));
	struct reftable_merged_table *mt = reftable_stack_merged_table(stack);
	struct reftable_table tab = { NULL };
	struct reftable_ref_record ref = { NULL };
	reftable_table_from_merged_table(&tab, mt);
	COPY_ARRAY(sorted, transaction->updates, transaction->nr);
	QSORT(sorted, transaction->nr, ref_update_cmp);
	reftable_writer_set_limits(writer, ts, ts);

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *u = sorted[i];
		struct reftable_log_record *log = &logs[log_count];
		struct object_id old_id = *null_oid();

		log->value.update.new_hash = NULL;
		log->value.update.old_hash = NULL;
		if ((u->flags & REF_FORCE_CREATE_REFLOG) ||
		    should_log(u->refname))
			log_count++;
		fill_reftable_log_record(log);

		log->update_index = ts;
		log->value_type = REFTABLE_LOG_UPDATE;
		log->refname = xstrdup(u->refname);
		log->value.update.new_hash = u->new_oid.hash;
		log->value.update.message =
			xstrndup(u->msg, refs->write_options.block_size / 2);

		err = reftable_table_read_ref(&tab, u->refname, &ref);
		if (err < 0)
			goto done;
		else if (err > 0) {
			err = 0;
		}

		/* XXX if this is a symref (say, HEAD), should we deref the
		 * symref and check the update.old_hash against the referent? */
		if (ref.value_type == REFTABLE_REF_VAL2 ||
		    ref.value_type == REFTABLE_REF_VAL1)
			oidread(&old_id, ref.value.val1);

		/* XXX fold together with the old_id check below? */
		log->value.update.old_hash = old_id.hash;
		if (u->flags & REF_LOG_ONLY) {
			continue;
		}

		if (u->flags & REF_HAVE_NEW) {
			struct reftable_ref_record ref = { NULL };
			struct object_id peeled;

			int peel_error = peel_object(&u->new_oid, &peeled);
			ref.refname = (char *)u->refname;
			ref.update_index = ts;

			if (!peel_error) {
				ref.value_type = REFTABLE_REF_VAL2;
				ref.value.val2.target_value = peeled.hash;
				ref.value.val2.value = u->new_oid.hash;
			} else if (!is_null_oid(&u->new_oid)) {
				ref.value_type = REFTABLE_REF_VAL1;
				ref.value.val1 = u->new_oid.hash;
			}

			err = reftable_writer_add_ref(writer, &ref);
			if (err < 0) {
				goto done;
			}
		}
	}

	for (i = 0; i < log_count; i++) {
		err = reftable_writer_add_log(writer, &logs[i]);
		logs[i].value.update.new_hash = NULL;
		logs[i].value.update.old_hash = NULL;
		clear_reftable_log_record(&logs[i]);
		if (err < 0) {
			goto done;
		}
	}

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_ref_record_release(&ref);
	free(logs);
	free(sorted);
	return err;
}

static int git_reftable_transaction_finish(struct ref_store *ref_store,
					   struct ref_transaction *transaction,
					   struct strbuf *errmsg)
{
	struct reftable_addition *add =
		(struct reftable_addition *)transaction->backend_data;
	int err = 0;
	int i;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *u = transaction->updates[i];
		if (u->flags & REF_HAVE_OLD) {
			err = reftable_check_old_oid(transaction->ref_store,
						     u->refname, &u->old_oid);
			if (err < 0) {
				goto done;
			}
		}
	}
	if (transaction->nr) {
		err = reftable_addition_add(add, &write_transaction_table,
					    transaction);
		if (err < 0) {
			goto done;
		}
	}

	err = reftable_addition_commit(add);

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_addition_destroy(add);
	transaction->state = REF_TRANSACTION_CLOSED;
	transaction->backend_data = NULL;
	if (err) {
		strbuf_addf(errmsg, "reftable: transaction failure: %s",
			    reftable_error_str(err));
		return -1;
	}
	return err;
}

static int
git_reftable_transaction_initial_commit(struct ref_store *ref_store,
					struct ref_transaction *transaction,
					struct strbuf *errmsg)
{
	int err = git_reftable_transaction_prepare(ref_store, transaction,
						   errmsg);
	if (err)
		return err;

	return git_reftable_transaction_finish(ref_store, transaction, errmsg);
}

struct write_delete_refs_arg {
	struct git_reftable_ref_store *refs;
	struct reftable_stack *stack;
	struct string_list *refnames;
	const char *logmsg;
	unsigned int flags;
};

static int write_delete_refs_table(struct reftable_writer *writer, void *argv)
{
	struct write_delete_refs_arg *arg =
		(struct write_delete_refs_arg *)argv;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	int err = 0;
	int i = 0;

	reftable_writer_set_limits(writer, ts, ts);
	for (i = 0; i < arg->refnames->nr; i++) {
		struct reftable_ref_record ref = {
			.refname = (char *)arg->refnames->items[i].string,
			.value_type = REFTABLE_REF_DELETION,
			.update_index = ts,
		};
		err = reftable_writer_add_ref(writer, &ref);
		if (err < 0) {
			return err;
		}
	}

	for (i = 0; i < arg->refnames->nr; i++) {
		struct reftable_log_record log = {
			.update_index = ts,
		};
		struct reftable_ref_record current = { NULL };
		fill_reftable_log_record(&log);
		log.update_index = ts;
		log.refname = xstrdup(arg->refnames->items[i].string);
		if (!should_log(log.refname)) {
			continue;
		}
		log.value.update.message = xstrndup(
			arg->logmsg, arg->refs->write_options.block_size / 2);
		log.value.update.new_hash = NULL;
		log.value.update.old_hash = NULL;
		if (reftable_stack_read_ref(arg->stack, log.refname,
					    &current) == 0) {
			log.value.update.old_hash =
				reftable_ref_record_val1(&current);
		}
		err = reftable_writer_add_log(writer, &log);
		log.value.update.old_hash = NULL;
		reftable_ref_record_release(&current);

		clear_reftable_log_record(&log);
		if (err < 0) {
			return err;
		}
	}
	return 0;
}

static int git_reftable_delete_refs(struct ref_store *ref_store,
				    const char *msg,
				    struct string_list *refnames,
				    unsigned int flags)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(
		refs, refnames->nr ? refnames->items[0].string : NULL);
	struct write_delete_refs_arg arg = {
		.refs = refs,
		.stack = stack,
		.refnames = refnames,
		.logmsg = msg,
		.flags = flags,
	};
	int err = refs->err;
	if (err < 0) {
		goto done;
	}

	string_list_sort(refnames);
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}
	err = reftable_stack_add(stack, &write_delete_refs_table, &arg);
done:
	assert(err != REFTABLE_API_ERROR);
	return err;
}

static int git_reftable_pack_refs(struct ref_store *ref_store,
				  struct pack_refs_opts *opts)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;

	int err = refs->err;
	if (err < 0) {
		return err;
	}
	err = reftable_stack_compact_all(refs->main_stack, NULL);
	if (err == 0 && refs->worktree_stack)
		err = reftable_stack_compact_all(refs->worktree_stack, NULL);
	if (err == 0)
		err = reftable_stack_clean(refs->main_stack);
	if (err == 0 && refs->worktree_stack)
		err = reftable_stack_clean(refs->worktree_stack);

	return err;
}

struct write_create_symref_arg {
	struct git_reftable_ref_store *refs;
	struct reftable_stack *stack;
	const char *refname;
	const char *target;
	const char *logmsg;
};

static int write_create_symref_table(struct reftable_writer *writer, void *arg)
{
	struct write_create_symref_arg *create =
		(struct write_create_symref_arg *)arg;
	uint64_t ts = reftable_stack_next_update_index(create->stack);
	int err = 0;

	struct reftable_ref_record ref = {
		.refname = (char *)create->refname,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *)create->target,
		.update_index = ts,
	};
	reftable_writer_set_limits(writer, ts, ts);
	err = reftable_writer_add_ref(writer, &ref);
	if (err == 0) {
		struct reftable_log_record log = { NULL };
		struct object_id new_oid;
		struct object_id old_oid;

		fill_reftable_log_record(&log);
		log.refname = xstrdup(create->refname);
		if (!should_log(log.refname)) {
			return err;
		}
		log.update_index = ts;
		log.value.update.message =
			xstrndup(create->logmsg,
				 create->refs->write_options.block_size / 2);
		if (refs_resolve_ref_unsafe(
			    (struct ref_store *)create->refs, create->refname,
			    RESOLVE_REF_READING, &old_oid, NULL)) {
			log.value.update.old_hash = old_oid.hash;
		}

		if (refs_resolve_ref_unsafe((struct ref_store *)create->refs,
					    create->target, RESOLVE_REF_READING,
					    &new_oid, NULL)) {
			log.value.update.new_hash = new_oid.hash;
		}

		if (log.value.update.old_hash ||
		    log.value.update.new_hash) {
			err = reftable_writer_add_log(writer, &log);
		}
		log.refname = NULL;
		log.value.update.message = NULL;
		log.value.update.old_hash = NULL;
		log.value.update.new_hash = NULL;
		clear_reftable_log_record(&log);
	}
	return err;
}

static int git_reftable_create_symref(struct ref_store *ref_store,
				      const char *refname, const char *target,
				      const char *logmsg)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct write_create_symref_arg arg = { .refs = refs,
					       .stack = stack,
					       .refname = refname,
					       .target = target,
					       .logmsg = logmsg };
	int err = refs->err;
	if (err < 0) {
		goto done;
	}
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}
	err = reftable_stack_add(stack, &write_create_symref_table, &arg);
done:
	assert(err != REFTABLE_API_ERROR);
	return err;
}

struct write_rename_arg {
	struct git_reftable_ref_store *refs;
	struct reftable_stack *stack;
	const char *oldname;
	const char *newname;
	const char *logmsg;
};

static int write_rename_table(struct reftable_writer *writer, void *argv)
{
	struct write_rename_arg *arg = (struct write_rename_arg *)argv;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	struct reftable_ref_record old_ref = { NULL };
	struct reftable_ref_record new_ref = { NULL };
	int err = reftable_stack_read_ref(arg->stack, arg->oldname, &old_ref);
	struct reftable_ref_record todo[2] = {
		{
			.refname = (char *)arg->oldname,
			.update_index = ts,
			.value_type = REFTABLE_REF_DELETION,
		},
		old_ref,
	};

	if (err) {
		goto done;
	}

	/* git-branch supports a --force, but the check is not atomic. */
	if (!reftable_stack_read_ref(arg->stack, arg->newname, &new_ref)) {
		goto done;
	}

	reftable_writer_set_limits(writer, ts, ts);

	todo[1].update_index = ts;
	todo[1].refname = (char *)arg->newname;

	err = reftable_writer_add_refs(writer, todo, 2);
	if (err < 0) {
		goto done;
	}

	if (reftable_ref_record_val1(&old_ref)) {
		uint8_t *val1 = reftable_ref_record_val1(&old_ref);
		struct reftable_log_record todo[2] = { { NULL } };
		int firstlog = 0;
		int lastlog = 2;
		char *msg = xstrndup(arg->logmsg,
				     arg->refs->write_options.block_size / 2);
		fill_reftable_log_record(&todo[0]);
		fill_reftable_log_record(&todo[1]);

		todo[0].refname = xstrdup(arg->oldname);
		todo[0].update_index = ts;
		todo[0].value.update.message = msg;
		todo[0].value.update.old_hash = val1;
		todo[0].value.update.new_hash = NULL;

		todo[1].refname = xstrdup(arg->newname);
		todo[1].update_index = ts;
		todo[1].value.update.old_hash = NULL;
		todo[1].value.update.new_hash = val1;
		todo[1].value.update.message = xstrdup(msg);

		if (!should_log(todo[1].refname)) {
			lastlog--;
		}
		if (!should_log(todo[0].refname)) {
			firstlog++;
		}
		err = reftable_writer_add_logs(writer, &todo[firstlog],
					       lastlog - firstlog);

		clear_reftable_log_record(&todo[0]);
		clear_reftable_log_record(&todo[1]);
		if (err < 0) {
			goto done;
		}

	} else {
		/* XXX what should we write into the reflog if we rename a
		 * symref? */
	}

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_ref_record_release(&new_ref);
	reftable_ref_record_release(&old_ref);
	return err;
}

static int write_copy_table(struct reftable_writer *writer, void *argv)
{
	struct write_rename_arg *arg = (struct write_rename_arg *)argv;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	struct reftable_ref_record old_ref = { NULL };
	struct reftable_ref_record new_ref = { NULL };
	struct reftable_log_record log = { NULL };
	struct reftable_iterator it = { NULL };
	int err = reftable_stack_read_ref(arg->stack, arg->oldname, &old_ref);
	if (err) {
		goto done;
	}

	/* git-branch supports a --force, but the check is not atomic. */
	if (reftable_stack_read_ref(arg->stack, arg->newname, &new_ref) == 0) {
		goto done;
	}

	reftable_writer_set_limits(writer, ts, ts);

	FREE_AND_NULL(old_ref.refname);
	old_ref.refname = xstrdup(arg->newname);
	old_ref.update_index = ts;
	err = reftable_writer_add_ref(writer, &old_ref);
	if (err < 0) {
		goto done;
	}

	/* XXX this copies the entire reflog history. Is this the right
	 * semantics? should clear out existing reflog entries for oldname? */
	if (!should_log(arg->newname))
		goto done;

	err = reftable_merged_table_seek_log(
		reftable_stack_merged_table(arg->stack), &it, arg->oldname);
	if (err < 0) {
		goto done;
	}
	while (1) {
		int err = reftable_iterator_next_log(&it, &log);
		if (err < 0) {
			goto done;
		}

		if (err > 0 || strcmp(log.refname, arg->oldname)) {
			break;
		}
		FREE_AND_NULL(log.refname);
		log.refname = xstrdup(arg->newname);
		reftable_writer_add_log(writer, &log);
		reftable_log_record_release(&log);
	}

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_ref_record_release(&new_ref);
	reftable_ref_record_release(&old_ref);
	reftable_log_record_release(&log);
	reftable_iterator_destroy(&it);
	return err;
}

static int git_reftable_rename_ref(struct ref_store *ref_store,
				   const char *oldrefname,
				   const char *newrefname, const char *logmsg)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, newrefname);
	struct write_rename_arg arg = {
		.refs = refs,
		.stack = stack,
		.oldname = oldrefname,
		.newname = newrefname,
		.logmsg = logmsg,
	};
	int err = refs->err;
	if (err < 0) {
		goto done;
	}
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	err = reftable_stack_add(stack, &write_rename_table, &arg);
done:
	assert(err != REFTABLE_API_ERROR);
	return err;
}

static int git_reftable_copy_ref(struct ref_store *ref_store,
				 const char *oldrefname, const char *newrefname,
				 const char *logmsg)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, newrefname);
	struct write_rename_arg arg = {
		.refs = refs,
		.stack = stack,
		.oldname = oldrefname,
		.newname = newrefname,
		.logmsg = logmsg,
	};
	int err = refs->err;
	if (err < 0) {
		goto done;
	}
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	err = reftable_stack_add(stack, &write_copy_table, &arg);
done:
	assert(err != REFTABLE_API_ERROR);
	return err;
}

struct git_reftable_reflog_ref_iterator {
	struct ref_iterator base;
	struct reftable_iterator iter;
	struct reftable_log_record log;
	struct object_id oid;
	struct git_reftable_ref_store *refs;

	/* Used when iterating over worktree & main */
	struct reftable_merged_table *merged;
	char *last_name;
};

static int
git_reftable_reflog_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct git_reftable_reflog_ref_iterator *ri =
		(struct git_reftable_reflog_ref_iterator *)ref_iterator;

	while (1) {
		int flags = 0;
		int err = reftable_iterator_next_log(&ri->iter, &ri->log);

		if (err > 0) {
			return ITER_DONE;
		}
		if (err < 0) {
			return ITER_ERROR;
		}

		ri->base.refname = ri->log.refname;
		if (ri->last_name &&
		    !strcmp(ri->log.refname, ri->last_name)) {
			/* we want the refnames that we have reflogs for, so we
			 * skip if we've already produced this name. This could
			 * be faster by seeking directly to
			 * reflog@update_index==0.
			 */
			continue;
		}

		if (!refs_resolve_ref_unsafe(&ri->refs->base, ri->log.refname,
					     0, &ri->oid, &flags)) {
			error("bad ref for %s", ri->log.refname);
			continue;
		}

		free(ri->last_name);
		ri->last_name = xstrdup(ri->log.refname);
		ri->base.oid = &ri->oid;
		ri->base.flags = flags;
		return ITER_OK;
	}
}

static int
git_reftable_reflog_ref_iterator_peel(struct ref_iterator *ref_iterator,
				      struct object_id *peeled)
{
	BUG("not supported.");
	return -1;
}

static int
git_reftable_reflog_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct git_reftable_reflog_ref_iterator *ri =
		(struct git_reftable_reflog_ref_iterator *)ref_iterator;
	reftable_log_record_release(&ri->log);
	reftable_iterator_destroy(&ri->iter);
	if (ri->merged)
		reftable_merged_table_free(ri->merged);
	return 0;
}

static struct ref_iterator_vtable git_reftable_reflog_ref_iterator_vtable = {
	git_reftable_reflog_ref_iterator_advance,
	git_reftable_reflog_ref_iterator_peel,
	git_reftable_reflog_ref_iterator_abort
};

static struct ref_iterator *
git_reftable_reflog_iterator_begin(struct ref_store *ref_store)
{
	struct git_reftable_reflog_ref_iterator *ri = xcalloc(1, sizeof(*ri));
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;

	ri->refs = refs;
	if (!refs->worktree_stack) {
		struct reftable_stack *stack = refs->main_stack;
		struct reftable_merged_table *mt =
			reftable_stack_merged_table(stack);
		int err = reftable_merged_table_seek_log(mt, &ri->iter, "");
		if (err < 0) {
			free(ri);
			/* XXX how to handle errors in iterator_begin()? */
			return NULL;
		}
	} else {
		struct reftable_merged_table *mt1 =
			reftable_stack_merged_table(refs->main_stack);
		struct reftable_merged_table *mt2 =
			reftable_stack_merged_table(refs->worktree_stack);
		struct reftable_table *tabs =
			xcalloc(2, sizeof(struct reftable_table));
		int err = 0;
		reftable_table_from_merged_table(&tabs[0], mt1);
		reftable_table_from_merged_table(&tabs[1], mt2);
		err = reftable_new_merged_table(&ri->merged, tabs, 2,
						the_hash_algo->format_id);
		if (err < 0) {
			free(tabs);
			/* XXX idem. */
			return NULL;
		}
		err = reftable_merged_table_seek_log(ri->merged, &ri->iter, "");
		if (err < 0) {
			return NULL;
		}
	}
	base_ref_iterator_init(&ri->base,
			       &git_reftable_reflog_ref_iterator_vtable, 1);
	ri->base.oid = &ri->oid;

	return (struct ref_iterator *)ri;
}

static int git_reftable_for_each_reflog_ent_newest_first(
	struct ref_store *ref_store, const char *refname, each_reflog_ent_fn fn,
	void *cb_data)
{
	struct reftable_iterator it = { NULL };
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_merged_table *mt = NULL;
	int err = 0;
	struct reftable_log_record log = { NULL };

	if (refs->err < 0) {
		return refs->err;
	}
	refname = bare_ref_name(refname);

	mt = reftable_stack_merged_table(stack);
	err = reftable_merged_table_seek_log(mt, &it, refname);
	while (err == 0) {
		struct object_id old_oid;
		struct object_id new_oid;
		const char *full_committer = "";

		err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0) {
			break;
		}

		if (strcmp(log.refname, refname)) {
			break;
		}

		oidread(&old_oid, log.value.update.old_hash);
		oidread(&new_oid, log.value.update.new_hash);

		if (is_null_oid(&old_oid) && is_null_oid(&new_oid)) {
			/* placeholder for existence. */
			continue;
		}

		full_committer = fmt_ident(log.value.update.name,
					   log.value.update.email,
					   WANT_COMMITTER_IDENT,
					   /*date*/ NULL, IDENT_NO_DATE);
		err = fn(&old_oid, &new_oid, full_committer,
			 log.value.update.time, log.value.update.tz_offset,
			 log.value.update.message, cb_data);
		if (err)
			break;
	}

	reftable_log_record_release(&log);
	reftable_iterator_destroy(&it);
	return err;
}

static int git_reftable_for_each_reflog_ent_oldest_first(
	struct ref_store *ref_store, const char *refname, each_reflog_ent_fn fn,
	void *cb_data)
{
	struct reftable_iterator it = { NULL };
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_merged_table *mt = NULL;
	struct reftable_log_record *logs = NULL;
	int cap = 0;
	int len = 0;
	int err = 0;
	int i = 0;

	if (refs->err < 0) {
		return refs->err;
	}
	refname = bare_ref_name(refname);
	mt = reftable_stack_merged_table(stack);
	err = reftable_merged_table_seek_log(mt, &it, refname);

	while (err == 0) {
		struct reftable_log_record log = { NULL };
		err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0) {
			break;
		}

		if (strcmp(log.refname, refname)) {
			break;
		}

		if (len == cap) {
			cap = 2 * cap + 1;
			logs = realloc(logs, cap * sizeof(*logs));
		}

		logs[len++] = log;
	}

	for (i = len; i--;) {
		struct reftable_log_record *log = &logs[i];
		struct object_id old_oid;
		struct object_id new_oid;
		const char *full_committer = "";

		oidread(&old_oid, log->value.update.old_hash);
		oidread(&new_oid, log->value.update.new_hash);

		if (is_null_oid(&old_oid) && is_null_oid(&new_oid)) {
			/* placeholder for existence. */
			continue;
		}

		full_committer = fmt_ident(log->value.update.name,
					   log->value.update.email,
					   WANT_COMMITTER_IDENT, NULL,
					   IDENT_NO_DATE);
		err = fn(&old_oid, &new_oid, full_committer,
			 log->value.update.time, log->value.update.tz_offset,
			 log->value.update.message, cb_data);
		if (err) {
			break;
		}
	}

	for (i = 0; i < len; i++) {
		reftable_log_record_release(&logs[i]);
	}
	free(logs);

	reftable_iterator_destroy(&it);
	return err;
}

static int git_reftable_reflog_exists(struct ref_store *ref_store,
				      const char *refname)
{
	struct reftable_iterator it = { NULL };
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_merged_table *mt = reftable_stack_merged_table(stack);
	struct reftable_log_record log = { NULL };
	int err = refs->err;

	if (err < 0) {
		goto done;
	}

	refname = bare_ref_name(refname);
	err = reftable_merged_table_seek_log(mt, &it, refname);
	if (err) {
		goto done;
	}
	err = reftable_iterator_next_log(&it, &log);
	if (err) {
		goto done;
	}

	if (strcmp(log.refname, refname)) {
		err = 1;
	}

done:
	reftable_iterator_destroy(&it);
	reftable_log_record_release(&log);
	return !err;
}

struct write_reflog_existence_arg {
	struct git_reftable_ref_store *refs;
	const char *refname;
	struct reftable_stack *stack;
};

static int write_reflog_existence_table(struct reftable_writer *writer,
					void *argv)
{
	struct write_reflog_existence_arg *arg =
		(struct write_reflog_existence_arg *)argv;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	struct reftable_log_record log = { NULL };

	int err = reftable_stack_read_log(arg->stack, arg->refname, &log);
	if (err <= 0) {
		goto done;
	}

	reftable_writer_set_limits(writer, ts, ts);

	log.refname = (char *)arg->refname;
	log.update_index = ts;
	log.value_type = REFTABLE_LOG_UPDATE;
	err = reftable_writer_add_log(writer, &log);

	/* field is not malloced */
	log.refname = NULL;

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_log_record_release(&log);
	return err;
}

static int git_reftable_create_reflog(struct ref_store *ref_store,
				      const char *refname,
				      struct strbuf *errmsg)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct write_reflog_existence_arg arg = {
		.refs = refs,
		.stack = stack,
		.refname = refname,
	};
	int err = refs->err;
	if (err < 0) {
		goto done;
	}

	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	err = reftable_stack_add(stack, &write_reflog_existence_table, &arg);

done:
	return err;
}

struct write_reflog_delete_arg {
	struct reftable_stack *stack;
	const char *refname;
};

static int write_reflog_delete_table(struct reftable_writer *writer, void *argv)
{
	struct write_reflog_delete_arg *arg = argv;
	struct reftable_merged_table *mt =
		reftable_stack_merged_table(arg->stack);
	struct reftable_log_record log = { NULL };
	struct reftable_iterator it = { NULL };
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	int err = reftable_merged_table_seek_log(mt, &it, arg->refname);

	reftable_writer_set_limits(writer, ts, ts);
	while (err == 0) {
		struct reftable_log_record tombstone = {
			.refname = (char *)arg->refname,
			.update_index = REFTABLE_LOG_DELETION,
		};
		err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			err = 0;
			break;
		}

		if (err < 0 || strcmp(log.refname, arg->refname)) {
			break;
		}
		if (log.value_type == REFTABLE_LOG_DELETION)
			continue;

		tombstone.update_index = log.update_index;
		err = reftable_writer_add_log(writer, &tombstone);
	}

	reftable_log_record_release(&log);
	return err;
}

static int git_reftable_delete_reflog(struct ref_store *ref_store,
				      const char *refname)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct write_reflog_delete_arg arg = {
		.stack = stack,
		.refname = refname,
	};
	int err = reftable_stack_add(stack, &write_reflog_delete_table, &arg);
	assert(err != REFTABLE_API_ERROR);
	return err;
}

struct reflog_expiry_arg {
	struct reftable_stack *stack;
	struct reftable_log_record *records;
	int len;
	const char *refname;
};

static int write_reflog_expiry_table(struct reftable_writer *writer, void *argv)
{
	struct reflog_expiry_arg *arg = (struct reflog_expiry_arg *)argv;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	int i = 0;
	int live_records = 0;
	uint64_t max_ts = 0;
	for (i = 0; i < arg->len; i++) {
		if (arg->records[i].value_type == REFTABLE_LOG_UPDATE)
			live_records++;

		if (max_ts < arg->records[i].update_index)
			max_ts = arg->records[i].update_index;
	}

	reftable_writer_set_limits(writer, ts, ts);
	if (live_records == 0) {
		struct reftable_log_record log = {
			.refname = (char *)arg->refname,
			.update_index = max_ts + 1,
			.value_type = REFTABLE_LOG_UPDATE,
			/* existence dummy has null new/old oid */
		};
		int err;
		if (log.update_index < ts)
			log.update_index = ts;

		err = reftable_writer_add_log(writer, &log);
		if (err) {
			return err;
		}
	}

	for (i = 0; i < arg->len; i++) {
		int err = reftable_writer_add_log(writer, &arg->records[i]);
		if (err) {
			return err;
		}
	}
	return 0;
}

static int git_reftable_reflog_expire(
	struct ref_store *ref_store, const char *refname, unsigned int flags,
	reflog_expiry_prepare_fn prepare_fn,
	reflog_expiry_should_prune_fn should_prune_fn,
	reflog_expiry_cleanup_fn cleanup_fn, void *policy_cb_data)
{
	/*
	  For log expiry, we write tombstones in place of the expired entries,
	  This means that the entries are still retrievable by delving into the
	  stack, and expiring entries paradoxically takes extra memory.

	  This memory is only reclaimed when some operation issues a
	  git_reftable_pack_refs(), which will compact the entire stack and get
	  rid of deletion entries.

	  It would be better if the refs backend supported an API that sets a
	  criterion for all refs, passing the criterion to pack_refs().

	  On the plus side, because we do the expiration per ref, we can easily
	  insert the reflog existence dummies.
	*/
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_merged_table *mt = NULL;
	struct reflog_expiry_arg arg = {
		.stack = stack,
		.refname = refname,
	};
	struct reftable_log_record *logs = NULL;
	struct reftable_log_record *rewritten = NULL;
	struct reftable_ref_record ref_record = { NULL };
	int logs_len = 0;
	int logs_cap = 0;
	int i = 0;
	uint8_t *last_hash = NULL;
	struct reftable_iterator it = { NULL };
	struct reftable_addition *add = NULL;
	int err = 0;
	struct object_id oid = { 0 };
	if (refs->err < 0) {
		return refs->err;
	}
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	mt = reftable_stack_merged_table(stack);
	err = reftable_merged_table_seek_log(mt, &it, refname);
	if (err < 0) {
		goto done;
	}

	err = reftable_stack_new_addition(&add, stack);
	if (err) {
		goto done;
	}
	if (!reftable_stack_read_ref(stack, refname, &ref_record)) {
		uint8_t *hash = reftable_ref_record_val1(&ref_record);
		if (hash)
			oidread(&oid, hash);
	}

	prepare_fn(refname, &oid, policy_cb_data);
	while (1) {
		struct reftable_log_record log = { NULL };
		int err = reftable_iterator_next_log(&it, &log);
		if (err < 0) {
			goto done;
		}

		if (err > 0 || strcmp(log.refname, refname)) {
			break;
		}

		if (logs_len >= logs_cap) {
			int new_cap = logs_cap * 2 + 1;
			logs = realloc(logs, new_cap * sizeof(*logs));
			logs_cap = new_cap;
		}
		logs[logs_len++] = log;
	}

	rewritten = calloc(logs_len, sizeof(*rewritten));
	for (i = logs_len - 1; i >= 0; i--) {
		struct object_id ooid;
		struct object_id noid;
		struct reftable_log_record *dest = &rewritten[i];

		*dest = logs[i];
		oidread(&ooid, logs[i].value.update.old_hash);
		oidread(&noid, logs[i].value.update.new_hash);

		if (should_prune_fn(&ooid, &noid, logs[i].value.update.email,
				    (timestamp_t)logs[i].value.update.time,
				    logs[i].value.update.tz_offset,
				    logs[i].value.update.message,
				    policy_cb_data)) {
			dest->value_type = REFTABLE_LOG_DELETION;
		} else {
			if ((flags & EXPIRE_REFLOGS_REWRITE) &&
			    last_hash) {
				dest->value.update.old_hash = last_hash;
			}
			last_hash = logs[i].value.update.new_hash;
		}
	}

	arg.records = rewritten;
	arg.len = logs_len;
	err = reftable_addition_add(add, &write_reflog_expiry_table, &arg);
	if (err < 0) {
		goto done;
	}

	if (!(flags & EXPIRE_REFLOGS_DRY_RUN)) {
		/* future improvement: we could skip writing records that were
		 * not changed. */
		err = reftable_addition_commit(add);
	}

done:
	if (add) {
		cleanup_fn(policy_cb_data);
	}
	assert(err != REFTABLE_API_ERROR);
	reftable_addition_destroy(add);
	for (i = 0; i < logs_len; i++)
		reftable_log_record_release(&logs[i]);
	free(logs);
	free(rewritten);
	reftable_iterator_destroy(&it);
	return err;
}

static int git_reftable_read_raw_ref(struct ref_store *ref_store,
				     const char *refname, struct object_id *oid,
				     struct strbuf *referent,
				     unsigned int *type, int *failure_errno)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_ref_record ref = { NULL };
	int err = 0;

	refname = bare_ref_name(refname); /* XXX - in which other cases should
					     we do this? */
	if (refs->err < 0) {
		return refs->err;
	}

	/* This is usually not needed, but Git doesn't signal to ref backend if
	   a subprocess updated the ref DB.  So we always check.
	*/
	err = reftable_stack_reload(stack);
	if (err) {
		goto done;
	}

	err = reftable_stack_read_ref(stack, refname, &ref);
	if (err > 0) {
		*failure_errno = ENOENT;
		err = -1;
		goto done;
	}
	if (err < 0) {
		goto done;
	}

	if (ref.value_type == REFTABLE_REF_SYMREF) {
		strbuf_reset(referent);
		strbuf_addstr(referent, ref.value.symref);
		*type |= REF_ISSYMREF;
	} else if (reftable_ref_record_val1(&ref)) {
		oidread(oid, reftable_ref_record_val1(&ref));
	} else {
		/* We got a tombstone, which should not happen. */
		BUG("Got reftable_ref_record with value type %d",
		    ref.value_type);
	}

done:
	assert(err != REFTABLE_API_ERROR);
	reftable_ref_record_release(&ref);
	return err;
}

static int git_reftable_read_symbolic_ref(struct ref_store *ref_store,
					  const char *refname,
					  struct strbuf *referent)
{
	struct git_reftable_ref_store *refs =
		(struct git_reftable_ref_store *)ref_store;
	struct reftable_stack *stack = stack_for(refs, refname);
	struct reftable_ref_record ref = { NULL };
	int err = 0;

	err = reftable_stack_read_ref(stack, refname, &ref);
	if (err == 0 && ref.value_type == REFTABLE_REF_SYMREF) {
		strbuf_addstr(referent, ref.value.symref);
	} else {
		err = -1;
	}

	reftable_ref_record_release(&ref);
	return err;
}

struct ref_storage_be refs_be_reftable = {
	.next = &refs_be_files,
	.name = "reftable",
	.init = git_reftable_ref_store_create,
	.init_db = git_reftable_init_db,
	.transaction_prepare = git_reftable_transaction_prepare,
	.transaction_finish = git_reftable_transaction_finish,
	.transaction_abort = git_reftable_transaction_abort,
	.initial_transaction_commit = git_reftable_transaction_initial_commit,

	.pack_refs = git_reftable_pack_refs,
	.create_symref = git_reftable_create_symref,
	.delete_refs = git_reftable_delete_refs,
	.rename_ref = git_reftable_rename_ref,
	.copy_ref = git_reftable_copy_ref,

	.iterator_begin = git_reftable_ref_iterator_begin,
	.read_raw_ref = git_reftable_read_raw_ref,
	.read_symbolic_ref = git_reftable_read_symbolic_ref,

	.reflog_iterator_begin = git_reftable_reflog_iterator_begin,
	.for_each_reflog_ent = git_reftable_for_each_reflog_ent_oldest_first,
	.for_each_reflog_ent_reverse =
		git_reftable_for_each_reflog_ent_newest_first,
	.reflog_exists = git_reftable_reflog_exists,
	.create_reflog = git_reftable_create_reflog,
	.delete_reflog = git_reftable_delete_reflog,
	.reflog_expire = git_reftable_reflog_expire,
};
