#include "git-compat-util.h"
#include "hex.h"
#include "refs-internal.h"
#include "string-list.h"
#include "trace.h"

static struct trace_key trace_refs = TRACE_KEY_INIT(REFS);

struct debug_ref_store {
	struct ref_store base;
	struct ref_store *refs;
};

extern struct ref_storage_be refs_be_debug;

struct ref_store *maybe_debug_wrap_ref_store(const char *gitdir, struct ref_store *store)
{
	struct debug_ref_store *res;
	struct ref_storage_be *be_copy;

	if (!trace_want(&trace_refs)) {
		return store;
	}
	res = xmalloc(sizeof(struct debug_ref_store));
	be_copy = xmalloc(sizeof(*be_copy));
	*be_copy = refs_be_debug;
	/* we never deallocate backends, so safe to copy the pointer. */
	be_copy->name = store->be->name;
	trace_printf_key(&trace_refs, "ref_store for %s\n", gitdir);
	res->refs = store;
	base_ref_store_init((struct ref_store *)res, store->repo, gitdir,
			    be_copy);
	return (struct ref_store *)res;
}

static int debug_init_db(struct ref_store *refs, int flags, struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)refs;
	int res = drefs->refs->be->init_db(drefs->refs, flags, err);
	trace_printf_key(&trace_refs, "init_db: %d\n", res);
	return res;
}

static int debug_transaction_prepare(struct ref_store *refs,
				     struct ref_transaction *transaction,
				     struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)refs;
	int res;
	transaction->ref_store = drefs->refs;
	res = drefs->refs->be->transaction_prepare(drefs->refs, transaction,
						   err);
	trace_printf_key(&trace_refs, "transaction_prepare: %d \"%s\"\n", res,
			 err->buf);
	return res;
}

static void print_update(int i, const char *refname,
			 const struct object_id *old_oid,
			 const struct object_id *new_oid, unsigned int flags,
			 unsigned int type, const char *msg)
{
	char o[GIT_MAX_HEXSZ + 1] = "null";
	char n[GIT_MAX_HEXSZ + 1] = "null";
	if (old_oid)
		oid_to_hex_r(o, old_oid);
	if (new_oid)
		oid_to_hex_r(n, new_oid);

	type &= 0xf; /* see refs.h REF_* */
	flags &= REF_HAVE_NEW | REF_HAVE_OLD | REF_NO_DEREF |
		REF_FORCE_CREATE_REFLOG;
	trace_printf_key(&trace_refs, "%d: %s %s -> %s (F=0x%x, T=0x%x) \"%s\"\n", i, refname,
		o, n, flags, type, msg);
}

static void print_transaction(struct ref_transaction *transaction)
{
	int i;
	trace_printf_key(&trace_refs, "transaction {\n");
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *u = transaction->updates[i];
		print_update(i, u->refname, &u->old_oid, &u->new_oid, u->flags,
			     u->type, u->msg);
	}
	trace_printf_key(&trace_refs, "}\n");
}

static int debug_transaction_finish(struct ref_store *refs,
				    struct ref_transaction *transaction,
				    struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)refs;
	int res;
	transaction->ref_store = drefs->refs;
	res = drefs->refs->be->transaction_finish(drefs->refs, transaction,
						  err);
	print_transaction(transaction);
	trace_printf_key(&trace_refs, "finish: %d\n", res);
	return res;
}

static int debug_transaction_abort(struct ref_store *refs,
				   struct ref_transaction *transaction,
				   struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)refs;
	int res;
	transaction->ref_store = drefs->refs;
	res = drefs->refs->be->transaction_abort(drefs->refs, transaction, err);
	return res;
}

static int debug_initial_transaction_commit(struct ref_store *refs,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)refs;
	int res;
	transaction->ref_store = drefs->refs;
	res = drefs->refs->be->initial_transaction_commit(drefs->refs,
							  transaction, err);
	return res;
}

static int debug_pack_refs(struct ref_store *ref_store, struct pack_refs_opts *opts)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->pack_refs(drefs->refs, opts);
	trace_printf_key(&trace_refs, "pack_refs: %d\n", res);
	return res;
}

static int debug_create_symref(struct ref_store *ref_store,
			       const char *ref_name, const char *target,
			       const char *logmsg)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->create_symref(drefs->refs, ref_name, target,
						 logmsg);
	trace_printf_key(&trace_refs, "create_symref: %s -> %s \"%s\": %d\n", ref_name,
		target, logmsg, res);
	return res;
}

static int debug_rename_ref(struct ref_store *ref_store, const char *oldref,
			    const char *newref, const char *logmsg)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->rename_ref(drefs->refs, oldref, newref,
					      logmsg);
	trace_printf_key(&trace_refs, "rename_ref: %s -> %s \"%s\": %d\n", oldref, newref,
		logmsg, res);
	return res;
}

static int debug_copy_ref(struct ref_store *ref_store, const char *oldref,
			  const char *newref, const char *logmsg)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res =
		drefs->refs->be->copy_ref(drefs->refs, oldref, newref, logmsg);
	trace_printf_key(&trace_refs, "copy_ref: %s -> %s \"%s\": %d\n", oldref, newref,
		logmsg, res);
	return res;
}

struct debug_ref_iterator {
	struct ref_iterator base;
	struct ref_iterator *iter;
};

static int debug_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct debug_ref_iterator *diter =
		(struct debug_ref_iterator *)ref_iterator;
	int res = diter->iter->vtable->advance(diter->iter);
	if (res)
		trace_printf_key(&trace_refs, "iterator_advance: (%d)\n", res);
	else
		trace_printf_key(&trace_refs, "iterator_advance: %s (0)\n",
			diter->iter->refname);

	diter->base.ordered = diter->iter->ordered;
	diter->base.refname = diter->iter->refname;
	diter->base.oid = diter->iter->oid;
	diter->base.flags = diter->iter->flags;
	return res;
}

static int debug_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct debug_ref_iterator *diter =
		(struct debug_ref_iterator *)ref_iterator;
	int res = diter->iter->vtable->peel(diter->iter, peeled);
	trace_printf_key(&trace_refs, "iterator_peel: %s: %d\n", diter->iter->refname, res);
	return res;
}

static int debug_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct debug_ref_iterator *diter =
		(struct debug_ref_iterator *)ref_iterator;
	int res = diter->iter->vtable->abort(diter->iter);
	trace_printf_key(&trace_refs, "iterator_abort: %d\n", res);
	return res;
}

static struct ref_iterator_vtable debug_ref_iterator_vtable = {
	.advance = debug_ref_iterator_advance,
	.peel = debug_ref_iterator_peel,
	.abort = debug_ref_iterator_abort,
};

static struct ref_iterator *
debug_ref_iterator_begin(struct ref_store *ref_store, const char *prefix,
			 const char **exclude_patterns, unsigned int flags)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct ref_iterator *res =
		drefs->refs->be->iterator_begin(drefs->refs, prefix,
						exclude_patterns, flags);
	struct debug_ref_iterator *diter = xcalloc(1, sizeof(*diter));
	base_ref_iterator_init(&diter->base, &debug_ref_iterator_vtable, 1);
	diter->iter = res;
	trace_printf_key(&trace_refs, "ref_iterator_begin: \"%s\" (0x%x)\n",
			 prefix, flags);
	return &diter->base;
}

static int debug_read_raw_ref(struct ref_store *ref_store, const char *refname,
			      struct object_id *oid, struct strbuf *referent,
			      unsigned int *type, int *failure_errno)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = 0;

	oidcpy(oid, null_oid());
	res = drefs->refs->be->read_raw_ref(drefs->refs, refname, oid, referent,
					    type, failure_errno);

	if (res == 0) {
		trace_printf_key(&trace_refs, "read_raw_ref: %s: %s (=> %s) type %x: %d\n",
			refname, oid_to_hex(oid), referent->buf, *type, res);
	} else {
		trace_printf_key(&trace_refs,
				 "read_raw_ref: %s: %d (errno %d)\n", refname,
				 res, *failure_errno);
	}
	return res;
}

static int debug_read_symbolic_ref(struct ref_store *ref_store, const char *refname,
				   struct strbuf *referent)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct ref_store *refs = drefs->refs;
	int res;

	res = refs->be->read_symbolic_ref(refs, refname, referent);
	if (!res)
		trace_printf_key(&trace_refs, "read_symbolic_ref: %s: (%s)\n",
				 refname, referent->buf);
	else
		trace_printf_key(&trace_refs,
				 "read_symbolic_ref: %s: %d\n", refname, res);
	return res;

}

static struct ref_iterator *
debug_reflog_iterator_begin(struct ref_store *ref_store)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct ref_iterator *res =
		drefs->refs->be->reflog_iterator_begin(drefs->refs);
	trace_printf_key(&trace_refs, "for_each_reflog_iterator_begin\n");
	return res;
}

struct debug_reflog {
	const char *refname;
	each_reflog_ent_fn *fn;
	void *cb_data;
};

static int debug_print_reflog_ent(struct object_id *old_oid,
				  struct object_id *new_oid,
				  const char *committer, timestamp_t timestamp,
				  int tz, const char *msg, void *cb_data)
{
	struct debug_reflog *dbg = (struct debug_reflog *)cb_data;
	int ret;
	char o[GIT_MAX_HEXSZ + 1] = "null";
	char n[GIT_MAX_HEXSZ + 1] = "null";
	char *msgend = strchrnul(msg, '\n');
	if (old_oid)
		oid_to_hex_r(o, old_oid);
	if (new_oid)
		oid_to_hex_r(n, new_oid);

	ret = dbg->fn(old_oid, new_oid, committer, timestamp, tz, msg,
		      dbg->cb_data);
	trace_printf_key(&trace_refs,
			 "reflog_ent %s (ret %d): %s -> %s, %s %ld \"%.*s\"\n",
			 dbg->refname, ret, o, n, committer,
			 (long int)timestamp, (int)(msgend - msg), msg);
	return ret;
}

static int debug_for_each_reflog_ent(struct ref_store *ref_store,
				     const char *refname, each_reflog_ent_fn fn,
				     void *cb_data)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct debug_reflog dbg = {
		.refname = refname,
		.fn = fn,
		.cb_data = cb_data,
	};

	int res = drefs->refs->be->for_each_reflog_ent(
		drefs->refs, refname, &debug_print_reflog_ent, &dbg);
	trace_printf_key(&trace_refs, "for_each_reflog: %s: %d\n", refname, res);
	return res;
}

static int debug_for_each_reflog_ent_reverse(struct ref_store *ref_store,
					     const char *refname,
					     each_reflog_ent_fn fn,
					     void *cb_data)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct debug_reflog dbg = {
		.refname = refname,
		.fn = fn,
		.cb_data = cb_data,
	};
	int res = drefs->refs->be->for_each_reflog_ent_reverse(
		drefs->refs, refname, &debug_print_reflog_ent, &dbg);
	trace_printf_key(&trace_refs, "for_each_reflog_reverse: %s: %d\n", refname, res);
	return res;
}

static int debug_reflog_exists(struct ref_store *ref_store, const char *refname)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->reflog_exists(drefs->refs, refname);
	trace_printf_key(&trace_refs, "reflog_exists: %s: %d\n", refname, res);
	return res;
}

static int debug_create_reflog(struct ref_store *ref_store, const char *refname,
			       struct strbuf *err)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->create_reflog(drefs->refs, refname, err);
	trace_printf_key(&trace_refs, "create_reflog: %s: %d\n", refname, res);
	return res;
}

static int debug_delete_reflog(struct ref_store *ref_store, const char *refname)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	int res = drefs->refs->be->delete_reflog(drefs->refs, refname);
	trace_printf_key(&trace_refs, "delete_reflog: %s: %d\n", refname, res);
	return res;
}

struct debug_reflog_expiry_should_prune {
	reflog_expiry_prepare_fn *prepare;
	reflog_expiry_should_prune_fn *should_prune;
	reflog_expiry_cleanup_fn *cleanup;
	void *cb_data;
};

static void debug_reflog_expiry_prepare(const char *refname,
					const struct object_id *oid,
					void *cb_data)
{
	struct debug_reflog_expiry_should_prune *prune = cb_data;
	trace_printf_key(&trace_refs, "reflog_expire_prepare: %s\n", refname);
	prune->prepare(refname, oid, prune->cb_data);
}

static int debug_reflog_expiry_should_prune_fn(struct object_id *ooid,
					       struct object_id *noid,
					       const char *email,
					       timestamp_t timestamp, int tz,
					       const char *message, void *cb_data) {
	struct debug_reflog_expiry_should_prune *prune = cb_data;

	int result = prune->should_prune(ooid, noid, email, timestamp, tz, message, prune->cb_data);
	trace_printf_key(&trace_refs, "reflog_expire_should_prune: %s %ld: %d\n", message, (long int) timestamp, result);
	return result;
}

static void debug_reflog_expiry_cleanup(void *cb_data)
{
	struct debug_reflog_expiry_should_prune *prune = cb_data;
	prune->cleanup(prune->cb_data);
}

static int debug_reflog_expire(struct ref_store *ref_store, const char *refname,
			       unsigned int flags,
			       reflog_expiry_prepare_fn prepare_fn,
			       reflog_expiry_should_prune_fn should_prune_fn,
			       reflog_expiry_cleanup_fn cleanup_fn,
			       void *policy_cb_data)
{
	struct debug_ref_store *drefs = (struct debug_ref_store *)ref_store;
	struct debug_reflog_expiry_should_prune prune = {
		.prepare = prepare_fn,
		.cleanup = cleanup_fn,
		.should_prune = should_prune_fn,
		.cb_data = policy_cb_data,
	};
	int res = drefs->refs->be->reflog_expire(drefs->refs, refname,
						 flags, &debug_reflog_expiry_prepare,
						 &debug_reflog_expiry_should_prune_fn,
						 &debug_reflog_expiry_cleanup,
						 &prune);
	trace_printf_key(&trace_refs, "reflog_expire: %s: %d\n", refname, res);
	return res;
}

struct ref_storage_be refs_be_debug = {
	.name = "debug",
	.init = NULL,
	.init_db = debug_init_db,

	/*
	 * None of these should be NULL. If the "files" backend (in
	 * "struct ref_storage_be refs_be_files" in files-backend.c)
	 * has a function we should also have a wrapper for it here.
	 * Test the output with "GIT_TRACE_REFS=1".
	 */
	.transaction_prepare = debug_transaction_prepare,
	.transaction_finish = debug_transaction_finish,
	.transaction_abort = debug_transaction_abort,
	.initial_transaction_commit = debug_initial_transaction_commit,

	.pack_refs = debug_pack_refs,
	.create_symref = debug_create_symref,
	.rename_ref = debug_rename_ref,
	.copy_ref = debug_copy_ref,

	.iterator_begin = debug_ref_iterator_begin,
	.read_raw_ref = debug_read_raw_ref,
	.read_symbolic_ref = debug_read_symbolic_ref,

	.reflog_iterator_begin = debug_reflog_iterator_begin,
	.for_each_reflog_ent = debug_for_each_reflog_ent,
	.for_each_reflog_ent_reverse = debug_for_each_reflog_ent_reverse,
	.reflog_exists = debug_reflog_exists,
	.create_reflog = debug_create_reflog,
	.delete_reflog = debug_delete_reflog,
	.reflog_expire = debug_reflog_expire,
};
