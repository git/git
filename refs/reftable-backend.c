#include "../git-compat-util.h"
#include "../abspath.h"
#include "../chdir-notify.h"
#include "../environment.h"
#include "../gettext.h"
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
#include "../reftable/reftable-iterator.h"
#include "../reftable/reftable-merged.h"
#include "../setup.h"
#include "../strmap.h"
#include "parse.h"
#include "refs-internal.h"

/*
 * Used as a flag in ref_update::flags when the ref_update was via an
 * update to HEAD.
 */
#define REF_UPDATE_VIA_HEAD (1 << 8)

struct reftable_ref_store {
	struct ref_store base;

	/*
	 * The main stack refers to the common dir and thus contains common
	 * refs as well as refs of the main repository.
	 */
	struct reftable_stack *main_stack;
	/*
	 * The worktree stack refers to the gitdir in case the refdb is opened
	 * via a worktree. It thus contains the per-worktree refs.
	 */
	struct reftable_stack *worktree_stack;
	/*
	 * Map of worktree stacks by their respective worktree names. The map
	 * is populated lazily when we try to resolve `worktrees/$worktree` refs.
	 */
	struct strmap worktree_stacks;
	struct reftable_write_options write_options;

	unsigned int store_flags;
	int err;
};

/*
 * Downcast ref_store to reftable_ref_store. Die if ref_store is not a
 * reftable_ref_store. required_flags is compared with ref_store's store_flags
 * to ensure the ref_store has all required capabilities. "caller" is used in
 * any necessary error messages.
 */
static struct reftable_ref_store *reftable_be_downcast(struct ref_store *ref_store,
						       unsigned int required_flags,
						       const char *caller)
{
	struct reftable_ref_store *refs;

	if (ref_store->be != &refs_be_reftable)
		BUG("ref_store is type \"%s\" not \"reftables\" in %s",
		    ref_store->be->name, caller);

	refs = (struct reftable_ref_store *)ref_store;

	if ((refs->store_flags & required_flags) != required_flags)
		BUG("operation %s requires abilities 0x%x, but only have 0x%x",
		    caller, required_flags, refs->store_flags);

	return refs;
}

/*
 * Some refs are global to the repository (refs/heads/{*}), while others are
 * local to the worktree (eg. HEAD, refs/bisect/{*}). We solve this by having
 * multiple separate databases (ie. multiple reftable/ directories), one for
 * the shared refs, one for the current worktree refs, and one for each
 * additional worktree. For reading, we merge the view of both the shared and
 * the current worktree's refs, when necessary.
 *
 * This function also optionally assigns the rewritten reference name that is
 * local to the stack. This translation is required when using worktree refs
 * like `worktrees/$worktree/refs/heads/foo` as worktree stacks will store
 * those references in their normalized form.
 */
static struct reftable_stack *stack_for(struct reftable_ref_store *store,
					const char *refname,
					const char **rewritten_ref)
{
	const char *wtname;
	int wtname_len;

	if (!refname)
		return store->main_stack;

	switch (parse_worktree_ref(refname, &wtname, &wtname_len, rewritten_ref)) {
	case REF_WORKTREE_OTHER: {
		static struct strbuf wtname_buf = STRBUF_INIT;
		struct strbuf wt_dir = STRBUF_INIT;
		struct reftable_stack *stack;

		/*
		 * We're using a static buffer here so that we don't need to
		 * allocate the worktree name whenever we look up a reference.
		 * This could be avoided if the strmap interface knew how to
		 * handle keys with a length.
		 */
		strbuf_reset(&wtname_buf);
		strbuf_add(&wtname_buf, wtname, wtname_len);

		/*
		 * There is an edge case here: when the worktree references the
		 * current worktree, then we set up the stack once via
		 * `worktree_stacks` and once via `worktree_stack`. This is
		 * wasteful, but in the reading case it shouldn't matter. And
		 * in the writing case we would notice that the stack is locked
		 * already and error out when trying to write a reference via
		 * both stacks.
		 */
		stack = strmap_get(&store->worktree_stacks, wtname_buf.buf);
		if (!stack) {
			strbuf_addf(&wt_dir, "%s/worktrees/%s/reftable",
				    store->base.repo->commondir, wtname_buf.buf);

			store->err = reftable_new_stack(&stack, wt_dir.buf,
							store->write_options);
			assert(store->err != REFTABLE_API_ERROR);
			strmap_put(&store->worktree_stacks, wtname_buf.buf, stack);
		}

		strbuf_release(&wt_dir);
		return stack;
	}
	case REF_WORKTREE_CURRENT:
		/*
		 * If there is no worktree stack then we're currently in the
		 * main worktree. We thus return the main stack in that case.
		 */
		if (!store->worktree_stack)
			return store->main_stack;
		return store->worktree_stack;
	case REF_WORKTREE_MAIN:
	case REF_WORKTREE_SHARED:
		return store->main_stack;
	default:
		BUG("unhandled worktree reference type");
	}
}

static int should_write_log(struct ref_store *refs, const char *refname)
{
	if (log_all_ref_updates == LOG_REFS_UNSET)
		log_all_ref_updates = is_bare_repository() ? LOG_REFS_NONE : LOG_REFS_NORMAL;

	switch (log_all_ref_updates) {
	case LOG_REFS_NONE:
		return refs_reflog_exists(refs, refname);
	case LOG_REFS_ALWAYS:
		return 1;
	case LOG_REFS_NORMAL:
		if (should_autocreate_reflog(refname))
			return 1;
		return refs_reflog_exists(refs, refname);
	default:
		BUG("unhandled core.logAllRefUpdates value %d", log_all_ref_updates);
	}
}

static void fill_reftable_log_record(struct reftable_log_record *log, const struct ident_split *split)
{
	const char *tz_begin;
	int sign = 1;

	reftable_log_record_release(log);
	log->value_type = REFTABLE_LOG_UPDATE;
	log->value.update.name =
		xstrndup(split->name_begin, split->name_end - split->name_begin);
	log->value.update.email =
		xstrndup(split->mail_begin, split->mail_end - split->mail_begin);
	log->value.update.time = atol(split->date_begin);

	tz_begin = split->tz_begin;
	if (*tz_begin == '-') {
		sign = -1;
		tz_begin++;
	}
	if (*tz_begin == '+') {
		sign = 1;
		tz_begin++;
	}

	log->value.update.tz_offset = sign * atoi(tz_begin);
}

static int read_ref_without_reload(struct reftable_stack *stack,
				   const char *refname,
				   struct object_id *oid,
				   struct strbuf *referent,
				   unsigned int *type)
{
	struct reftable_ref_record ref = {0};
	int ret;

	ret = reftable_stack_read_ref(stack, refname, &ref);
	if (ret)
		goto done;

	if (ref.value_type == REFTABLE_REF_SYMREF) {
		strbuf_reset(referent);
		strbuf_addstr(referent, ref.value.symref);
		*type |= REF_ISSYMREF;
	} else if (reftable_ref_record_val1(&ref)) {
		oidread(oid, reftable_ref_record_val1(&ref));
	} else {
		/* We got a tombstone, which should not happen. */
		BUG("unhandled reference value type %d", ref.value_type);
	}

done:
	assert(ret != REFTABLE_API_ERROR);
	reftable_ref_record_release(&ref);
	return ret;
}

static struct ref_store *reftable_be_init(struct repository *repo,
					  const char *gitdir,
					  unsigned int store_flags)
{
	struct reftable_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct strbuf path = STRBUF_INIT;
	int is_worktree;
	mode_t mask;

	mask = umask(0);
	umask(mask);

	base_ref_store_init(&refs->base, repo, gitdir, &refs_be_reftable);
	strmap_init(&refs->worktree_stacks);
	refs->store_flags = store_flags;
	refs->write_options.block_size = 4096;
	refs->write_options.hash_id = repo->hash_algo->format_id;
	refs->write_options.default_permissions = calc_shared_perm(0666 & ~mask);
	refs->write_options.disable_auto_compact =
		!git_env_bool("GIT_TEST_REFTABLE_AUTOCOMPACTION", 1);

	/*
	 * Set up the main reftable stack that is hosted in GIT_COMMON_DIR.
	 * This stack contains both the shared and the main worktree refs.
	 *
	 * Note that we don't try to resolve the path in case we have a
	 * worktree because `get_common_dir_noenv()` already does it for us.
	 */
	is_worktree = get_common_dir_noenv(&path, gitdir);
	if (!is_worktree) {
		strbuf_reset(&path);
		strbuf_realpath(&path, gitdir, 0);
	}
	strbuf_addstr(&path, "/reftable");
	refs->err = reftable_new_stack(&refs->main_stack, path.buf,
				       refs->write_options);
	if (refs->err)
		goto done;

	/*
	 * If we're in a worktree we also need to set up the worktree reftable
	 * stack that is contained in the per-worktree GIT_DIR.
	 *
	 * Ideally, we would also add the stack to our worktree stack map. But
	 * we have no way to figure out the worktree name here and thus can't
	 * do it efficiently.
	 */
	if (is_worktree) {
		strbuf_reset(&path);
		strbuf_addf(&path, "%s/reftable", gitdir);

		refs->err = reftable_new_stack(&refs->worktree_stack, path.buf,
					       refs->write_options);
		if (refs->err)
			goto done;
	}

	chdir_notify_reparent("reftables-backend $GIT_DIR", &refs->base.gitdir);

done:
	assert(refs->err != REFTABLE_API_ERROR);
	strbuf_release(&path);
	return &refs->base;
}

static int reftable_be_init_db(struct ref_store *ref_store,
			       int flags UNUSED,
			       struct strbuf *err UNUSED)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "init_db");
	struct strbuf sb = STRBUF_INIT;

	strbuf_addf(&sb, "%s/reftable", refs->base.gitdir);
	safe_create_dir(sb.buf, 1);
	strbuf_reset(&sb);

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

struct reftable_ref_iterator {
	struct ref_iterator base;
	struct reftable_ref_store *refs;
	struct reftable_iterator iter;
	struct reftable_ref_record ref;
	struct object_id oid;

	const char *prefix;
	size_t prefix_len;
	unsigned int flags;
	int err;
};

static int reftable_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct reftable_ref_iterator *iter =
		(struct reftable_ref_iterator *)ref_iterator;
	struct reftable_ref_store *refs = iter->refs;

	while (!iter->err) {
		int flags = 0;

		iter->err = reftable_iterator_next_ref(&iter->iter, &iter->ref);
		if (iter->err)
			break;

		/*
		 * The files backend only lists references contained in "refs/" unless
		 * the root refs are to be included. We emulate the same behaviour here.
		 */
		if (!starts_with(iter->ref.refname, "refs/") &&
		    !(iter->flags & DO_FOR_EACH_INCLUDE_ROOT_REFS &&
		     (is_pseudoref(&iter->refs->base, iter->ref.refname) ||
		      is_headref(&iter->refs->base, iter->ref.refname)))) {
			continue;
		}

		if (iter->prefix_len &&
		    strncmp(iter->prefix, iter->ref.refname, iter->prefix_len)) {
			iter->err = 1;
			break;
		}

		if (iter->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    parse_worktree_ref(iter->ref.refname, NULL, NULL, NULL) !=
			    REF_WORKTREE_CURRENT)
			continue;

		switch (iter->ref.value_type) {
		case REFTABLE_REF_VAL1:
			oidread(&iter->oid, iter->ref.value.val1);
			break;
		case REFTABLE_REF_VAL2:
			oidread(&iter->oid, iter->ref.value.val2.value);
			break;
		case REFTABLE_REF_SYMREF:
			if (!refs_resolve_ref_unsafe(&iter->refs->base, iter->ref.refname,
						     RESOLVE_REF_READING, &iter->oid, &flags))
				oidclr(&iter->oid);
			break;
		default:
			BUG("unhandled reference value type %d", iter->ref.value_type);
		}

		if (is_null_oid(&iter->oid))
			flags |= REF_ISBROKEN;

		if (check_refname_format(iter->ref.refname, REFNAME_ALLOW_ONELEVEL)) {
			if (!refname_is_safe(iter->ref.refname))
				die(_("refname is dangerous: %s"), iter->ref.refname);
			oidclr(&iter->oid);
			flags |= REF_BAD_NAME | REF_ISBROKEN;
		}

		if (iter->flags & DO_FOR_EACH_OMIT_DANGLING_SYMREFS &&
		    flags & REF_ISSYMREF &&
		    flags & REF_ISBROKEN)
			continue;

		if (!(iter->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(iter->ref.refname, refs->base.repo,
					    &iter->oid, flags))
				continue;

		iter->base.refname = iter->ref.refname;
		iter->base.oid = &iter->oid;
		iter->base.flags = flags;

		break;
	}

	if (iter->err > 0) {
		if (ref_iterator_abort(ref_iterator) != ITER_DONE)
			return ITER_ERROR;
		return ITER_DONE;
	}

	if (iter->err < 0) {
		ref_iterator_abort(ref_iterator);
		return ITER_ERROR;
	}

	return ITER_OK;
}

static int reftable_ref_iterator_peel(struct ref_iterator *ref_iterator,
				      struct object_id *peeled)
{
	struct reftable_ref_iterator *iter =
		(struct reftable_ref_iterator *)ref_iterator;

	if (iter->ref.value_type == REFTABLE_REF_VAL2) {
		oidread(peeled, iter->ref.value.val2.target_value);
		return 0;
	}

	return -1;
}

static int reftable_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct reftable_ref_iterator *iter =
		(struct reftable_ref_iterator *)ref_iterator;
	reftable_ref_record_release(&iter->ref);
	reftable_iterator_destroy(&iter->iter);
	free(iter);
	return ITER_DONE;
}

static struct ref_iterator_vtable reftable_ref_iterator_vtable = {
	.advance = reftable_ref_iterator_advance,
	.peel = reftable_ref_iterator_peel,
	.abort = reftable_ref_iterator_abort
};

static struct reftable_ref_iterator *ref_iterator_for_stack(struct reftable_ref_store *refs,
							    struct reftable_stack *stack,
							    const char *prefix,
							    int flags)
{
	struct reftable_merged_table *merged_table;
	struct reftable_ref_iterator *iter;
	int ret;

	iter = xcalloc(1, sizeof(*iter));
	base_ref_iterator_init(&iter->base, &reftable_ref_iterator_vtable);
	iter->prefix = prefix;
	iter->prefix_len = prefix ? strlen(prefix) : 0;
	iter->base.oid = &iter->oid;
	iter->flags = flags;
	iter->refs = refs;

	ret = refs->err;
	if (ret)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret)
		goto done;

	merged_table = reftable_stack_merged_table(stack);

	ret = reftable_merged_table_seek_ref(merged_table, &iter->iter, prefix);
	if (ret)
		goto done;

done:
	iter->err = ret;
	return iter;
}

static struct ref_iterator *reftable_be_iterator_begin(struct ref_store *ref_store,
						       const char *prefix,
						       const char **exclude_patterns,
						       unsigned int flags)
{
	struct reftable_ref_iterator *main_iter, *worktree_iter;
	struct reftable_ref_store *refs;
	unsigned int required_flags = REF_STORE_READ;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN))
		required_flags |= REF_STORE_ODB;
	refs = reftable_be_downcast(ref_store, required_flags, "ref_iterator_begin");

	main_iter = ref_iterator_for_stack(refs, refs->main_stack, prefix, flags);

	/*
	 * The worktree stack is only set when we're in an actual worktree
	 * right now. If we aren't, then we return the common reftable
	 * iterator, only.
	 */
	 if (!refs->worktree_stack)
		return &main_iter->base;

	/*
	 * Otherwise we merge both the common and the per-worktree refs into a
	 * single iterator.
	 */
	worktree_iter = ref_iterator_for_stack(refs, refs->worktree_stack, prefix, flags);
	return merge_ref_iterator_begin(&worktree_iter->base, &main_iter->base,
					ref_iterator_select, NULL);
}

static int reftable_be_read_raw_ref(struct ref_store *ref_store,
				    const char *refname,
				    struct object_id *oid,
				    struct strbuf *referent,
				    unsigned int *type,
				    int *failure_errno)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "read_raw_ref");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	int ret;

	if (refs->err < 0)
		return refs->err;

	ret = reftable_stack_reload(stack);
	if (ret)
		return ret;

	ret = read_ref_without_reload(stack, refname, oid, referent, type);
	if (ret < 0)
		return ret;
	if (ret > 0) {
		*failure_errno = ENOENT;
		return -1;
	}

	return 0;
}

static int reftable_be_read_symbolic_ref(struct ref_store *ref_store,
					 const char *refname,
					 struct strbuf *referent)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "read_symbolic_ref");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct reftable_ref_record ref = {0};
	int ret;

	ret = reftable_stack_reload(stack);
	if (ret)
		return ret;

	ret = reftable_stack_read_ref(stack, refname, &ref);
	if (ret == 0 && ref.value_type == REFTABLE_REF_SYMREF)
		strbuf_addstr(referent, ref.value.symref);
	else
		ret = -1;

	reftable_ref_record_release(&ref);
	return ret;
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

struct reftable_transaction_update {
	struct ref_update *update;
	struct object_id current_oid;
};

struct write_transaction_table_arg {
	struct reftable_ref_store *refs;
	struct reftable_stack *stack;
	struct reftable_addition *addition;
	struct reftable_transaction_update *updates;
	size_t updates_nr;
	size_t updates_alloc;
	size_t updates_expected;
};

struct reftable_transaction_data {
	struct write_transaction_table_arg *args;
	size_t args_nr, args_alloc;
};

static void free_transaction_data(struct reftable_transaction_data *tx_data)
{
	if (!tx_data)
		return;
	for (size_t i = 0; i < tx_data->args_nr; i++) {
		reftable_addition_destroy(tx_data->args[i].addition);
		free(tx_data->args[i].updates);
	}
	free(tx_data->args);
	free(tx_data);
}

/*
 * Prepare transaction update for the given reference update. This will cause
 * us to lock the corresponding reftable stack for concurrent modification.
 */
static int prepare_transaction_update(struct write_transaction_table_arg **out,
				      struct reftable_ref_store *refs,
				      struct reftable_transaction_data *tx_data,
				      struct ref_update *update,
				      struct strbuf *err)
{
	struct reftable_stack *stack = stack_for(refs, update->refname, NULL);
	struct write_transaction_table_arg *arg = NULL;
	size_t i;
	int ret;

	/*
	 * Search for a preexisting stack update. If there is one then we add
	 * the update to it, otherwise we set up a new stack update.
	 */
	for (i = 0; !arg && i < tx_data->args_nr; i++)
		if (tx_data->args[i].stack == stack)
			arg = &tx_data->args[i];

	if (!arg) {
		struct reftable_addition *addition;

		ret = reftable_stack_reload(stack);
		if (ret)
			return ret;

		ret = reftable_stack_new_addition(&addition, stack);
		if (ret) {
			if (ret == REFTABLE_LOCK_ERROR)
				strbuf_addstr(err, "cannot lock references");
			return ret;
		}

		ALLOC_GROW(tx_data->args, tx_data->args_nr + 1,
			   tx_data->args_alloc);
		arg = &tx_data->args[tx_data->args_nr++];
		arg->refs = refs;
		arg->stack = stack;
		arg->addition = addition;
		arg->updates = NULL;
		arg->updates_nr = 0;
		arg->updates_alloc = 0;
		arg->updates_expected = 0;
	}

	arg->updates_expected++;

	if (out)
		*out = arg;

	return 0;
}

/*
 * Queue a reference update for the correct stack. We potentially need to
 * handle multiple stack updates in a single transaction when it spans across
 * multiple worktrees.
 */
static int queue_transaction_update(struct reftable_ref_store *refs,
				    struct reftable_transaction_data *tx_data,
				    struct ref_update *update,
				    struct object_id *current_oid,
				    struct strbuf *err)
{
	struct write_transaction_table_arg *arg = NULL;
	int ret;

	if (update->backend_data)
		BUG("reference update queued more than once");

	ret = prepare_transaction_update(&arg, refs, tx_data, update, err);
	if (ret < 0)
		return ret;

	ALLOC_GROW(arg->updates, arg->updates_nr + 1,
		   arg->updates_alloc);
	arg->updates[arg->updates_nr].update = update;
	oidcpy(&arg->updates[arg->updates_nr].current_oid, current_oid);
	update->backend_data = &arg->updates[arg->updates_nr++];

	return 0;
}

static int reftable_be_transaction_prepare(struct ref_store *ref_store,
					   struct ref_transaction *transaction,
					   struct strbuf *err)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE|REF_STORE_MAIN, "ref_transaction_prepare");
	struct strbuf referent = STRBUF_INIT, head_referent = STRBUF_INIT;
	struct string_list affected_refnames = STRING_LIST_INIT_NODUP;
	struct reftable_transaction_data *tx_data = NULL;
	struct object_id head_oid;
	unsigned int head_type = 0;
	size_t i;
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	tx_data = xcalloc(1, sizeof(*tx_data));

	/*
	 * Preprocess all updates. For one we check that there are no duplicate
	 * reference updates in this transaction. Second, we lock all stacks
	 * that will be modified during the transaction.
	 */
	for (i = 0; i < transaction->nr; i++) {
		ret = prepare_transaction_update(NULL, refs, tx_data,
						 transaction->updates[i], err);
		if (ret)
			goto done;

		string_list_append(&affected_refnames,
				   transaction->updates[i]->refname);
	}

	/*
	 * Now that we have counted updates per stack we can preallocate their
	 * arrays. This avoids having to reallocate many times.
	 */
	for (i = 0; i < tx_data->args_nr; i++) {
		CALLOC_ARRAY(tx_data->args[i].updates, tx_data->args[i].updates_expected);
		tx_data->args[i].updates_alloc = tx_data->args[i].updates_expected;
	}

	/*
	 * Fail if a refname appears more than once in the transaction.
	 * This code is taken from the files backend and is a good candidate to
	 * be moved into the generic layer.
	 */
	string_list_sort(&affected_refnames);
	if (ref_update_reject_duplicates(&affected_refnames, err)) {
		ret = TRANSACTION_GENERIC_ERROR;
		goto done;
	}

	ret = read_ref_without_reload(stack_for(refs, "HEAD", NULL), "HEAD", &head_oid,
				      &head_referent, &head_type);
	if (ret < 0)
		goto done;
	ret = 0;

	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *u = transaction->updates[i];
		struct object_id current_oid = {0};
		struct reftable_stack *stack;
		const char *rewritten_ref;

		stack = stack_for(refs, u->refname, &rewritten_ref);

		/* Verify that the new object ID is valid. */
		if ((u->flags & REF_HAVE_NEW) && !is_null_oid(&u->new_oid) &&
		    !(u->flags & REF_SKIP_OID_VERIFICATION) &&
		    !(u->flags & REF_LOG_ONLY)) {
			struct object *o = parse_object(refs->base.repo, &u->new_oid);
			if (!o) {
				strbuf_addf(err,
					    _("trying to write ref '%s' with nonexistent object %s"),
					    u->refname, oid_to_hex(&u->new_oid));
				ret = -1;
				goto done;
			}

			if (o->type != OBJ_COMMIT && is_branch(u->refname)) {
				strbuf_addf(err, _("trying to write non-commit object %s to branch '%s'"),
					    oid_to_hex(&u->new_oid), u->refname);
				ret = -1;
				goto done;
			}
		}

		/*
		 * When we update the reference that HEAD points to we enqueue
		 * a second log-only update for HEAD so that its reflog is
		 * updated accordingly.
		 */
		if (head_type == REF_ISSYMREF &&
		    !(u->flags & REF_LOG_ONLY) &&
		    !(u->flags & REF_UPDATE_VIA_HEAD) &&
		    !strcmp(rewritten_ref, head_referent.buf)) {
			struct ref_update *new_update;

			/*
			 * First make sure that HEAD is not already in the
			 * transaction. This check is O(lg N) in the transaction
			 * size, but it happens at most once per transaction.
			 */
			if (string_list_has_string(&affected_refnames, "HEAD")) {
				/* An entry already existed */
				strbuf_addf(err,
					    _("multiple updates for 'HEAD' (including one "
					    "via its referent '%s') are not allowed"),
					    u->refname);
				ret = TRANSACTION_NAME_CONFLICT;
				goto done;
			}

			new_update = ref_transaction_add_update(
					transaction, "HEAD",
					u->flags | REF_LOG_ONLY | REF_NO_DEREF,
					&u->new_oid, &u->old_oid, u->msg);
			string_list_insert(&affected_refnames, new_update->refname);
		}

		ret = read_ref_without_reload(stack, rewritten_ref,
					      &current_oid, &referent, &u->type);
		if (ret < 0)
			goto done;
		if (ret > 0 && (!(u->flags & REF_HAVE_OLD) || is_null_oid(&u->old_oid))) {
			/*
			 * The reference does not exist, and we either have no
			 * old object ID or expect the reference to not exist.
			 * We can thus skip below safety checks as well as the
			 * symref splitting. But we do want to verify that
			 * there is no conflicting reference here so that we
			 * can output a proper error message instead of failing
			 * at a later point.
			 */
			ret = refs_verify_refname_available(ref_store, u->refname,
							    &affected_refnames, NULL, err);
			if (ret < 0)
				goto done;

			/*
			 * There is no need to write the reference deletion
			 * when the reference in question doesn't exist.
			 */
			 if (u->flags & REF_HAVE_NEW && !is_null_oid(&u->new_oid)) {
				 ret = queue_transaction_update(refs, tx_data, u,
								&current_oid, err);
				 if (ret)
					 goto done;
			 }

			continue;
		}
		if (ret > 0) {
			/* The reference does not exist, but we expected it to. */
			strbuf_addf(err, _("cannot lock ref '%s': "
				    "unable to resolve reference '%s'"),
				    original_update_refname(u), u->refname);
			ret = -1;
			goto done;
		}

		if (u->type & REF_ISSYMREF) {
			/*
			 * The reftable stack is locked at this point already,
			 * so it is safe to call `refs_resolve_ref_unsafe()`
			 * here without causing races.
			 */
			const char *resolved = refs_resolve_ref_unsafe(&refs->base, u->refname, 0,
								       &current_oid, NULL);

			if (u->flags & REF_NO_DEREF) {
				if (u->flags & REF_HAVE_OLD && !resolved) {
					strbuf_addf(err, _("cannot lock ref '%s': "
						    "error reading reference"), u->refname);
					ret = -1;
					goto done;
				}
			} else {
				struct ref_update *new_update;
				int new_flags;

				new_flags = u->flags;
				if (!strcmp(rewritten_ref, "HEAD"))
					new_flags |= REF_UPDATE_VIA_HEAD;

				/*
				 * If we are updating a symref (eg. HEAD), we should also
				 * update the branch that the symref points to.
				 *
				 * This is generic functionality, and would be better
				 * done in refs.c, but the current implementation is
				 * intertwined with the locking in files-backend.c.
				 */
				new_update = ref_transaction_add_update(
						transaction, referent.buf, new_flags,
						&u->new_oid, &u->old_oid, u->msg);
				new_update->parent_update = u;

				/*
				 * Change the symbolic ref update to log only. Also, it
				 * doesn't need to check its old OID value, as that will be
				 * done when new_update is processed.
				 */
				u->flags |= REF_LOG_ONLY | REF_NO_DEREF;
				u->flags &= ~REF_HAVE_OLD;

				if (string_list_has_string(&affected_refnames, new_update->refname)) {
					strbuf_addf(err,
						    _("multiple updates for '%s' (including one "
						    "via symref '%s') are not allowed"),
						    referent.buf, u->refname);
					ret = TRANSACTION_NAME_CONFLICT;
					goto done;
				}
				string_list_insert(&affected_refnames, new_update->refname);
			}
		}

		/*
		 * Verify that the old object matches our expectations. Note
		 * that the error messages here do not make a lot of sense in
		 * the context of the reftable backend as we never lock
		 * individual refs. But the error messages match what the files
		 * backend returns, which keeps our tests happy.
		 */
		if (u->flags & REF_HAVE_OLD && !oideq(&current_oid, &u->old_oid)) {
			if (is_null_oid(&u->old_oid))
				strbuf_addf(err, _("cannot lock ref '%s': "
					    "reference already exists"),
					    original_update_refname(u));
			else if (is_null_oid(&current_oid))
				strbuf_addf(err, _("cannot lock ref '%s': "
					    "reference is missing but expected %s"),
					    original_update_refname(u),
					    oid_to_hex(&u->old_oid));
			else
				strbuf_addf(err, _("cannot lock ref '%s': "
					    "is at %s but expected %s"),
					    original_update_refname(u),
					    oid_to_hex(&current_oid),
					    oid_to_hex(&u->old_oid));
			ret = -1;
			goto done;
		}

		/*
		 * If all of the following conditions are true:
		 *
		 *   - We're not about to write a symref.
		 *   - We're not about to write a log-only entry.
		 *   - Old and new object ID are different.
		 *
		 * Then we're essentially doing a no-op update that can be
		 * skipped. This is not only for the sake of efficiency, but
		 * also skips writing unneeded reflog entries.
		 */
		if ((u->type & REF_ISSYMREF) ||
		    (u->flags & REF_LOG_ONLY) ||
		    (u->flags & REF_HAVE_NEW && !oideq(&current_oid, &u->new_oid))) {
			ret = queue_transaction_update(refs, tx_data, u,
						       &current_oid, err);
			if (ret)
				goto done;
		}
	}

	transaction->backend_data = tx_data;
	transaction->state = REF_TRANSACTION_PREPARED;

done:
	assert(ret != REFTABLE_API_ERROR);
	if (ret < 0) {
		free_transaction_data(tx_data);
		transaction->state = REF_TRANSACTION_CLOSED;
		if (!err->len)
			strbuf_addf(err, _("reftable: transaction prepare: %s"),
				    reftable_error_str(ret));
	}
	string_list_clear(&affected_refnames, 0);
	strbuf_release(&referent);
	strbuf_release(&head_referent);

	return ret;
}

static int reftable_be_transaction_abort(struct ref_store *ref_store,
					 struct ref_transaction *transaction,
					 struct strbuf *err)
{
	struct reftable_transaction_data *tx_data = transaction->backend_data;
	free_transaction_data(tx_data);
	transaction->state = REF_TRANSACTION_CLOSED;
	return 0;
}

static int transaction_update_cmp(const void *a, const void *b)
{
	return strcmp(((struct reftable_transaction_update *)a)->update->refname,
		      ((struct reftable_transaction_update *)b)->update->refname);
}

static int write_transaction_table(struct reftable_writer *writer, void *cb_data)
{
	struct write_transaction_table_arg *arg = cb_data;
	struct reftable_merged_table *mt =
		reftable_stack_merged_table(arg->stack);
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	struct reftable_log_record *logs = NULL;
	struct ident_split committer_ident = {0};
	size_t logs_nr = 0, logs_alloc = 0, i;
	const char *committer_info;
	int ret = 0;

	committer_info = git_committer_info(0);
	if (split_ident_line(&committer_ident, committer_info, strlen(committer_info)))
		BUG("failed splitting committer info");

	QSORT(arg->updates, arg->updates_nr, transaction_update_cmp);

	reftable_writer_set_limits(writer, ts, ts);

	for (i = 0; i < arg->updates_nr; i++) {
		struct reftable_transaction_update *tx_update = &arg->updates[i];
		struct ref_update *u = tx_update->update;

		/*
		 * Write a reflog entry when updating a ref to point to
		 * something new in either of the following cases:
		 *
		 * - The reference is about to be deleted. We always want to
		 *   delete the reflog in that case.
		 * - REF_FORCE_CREATE_REFLOG is set, asking us to always create
		 *   the reflog entry.
		 * - `core.logAllRefUpdates` tells us to create the reflog for
		 *   the given ref.
		 */
		if (u->flags & REF_HAVE_NEW && !(u->type & REF_ISSYMREF) && is_null_oid(&u->new_oid)) {
			struct reftable_log_record log = {0};
			struct reftable_iterator it = {0};

			/*
			 * When deleting refs we also delete all reflog entries
			 * with them. While it is not strictly required to
			 * delete reflogs together with their refs, this
			 * matches the behaviour of the files backend.
			 *
			 * Unfortunately, we have no better way than to delete
			 * all reflog entries one by one.
			 */
			ret = reftable_merged_table_seek_log(mt, &it, u->refname);
			while (ret == 0) {
				struct reftable_log_record *tombstone;

				ret = reftable_iterator_next_log(&it, &log);
				if (ret < 0)
					break;
				if (ret > 0 || strcmp(log.refname, u->refname)) {
					ret = 0;
					break;
				}

				ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
				tombstone = &logs[logs_nr++];
				tombstone->refname = xstrdup(u->refname);
				tombstone->value_type = REFTABLE_LOG_DELETION;
				tombstone->update_index = log.update_index;
			}

			reftable_log_record_release(&log);
			reftable_iterator_destroy(&it);

			if (ret)
				goto done;
		} else if (u->flags & REF_HAVE_NEW &&
			   (u->flags & REF_FORCE_CREATE_REFLOG ||
			    should_write_log(&arg->refs->base, u->refname))) {
			struct reftable_log_record *log;

			ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
			log = &logs[logs_nr++];
			memset(log, 0, sizeof(*log));

			fill_reftable_log_record(log, &committer_ident);
			log->update_index = ts;
			log->refname = xstrdup(u->refname);
			memcpy(log->value.update.new_hash, u->new_oid.hash, GIT_MAX_RAWSZ);
			memcpy(log->value.update.old_hash, tx_update->current_oid.hash, GIT_MAX_RAWSZ);
			log->value.update.message =
				xstrndup(u->msg, arg->refs->write_options.block_size / 2);
		}

		if (u->flags & REF_LOG_ONLY)
			continue;

		if (u->flags & REF_HAVE_NEW && is_null_oid(&u->new_oid)) {
			struct reftable_ref_record ref = {
				.refname = (char *)u->refname,
				.update_index = ts,
				.value_type = REFTABLE_REF_DELETION,
			};

			ret = reftable_writer_add_ref(writer, &ref);
			if (ret < 0)
				goto done;
		} else if (u->flags & REF_HAVE_NEW) {
			struct reftable_ref_record ref = {0};
			struct object_id peeled;
			int peel_error;

			ref.refname = (char *)u->refname;
			ref.update_index = ts;

			peel_error = peel_object(&u->new_oid, &peeled);
			if (!peel_error) {
				ref.value_type = REFTABLE_REF_VAL2;
				memcpy(ref.value.val2.target_value, peeled.hash, GIT_MAX_RAWSZ);
				memcpy(ref.value.val2.value, u->new_oid.hash, GIT_MAX_RAWSZ);
			} else if (!is_null_oid(&u->new_oid)) {
				ref.value_type = REFTABLE_REF_VAL1;
				memcpy(ref.value.val1, u->new_oid.hash, GIT_MAX_RAWSZ);
			}

			ret = reftable_writer_add_ref(writer, &ref);
			if (ret < 0)
				goto done;
		}
	}

	/*
	 * Logs are written at the end so that we do not have intermixed ref
	 * and log blocks.
	 */
	if (logs) {
		ret = reftable_writer_add_logs(writer, logs, logs_nr);
		if (ret < 0)
			goto done;
	}

done:
	assert(ret != REFTABLE_API_ERROR);
	for (i = 0; i < logs_nr; i++)
		reftable_log_record_release(&logs[i]);
	free(logs);
	return ret;
}

static int reftable_be_transaction_finish(struct ref_store *ref_store,
					  struct ref_transaction *transaction,
					  struct strbuf *err)
{
	struct reftable_transaction_data *tx_data = transaction->backend_data;
	int ret = 0;

	for (size_t i = 0; i < tx_data->args_nr; i++) {
		ret = reftable_addition_add(tx_data->args[i].addition,
					    write_transaction_table, &tx_data->args[i]);
		if (ret < 0)
			goto done;

		ret = reftable_addition_commit(tx_data->args[i].addition);
		if (ret < 0)
			goto done;
	}

done:
	assert(ret != REFTABLE_API_ERROR);
	free_transaction_data(tx_data);
	transaction->state = REF_TRANSACTION_CLOSED;

	if (ret) {
		strbuf_addf(err, _("reftable: transaction failure: %s"),
			    reftable_error_str(ret));
		return -1;
	}
	return ret;
}

static int reftable_be_initial_transaction_commit(struct ref_store *ref_store UNUSED,
						  struct ref_transaction *transaction,
						  struct strbuf *err)
{
	return ref_transaction_commit(transaction, err);
}

static int reftable_be_pack_refs(struct ref_store *ref_store,
				 struct pack_refs_opts *opts)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE | REF_STORE_ODB, "pack_refs");
	struct reftable_stack *stack;
	int ret;

	if (refs->err)
		return refs->err;

	stack = refs->worktree_stack;
	if (!stack)
		stack = refs->main_stack;

	if (opts->flags & PACK_REFS_AUTO)
		ret = reftable_stack_auto_compact(stack);
	else
		ret = reftable_stack_compact_all(stack, NULL);
	if (ret < 0) {
		ret = error(_("unable to compact stack: %s"),
			    reftable_error_str(ret));
		goto out;
	}

	ret = reftable_stack_clean(stack);
	if (ret)
		goto out;

out:
	return ret;
}

struct write_create_symref_arg {
	struct reftable_ref_store *refs;
	struct reftable_stack *stack;
	struct strbuf *err;
	const char *refname;
	const char *target;
	const char *logmsg;
};

static int write_create_symref_table(struct reftable_writer *writer, void *cb_data)
{
	struct write_create_symref_arg *create = cb_data;
	uint64_t ts = reftable_stack_next_update_index(create->stack);
	struct reftable_ref_record ref = {
		.refname = (char *)create->refname,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *)create->target,
		.update_index = ts,
	};
	struct ident_split committer_ident = {0};
	struct reftable_log_record log = {0};
	struct object_id new_oid;
	struct object_id old_oid;
	const char *committer_info;
	int ret;

	reftable_writer_set_limits(writer, ts, ts);

	ret = refs_verify_refname_available(&create->refs->base, create->refname,
					    NULL, NULL, create->err);
	if (ret < 0)
		return ret;

	ret = reftable_writer_add_ref(writer, &ref);
	if (ret)
		return ret;

	/*
	 * Note that it is important to try and resolve the reference before we
	 * write the log entry. This is because `should_write_log()` will munge
	 * `core.logAllRefUpdates`, which is undesirable when we create a new
	 * repository because it would be written into the config. As HEAD will
	 * not resolve for new repositories this ordering will ensure that this
	 * never happens.
	 */
	if (!create->logmsg ||
	    !refs_resolve_ref_unsafe(&create->refs->base, create->target,
				     RESOLVE_REF_READING, &new_oid, NULL) ||
	    !should_write_log(&create->refs->base, create->refname))
		return 0;

	committer_info = git_committer_info(0);
	if (split_ident_line(&committer_ident, committer_info, strlen(committer_info)))
		BUG("failed splitting committer info");

	fill_reftable_log_record(&log, &committer_ident);
	log.refname = xstrdup(create->refname);
	log.update_index = ts;
	log.value.update.message = xstrndup(create->logmsg,
					    create->refs->write_options.block_size / 2);
	memcpy(log.value.update.new_hash, new_oid.hash, GIT_MAX_RAWSZ);
	if (refs_resolve_ref_unsafe(&create->refs->base, create->refname,
				    RESOLVE_REF_READING, &old_oid, NULL))
		memcpy(log.value.update.old_hash, old_oid.hash, GIT_MAX_RAWSZ);

	ret = reftable_writer_add_log(writer, &log);
	reftable_log_record_release(&log);
	return ret;
}

static int reftable_be_create_symref(struct ref_store *ref_store,
				     const char *refname,
				     const char *target,
				     const char *logmsg)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "create_symref");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct strbuf err = STRBUF_INIT;
	struct write_create_symref_arg arg = {
		.refs = refs,
		.stack = stack,
		.refname = refname,
		.target = target,
		.logmsg = logmsg,
		.err = &err,
	};
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret)
		goto done;

	ret = reftable_stack_add(stack, &write_create_symref_table, &arg);

done:
	assert(ret != REFTABLE_API_ERROR);
	if (ret) {
		if (err.len)
			error("%s", err.buf);
		else
			error("unable to write symref for %s: %s", refname,
			      reftable_error_str(ret));
	}

	strbuf_release(&err);
	return ret;
}

struct write_copy_arg {
	struct reftable_ref_store *refs;
	struct reftable_stack *stack;
	const char *oldname;
	const char *newname;
	const char *logmsg;
	int delete_old;
};

static int write_copy_table(struct reftable_writer *writer, void *cb_data)
{
	struct write_copy_arg *arg = cb_data;
	uint64_t deletion_ts, creation_ts;
	struct reftable_merged_table *mt = reftable_stack_merged_table(arg->stack);
	struct reftable_ref_record old_ref = {0}, refs[2] = {0};
	struct reftable_log_record old_log = {0}, *logs = NULL;
	struct reftable_iterator it = {0};
	struct string_list skip = STRING_LIST_INIT_NODUP;
	struct ident_split committer_ident = {0};
	struct strbuf errbuf = STRBUF_INIT;
	size_t logs_nr = 0, logs_alloc = 0, i;
	const char *committer_info;
	int ret;

	committer_info = git_committer_info(0);
	if (split_ident_line(&committer_ident, committer_info, strlen(committer_info)))
		BUG("failed splitting committer info");

	if (reftable_stack_read_ref(arg->stack, arg->oldname, &old_ref)) {
		ret = error(_("refname %s not found"), arg->oldname);
		goto done;
	}
	if (old_ref.value_type == REFTABLE_REF_SYMREF) {
		ret = error(_("refname %s is a symbolic ref, copying it is not supported"),
			    arg->oldname);
		goto done;
	}

	/*
	 * There's nothing to do in case the old and new name are the same, so
	 * we exit early in that case.
	 */
	if (!strcmp(arg->oldname, arg->newname)) {
		ret = 0;
		goto done;
	}

	/*
	 * Verify that the new refname is available.
	 */
	if (arg->delete_old)
		string_list_insert(&skip, arg->oldname);
	ret = refs_verify_refname_available(&arg->refs->base, arg->newname,
					    NULL, &skip, &errbuf);
	if (ret < 0) {
		error("%s", errbuf.buf);
		goto done;
	}

	/*
	 * When deleting the old reference we have to use two update indices:
	 * once to delete the old ref and its reflog, and once to create the
	 * new ref and its reflog. They need to be staged with two separate
	 * indices because the new reflog needs to encode both the deletion of
	 * the old branch and the creation of the new branch, and we cannot do
	 * two changes to a reflog in a single update.
	 */
	deletion_ts = creation_ts = reftable_stack_next_update_index(arg->stack);
	if (arg->delete_old)
		creation_ts++;
	reftable_writer_set_limits(writer, deletion_ts, creation_ts);

	/*
	 * Add the new reference. If this is a rename then we also delete the
	 * old reference.
	 */
	refs[0] = old_ref;
	refs[0].refname = (char *)arg->newname;
	refs[0].update_index = creation_ts;
	if (arg->delete_old) {
		refs[1].refname = (char *)arg->oldname;
		refs[1].value_type = REFTABLE_REF_DELETION;
		refs[1].update_index = deletion_ts;
	}
	ret = reftable_writer_add_refs(writer, refs, arg->delete_old ? 2 : 1);
	if (ret < 0)
		goto done;

	/*
	 * When deleting the old branch we need to create a reflog entry on the
	 * new branch name that indicates that the old branch has been deleted
	 * and then recreated. This is a tad weird, but matches what the files
	 * backend does.
	 */
	if (arg->delete_old) {
		struct strbuf head_referent = STRBUF_INIT;
		struct object_id head_oid;
		int append_head_reflog;
		unsigned head_type = 0;

		ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
		memset(&logs[logs_nr], 0, sizeof(logs[logs_nr]));
		fill_reftable_log_record(&logs[logs_nr], &committer_ident);
		logs[logs_nr].refname = (char *)arg->newname;
		logs[logs_nr].update_index = deletion_ts;
		logs[logs_nr].value.update.message =
			xstrndup(arg->logmsg, arg->refs->write_options.block_size / 2);
		memcpy(logs[logs_nr].value.update.old_hash, old_ref.value.val1, GIT_MAX_RAWSZ);
		logs_nr++;

		ret = read_ref_without_reload(arg->stack, "HEAD", &head_oid, &head_referent, &head_type);
		if (ret < 0)
			goto done;
		append_head_reflog = (head_type & REF_ISSYMREF) && !strcmp(head_referent.buf, arg->oldname);
		strbuf_release(&head_referent);

		/*
		 * The files backend uses `refs_delete_ref()` to delete the old
		 * branch name, which will append a reflog entry for HEAD in
		 * case it points to the old branch.
		 */
		if (append_head_reflog) {
			ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
			logs[logs_nr] = logs[logs_nr - 1];
			logs[logs_nr].refname = "HEAD";
			logs_nr++;
		}
	}

	/*
	 * Create the reflog entry for the newly created branch.
	 */
	ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
	memset(&logs[logs_nr], 0, sizeof(logs[logs_nr]));
	fill_reftable_log_record(&logs[logs_nr], &committer_ident);
	logs[logs_nr].refname = (char *)arg->newname;
	logs[logs_nr].update_index = creation_ts;
	logs[logs_nr].value.update.message =
		xstrndup(arg->logmsg, arg->refs->write_options.block_size / 2);
	memcpy(logs[logs_nr].value.update.new_hash, old_ref.value.val1, GIT_MAX_RAWSZ);
	logs_nr++;

	/*
	 * In addition to writing the reflog entry for the new branch, we also
	 * copy over all log entries from the old reflog. Last but not least,
	 * when renaming we also have to delete all the old reflog entries.
	 */
	ret = reftable_merged_table_seek_log(mt, &it, arg->oldname);
	if (ret < 0)
		goto done;

	while (1) {
		ret = reftable_iterator_next_log(&it, &old_log);
		if (ret < 0)
			goto done;
		if (ret > 0 || strcmp(old_log.refname, arg->oldname)) {
			ret = 0;
			break;
		}

		free(old_log.refname);

		/*
		 * Copy over the old reflog entry with the new refname.
		 */
		ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
		logs[logs_nr] = old_log;
		logs[logs_nr].refname = (char *)arg->newname;
		logs_nr++;

		/*
		 * Delete the old reflog entry in case we are renaming.
		 */
		if (arg->delete_old) {
			ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
			memset(&logs[logs_nr], 0, sizeof(logs[logs_nr]));
			logs[logs_nr].refname = (char *)arg->oldname;
			logs[logs_nr].value_type = REFTABLE_LOG_DELETION;
			logs[logs_nr].update_index = old_log.update_index;
			logs_nr++;
		}

		/*
		 * Transfer ownership of the log record we're iterating over to
		 * the array of log records. Otherwise, the pointers would get
		 * free'd or reallocated by the iterator.
		 */
		memset(&old_log, 0, sizeof(old_log));
	}

	ret = reftable_writer_add_logs(writer, logs, logs_nr);
	if (ret < 0)
		goto done;

done:
	assert(ret != REFTABLE_API_ERROR);
	reftable_iterator_destroy(&it);
	string_list_clear(&skip, 0);
	strbuf_release(&errbuf);
	for (i = 0; i < logs_nr; i++) {
		if (!strcmp(logs[i].refname, "HEAD"))
			continue;
		logs[i].refname = NULL;
		reftable_log_record_release(&logs[i]);
	}
	free(logs);
	reftable_ref_record_release(&old_ref);
	reftable_log_record_release(&old_log);
	return ret;
}

static int reftable_be_rename_ref(struct ref_store *ref_store,
				  const char *oldrefname,
				  const char *newrefname,
				  const char *logmsg)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "rename_ref");
	struct reftable_stack *stack = stack_for(refs, newrefname, &newrefname);
	struct write_copy_arg arg = {
		.refs = refs,
		.stack = stack,
		.oldname = oldrefname,
		.newname = newrefname,
		.logmsg = logmsg,
		.delete_old = 1,
	};
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret)
		goto done;
	ret = reftable_stack_add(stack, &write_copy_table, &arg);

done:
	assert(ret != REFTABLE_API_ERROR);
	return ret;
}

static int reftable_be_copy_ref(struct ref_store *ref_store,
				const char *oldrefname,
				const char *newrefname,
				const char *logmsg)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "copy_ref");
	struct reftable_stack *stack = stack_for(refs, newrefname, &newrefname);
	struct write_copy_arg arg = {
		.refs = refs,
		.stack = stack,
		.oldname = oldrefname,
		.newname = newrefname,
		.logmsg = logmsg,
	};
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret)
		goto done;
	ret = reftable_stack_add(stack, &write_copy_table, &arg);

done:
	assert(ret != REFTABLE_API_ERROR);
	return ret;
}

struct reftable_reflog_iterator {
	struct ref_iterator base;
	struct reftable_ref_store *refs;
	struct reftable_iterator iter;
	struct reftable_log_record log;
	struct strbuf last_name;
	int err;
};

static int reftable_reflog_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct reftable_reflog_iterator *iter =
		(struct reftable_reflog_iterator *)ref_iterator;

	while (!iter->err) {
		iter->err = reftable_iterator_next_log(&iter->iter, &iter->log);
		if (iter->err)
			break;

		/*
		 * We want the refnames that we have reflogs for, so we skip if
		 * we've already produced this name. This could be faster by
		 * seeking directly to reflog@update_index==0.
		 */
		if (!strcmp(iter->log.refname, iter->last_name.buf))
			continue;

		if (check_refname_format(iter->log.refname,
					 REFNAME_ALLOW_ONELEVEL))
			continue;

		strbuf_reset(&iter->last_name);
		strbuf_addstr(&iter->last_name, iter->log.refname);
		iter->base.refname = iter->log.refname;

		break;
	}

	if (iter->err > 0) {
		if (ref_iterator_abort(ref_iterator) != ITER_DONE)
			return ITER_ERROR;
		return ITER_DONE;
	}

	if (iter->err < 0) {
		ref_iterator_abort(ref_iterator);
		return ITER_ERROR;
	}

	return ITER_OK;
}

static int reftable_reflog_iterator_peel(struct ref_iterator *ref_iterator,
						 struct object_id *peeled)
{
	BUG("reftable reflog iterator cannot be peeled");
	return -1;
}

static int reftable_reflog_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct reftable_reflog_iterator *iter =
		(struct reftable_reflog_iterator *)ref_iterator;
	reftable_log_record_release(&iter->log);
	reftable_iterator_destroy(&iter->iter);
	strbuf_release(&iter->last_name);
	free(iter);
	return ITER_DONE;
}

static struct ref_iterator_vtable reftable_reflog_iterator_vtable = {
	.advance = reftable_reflog_iterator_advance,
	.peel = reftable_reflog_iterator_peel,
	.abort = reftable_reflog_iterator_abort
};

static struct reftable_reflog_iterator *reflog_iterator_for_stack(struct reftable_ref_store *refs,
								  struct reftable_stack *stack)
{
	struct reftable_merged_table *merged_table;
	struct reftable_reflog_iterator *iter;
	int ret;

	iter = xcalloc(1, sizeof(*iter));
	base_ref_iterator_init(&iter->base, &reftable_reflog_iterator_vtable);
	strbuf_init(&iter->last_name, 0);
	iter->refs = refs;

	ret = refs->err;
	if (ret)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret < 0)
		goto done;

	merged_table = reftable_stack_merged_table(stack);

	ret = reftable_merged_table_seek_log(merged_table, &iter->iter, "");
	if (ret < 0)
		goto done;

done:
	iter->err = ret;
	return iter;
}

static struct ref_iterator *reftable_be_reflog_iterator_begin(struct ref_store *ref_store)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "reflog_iterator_begin");
	struct reftable_reflog_iterator *main_iter, *worktree_iter;

	main_iter = reflog_iterator_for_stack(refs, refs->main_stack);
	if (!refs->worktree_stack)
		return &main_iter->base;

	worktree_iter = reflog_iterator_for_stack(refs, refs->worktree_stack);

	return merge_ref_iterator_begin(&worktree_iter->base, &main_iter->base,
					ref_iterator_select, NULL);
}

static int yield_log_record(struct reftable_log_record *log,
			    each_reflog_ent_fn fn,
			    void *cb_data)
{
	struct object_id old_oid, new_oid;
	const char *full_committer;

	oidread(&old_oid, log->value.update.old_hash);
	oidread(&new_oid, log->value.update.new_hash);

	/*
	 * When both the old object ID and the new object ID are null
	 * then this is the reflog existence marker. The caller must
	 * not be aware of it.
	 */
	if (is_null_oid(&old_oid) && is_null_oid(&new_oid))
		return 0;

	full_committer = fmt_ident(log->value.update.name, log->value.update.email,
				   WANT_COMMITTER_IDENT, NULL, IDENT_NO_DATE);
	return fn(&old_oid, &new_oid, full_committer,
		  log->value.update.time, log->value.update.tz_offset,
		  log->value.update.message, cb_data);
}

static int reftable_be_for_each_reflog_ent_reverse(struct ref_store *ref_store,
						   const char *refname,
						   each_reflog_ent_fn fn,
						   void *cb_data)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "for_each_reflog_ent_reverse");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct reftable_merged_table *mt = NULL;
	struct reftable_log_record log = {0};
	struct reftable_iterator it = {0};
	int ret;

	if (refs->err < 0)
		return refs->err;

	mt = reftable_stack_merged_table(stack);
	ret = reftable_merged_table_seek_log(mt, &it, refname);
	while (!ret) {
		ret = reftable_iterator_next_log(&it, &log);
		if (ret < 0)
			break;
		if (ret > 0 || strcmp(log.refname, refname)) {
			ret = 0;
			break;
		}

		ret = yield_log_record(&log, fn, cb_data);
		if (ret)
			break;
	}

	reftable_log_record_release(&log);
	reftable_iterator_destroy(&it);
	return ret;
}

static int reftable_be_for_each_reflog_ent(struct ref_store *ref_store,
					   const char *refname,
					   each_reflog_ent_fn fn,
					   void *cb_data)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "for_each_reflog_ent");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct reftable_merged_table *mt = NULL;
	struct reftable_log_record *logs = NULL;
	struct reftable_iterator it = {0};
	size_t logs_alloc = 0, logs_nr = 0, i;
	int ret;

	if (refs->err < 0)
		return refs->err;

	mt = reftable_stack_merged_table(stack);
	ret = reftable_merged_table_seek_log(mt, &it, refname);
	while (!ret) {
		struct reftable_log_record log = {0};

		ret = reftable_iterator_next_log(&it, &log);
		if (ret < 0)
			goto done;
		if (ret > 0 || strcmp(log.refname, refname)) {
			reftable_log_record_release(&log);
			ret = 0;
			break;
		}

		ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
		logs[logs_nr++] = log;
	}

	for (i = logs_nr; i--;) {
		ret = yield_log_record(&logs[i], fn, cb_data);
		if (ret)
			goto done;
	}

done:
	reftable_iterator_destroy(&it);
	for (i = 0; i < logs_nr; i++)
		reftable_log_record_release(&logs[i]);
	free(logs);
	return ret;
}

static int reftable_be_reflog_exists(struct ref_store *ref_store,
				     const char *refname)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_READ, "reflog_exists");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct reftable_merged_table *mt = reftable_stack_merged_table(stack);
	struct reftable_log_record log = {0};
	struct reftable_iterator it = {0};
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret < 0)
		goto done;

	ret = reftable_merged_table_seek_log(mt, &it, refname);
	if (ret < 0)
		goto done;

	/*
	 * Check whether we get at least one log record for the given ref name.
	 * If so, the reflog exists, otherwise it doesn't.
	 */
	ret = reftable_iterator_next_log(&it, &log);
	if (ret < 0)
		goto done;
	if (ret > 0) {
		ret = 0;
		goto done;
	}

	ret = strcmp(log.refname, refname) == 0;

done:
	reftable_iterator_destroy(&it);
	reftable_log_record_release(&log);
	if (ret < 0)
		ret = 0;
	return ret;
}

struct write_reflog_existence_arg {
	struct reftable_ref_store *refs;
	const char *refname;
	struct reftable_stack *stack;
};

static int write_reflog_existence_table(struct reftable_writer *writer,
					void *cb_data)
{
	struct write_reflog_existence_arg *arg = cb_data;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	struct reftable_log_record log = {0};
	int ret;

	ret = reftable_stack_read_log(arg->stack, arg->refname, &log);
	if (ret <= 0)
		goto done;

	reftable_writer_set_limits(writer, ts, ts);

	/*
	 * The existence entry has both old and new object ID set to the the
	 * null object ID. Our iterators are aware of this and will not present
	 * them to their callers.
	 */
	log.refname = xstrdup(arg->refname);
	log.update_index = ts;
	log.value_type = REFTABLE_LOG_UPDATE;
	ret = reftable_writer_add_log(writer, &log);

done:
	assert(ret != REFTABLE_API_ERROR);
	reftable_log_record_release(&log);
	return ret;
}

static int reftable_be_create_reflog(struct ref_store *ref_store,
				     const char *refname,
				     struct strbuf *errmsg)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "create_reflog");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct write_reflog_existence_arg arg = {
		.refs = refs,
		.stack = stack,
		.refname = refname,
	};
	int ret;

	ret = refs->err;
	if (ret < 0)
		goto done;

	ret = reftable_stack_reload(stack);
	if (ret)
		goto done;

	ret = reftable_stack_add(stack, &write_reflog_existence_table, &arg);

done:
	return ret;
}

struct write_reflog_delete_arg {
	struct reftable_stack *stack;
	const char *refname;
};

static int write_reflog_delete_table(struct reftable_writer *writer, void *cb_data)
{
	struct write_reflog_delete_arg *arg = cb_data;
	struct reftable_merged_table *mt =
		reftable_stack_merged_table(arg->stack);
	struct reftable_log_record log = {0}, tombstone = {0};
	struct reftable_iterator it = {0};
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	int ret;

	reftable_writer_set_limits(writer, ts, ts);

	/*
	 * In order to delete a table we need to delete all reflog entries one
	 * by one. This is inefficient, but the reftable format does not have a
	 * better marker right now.
	 */
	ret = reftable_merged_table_seek_log(mt, &it, arg->refname);
	while (ret == 0) {
		ret = reftable_iterator_next_log(&it, &log);
		if (ret < 0)
			break;
		if (ret > 0 || strcmp(log.refname, arg->refname)) {
			ret = 0;
			break;
		}

		tombstone.refname = (char *)arg->refname;
		tombstone.value_type = REFTABLE_LOG_DELETION;
		tombstone.update_index = log.update_index;

		ret = reftable_writer_add_log(writer, &tombstone);
	}

	reftable_log_record_release(&log);
	reftable_iterator_destroy(&it);
	return ret;
}

static int reftable_be_delete_reflog(struct ref_store *ref_store,
				     const char *refname)
{
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "delete_reflog");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct write_reflog_delete_arg arg = {
		.stack = stack,
		.refname = refname,
	};
	int ret;

	ret = reftable_stack_reload(stack);
	if (ret)
		return ret;
	ret = reftable_stack_add(stack, &write_reflog_delete_table, &arg);

	assert(ret != REFTABLE_API_ERROR);
	return ret;
}

struct reflog_expiry_arg {
	struct reftable_stack *stack;
	struct reftable_log_record *records;
	struct object_id update_oid;
	const char *refname;
	size_t len;
};

static int write_reflog_expiry_table(struct reftable_writer *writer, void *cb_data)
{
	struct reflog_expiry_arg *arg = cb_data;
	uint64_t ts = reftable_stack_next_update_index(arg->stack);
	uint64_t live_records = 0;
	size_t i;
	int ret;

	for (i = 0; i < arg->len; i++)
		if (arg->records[i].value_type == REFTABLE_LOG_UPDATE)
			live_records++;

	reftable_writer_set_limits(writer, ts, ts);

	if (!is_null_oid(&arg->update_oid)) {
		struct reftable_ref_record ref = {0};
		struct object_id peeled;

		ref.refname = (char *)arg->refname;
		ref.update_index = ts;

		if (!peel_object(&arg->update_oid, &peeled)) {
			ref.value_type = REFTABLE_REF_VAL2;
			memcpy(ref.value.val2.target_value, peeled.hash, GIT_MAX_RAWSZ);
			memcpy(ref.value.val2.value, arg->update_oid.hash, GIT_MAX_RAWSZ);
		} else {
			ref.value_type = REFTABLE_REF_VAL1;
			memcpy(ref.value.val1, arg->update_oid.hash, GIT_MAX_RAWSZ);
		}

		ret = reftable_writer_add_ref(writer, &ref);
		if (ret < 0)
			return ret;
	}

	/*
	 * When there are no more entries left in the reflog we empty it
	 * completely, but write a placeholder reflog entry that indicates that
	 * the reflog still exists.
	 */
	if (!live_records) {
		struct reftable_log_record log = {
			.refname = (char *)arg->refname,
			.value_type = REFTABLE_LOG_UPDATE,
			.update_index = ts,
		};

		ret = reftable_writer_add_log(writer, &log);
		if (ret)
			return ret;
	}

	for (i = 0; i < arg->len; i++) {
		ret = reftable_writer_add_log(writer, &arg->records[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int reftable_be_reflog_expire(struct ref_store *ref_store,
				     const char *refname,
				     unsigned int flags,
				     reflog_expiry_prepare_fn prepare_fn,
				     reflog_expiry_should_prune_fn should_prune_fn,
				     reflog_expiry_cleanup_fn cleanup_fn,
				     void *policy_cb_data)
{
	/*
	 * For log expiry, we write tombstones for every single reflog entry
	 * that is to be expired. This means that the entries are still
	 * retrievable by delving into the stack, and expiring entries
	 * paradoxically takes extra memory. This memory is only reclaimed when
	 * compacting the reftable stack.
	 *
	 * It would be better if the refs backend supported an API that sets a
	 * criterion for all refs, passing the criterion to pack_refs().
	 *
	 * On the plus side, because we do the expiration per ref, we can easily
	 * insert the reflog existence dummies.
	 */
	struct reftable_ref_store *refs =
		reftable_be_downcast(ref_store, REF_STORE_WRITE, "reflog_expire");
	struct reftable_stack *stack = stack_for(refs, refname, &refname);
	struct reftable_merged_table *mt = reftable_stack_merged_table(stack);
	struct reftable_log_record *logs = NULL;
	struct reftable_log_record *rewritten = NULL;
	struct reftable_ref_record ref_record = {0};
	struct reftable_iterator it = {0};
	struct reftable_addition *add = NULL;
	struct reflog_expiry_arg arg = {0};
	struct object_id oid = {0};
	uint8_t *last_hash = NULL;
	size_t logs_nr = 0, logs_alloc = 0, i;
	int ret;

	if (refs->err < 0)
		return refs->err;

	ret = reftable_stack_reload(stack);
	if (ret < 0)
		goto done;

	ret = reftable_merged_table_seek_log(mt, &it, refname);
	if (ret < 0)
		goto done;

	ret = reftable_stack_new_addition(&add, stack);
	if (ret < 0)
		goto done;

	ret = reftable_stack_read_ref(stack, refname, &ref_record);
	if (ret < 0)
		goto done;
	if (reftable_ref_record_val1(&ref_record))
		oidread(&oid, reftable_ref_record_val1(&ref_record));
	prepare_fn(refname, &oid, policy_cb_data);

	while (1) {
		struct reftable_log_record log = {0};
		struct object_id old_oid, new_oid;

		ret = reftable_iterator_next_log(&it, &log);
		if (ret < 0)
			goto done;
		if (ret > 0 || strcmp(log.refname, refname)) {
			reftable_log_record_release(&log);
			break;
		}

		oidread(&old_oid, log.value.update.old_hash);
		oidread(&new_oid, log.value.update.new_hash);

		/*
		 * Skip over the reflog existence marker. We will add it back
		 * in when there are no live reflog records.
		 */
		if (is_null_oid(&old_oid) && is_null_oid(&new_oid)) {
			reftable_log_record_release(&log);
			continue;
		}

		ALLOC_GROW(logs, logs_nr + 1, logs_alloc);
		logs[logs_nr++] = log;
	}

	/*
	 * We need to rewrite all reflog entries according to the pruning
	 * callback function:
	 *
	 *   - If a reflog entry shall be pruned we mark the record for
	 *     deletion.
	 *
	 *   - Otherwise we may have to rewrite the chain of reflog entries so
	 *     that gaps created by just-deleted records get backfilled.
	 */
	CALLOC_ARRAY(rewritten, logs_nr);
	for (i = logs_nr; i--;) {
		struct reftable_log_record *dest = &rewritten[i];
		struct object_id old_oid, new_oid;

		*dest = logs[i];
		oidread(&old_oid, logs[i].value.update.old_hash);
		oidread(&new_oid, logs[i].value.update.new_hash);

		if (should_prune_fn(&old_oid, &new_oid, logs[i].value.update.email,
				    (timestamp_t)logs[i].value.update.time,
				    logs[i].value.update.tz_offset,
				    logs[i].value.update.message,
				    policy_cb_data)) {
			dest->value_type = REFTABLE_LOG_DELETION;
		} else {
			if ((flags & EXPIRE_REFLOGS_REWRITE) && last_hash)
				memcpy(dest->value.update.old_hash, last_hash, GIT_MAX_RAWSZ);
			last_hash = logs[i].value.update.new_hash;
		}
	}

	if (flags & EXPIRE_REFLOGS_UPDATE_REF && last_hash &&
	    reftable_ref_record_val1(&ref_record))
		oidread(&arg.update_oid, last_hash);

	arg.records = rewritten;
	arg.len = logs_nr;
	arg.stack = stack,
	arg.refname = refname,

	ret = reftable_addition_add(add, &write_reflog_expiry_table, &arg);
	if (ret < 0)
		goto done;

	/*
	 * Future improvement: we could skip writing records that were
	 * not changed.
	 */
	if (!(flags & EXPIRE_REFLOGS_DRY_RUN))
		ret = reftable_addition_commit(add);

done:
	if (add)
		cleanup_fn(policy_cb_data);
	assert(ret != REFTABLE_API_ERROR);

	reftable_ref_record_release(&ref_record);
	reftable_iterator_destroy(&it);
	reftable_addition_destroy(add);
	for (i = 0; i < logs_nr; i++)
		reftable_log_record_release(&logs[i]);
	free(logs);
	free(rewritten);
	return ret;
}

struct ref_storage_be refs_be_reftable = {
	.name = "reftable",
	.init = reftable_be_init,
	.init_db = reftable_be_init_db,
	.transaction_prepare = reftable_be_transaction_prepare,
	.transaction_finish = reftable_be_transaction_finish,
	.transaction_abort = reftable_be_transaction_abort,
	.initial_transaction_commit = reftable_be_initial_transaction_commit,

	.pack_refs = reftable_be_pack_refs,
	.create_symref = reftable_be_create_symref,
	.rename_ref = reftable_be_rename_ref,
	.copy_ref = reftable_be_copy_ref,

	.iterator_begin = reftable_be_iterator_begin,
	.read_raw_ref = reftable_be_read_raw_ref,
	.read_symbolic_ref = reftable_be_read_symbolic_ref,

	.reflog_iterator_begin = reftable_be_reflog_iterator_begin,
	.for_each_reflog_ent = reftable_be_for_each_reflog_ent,
	.for_each_reflog_ent_reverse = reftable_be_for_each_reflog_ent_reverse,
	.reflog_exists = reftable_be_reflog_exists,
	.create_reflog = reftable_be_create_reflog,
	.delete_reflog = reftable_be_delete_reflog,
	.reflog_expire = reftable_be_reflog_expire,
};
