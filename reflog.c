#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "odb.h"
#include "reflog.h"
#include "refs.h"
#include "revision.h"
#include "tree.h"
#include "tree-walk.h"
#include "wildmatch.h"

static struct reflog_expire_entry_option *find_cfg_ent(struct reflog_expire_options *opts,
						       const char *pattern, size_t len)
{
	struct reflog_expire_entry_option *ent;

	if (!opts->entries_tail)
		opts->entries_tail = &opts->entries;

	for (ent = opts->entries; ent; ent = ent->next)
		if (!xstrncmpz(ent->pattern, pattern, len))
			return ent;

	FLEX_ALLOC_MEM(ent, pattern, pattern, len);
	*opts->entries_tail = ent;
	opts->entries_tail = &(ent->next);
	return ent;
}

int reflog_expire_config(const char *var, const char *value,
			 const struct config_context *ctx, void *cb)
{
	struct reflog_expire_options *opts = cb;
	const char *pattern, *key;
	size_t pattern_len;
	timestamp_t expire;
	int slot;
	struct reflog_expire_entry_option *ent;

	if (parse_config_key(var, "gc", &pattern, &pattern_len, &key) < 0)
		return git_default_config(var, value, ctx, cb);

	if (!strcmp(key, "reflogexpire")) {
		slot = REFLOG_EXPIRE_TOTAL;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else if (!strcmp(key, "reflogexpireunreachable")) {
		slot = REFLOG_EXPIRE_UNREACH;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else
		return git_default_config(var, value, ctx, cb);

	if (!pattern) {
		switch (slot) {
		case REFLOG_EXPIRE_TOTAL:
			opts->default_expire_total = expire;
			break;
		case REFLOG_EXPIRE_UNREACH:
			opts->default_expire_unreachable = expire;
			break;
		}
		return 0;
	}

	ent = find_cfg_ent(opts, pattern, pattern_len);
	if (!ent)
		return -1;
	switch (slot) {
	case REFLOG_EXPIRE_TOTAL:
		ent->expire_total = expire;
		break;
	case REFLOG_EXPIRE_UNREACH:
		ent->expire_unreachable = expire;
		break;
	}
	return 0;
}

void reflog_expire_options_set_refname(struct reflog_expire_options *cb,
				       const char *ref)
{
	struct reflog_expire_entry_option *ent;

	if (cb->explicit_expiry == (REFLOG_EXPIRE_TOTAL|REFLOG_EXPIRE_UNREACH))
		return; /* both given explicitly -- nothing to tweak */

	for (ent = cb->entries; ent; ent = ent->next) {
		if (!wildmatch(ent->pattern, ref, 0)) {
			if (!(cb->explicit_expiry & REFLOG_EXPIRE_TOTAL))
				cb->expire_total = ent->expire_total;
			if (!(cb->explicit_expiry & REFLOG_EXPIRE_UNREACH))
				cb->expire_unreachable = ent->expire_unreachable;
			return;
		}
	}

	/*
	 * If unconfigured, make stash never expire
	 */
	if (!strcmp(ref, "refs/stash")) {
		if (!(cb->explicit_expiry & REFLOG_EXPIRE_TOTAL))
			cb->expire_total = 0;
		if (!(cb->explicit_expiry & REFLOG_EXPIRE_UNREACH))
			cb->expire_unreachable = 0;
		return;
	}

	/* Nothing matched -- use the default value */
	if (!(cb->explicit_expiry & REFLOG_EXPIRE_TOTAL))
		cb->expire_total = cb->default_expire_total;
	if (!(cb->explicit_expiry & REFLOG_EXPIRE_UNREACH))
		cb->expire_unreachable = cb->default_expire_unreachable;
}

/* Remember to update object flag allocation in object.h */
#define INCOMPLETE	(1u<<10)
#define STUDYING	(1u<<11)
#define REACHABLE	(1u<<12)

static int tree_is_complete(const struct object_id *oid)
{
	struct tree_desc desc;
	struct name_entry entry;
	int complete;
	struct tree *tree;

	tree = lookup_tree(the_repository, oid);
	if (!tree)
		return 0;
	if (tree->object.flags & SEEN)
		return 1;
	if (tree->object.flags & INCOMPLETE)
		return 0;

	if (!tree->buffer) {
		enum object_type type;
		unsigned long size;
		void *data = odb_read_object(the_repository->objects, oid,
					     &type, &size);
		if (!data) {
			tree->object.flags |= INCOMPLETE;
			return 0;
		}
		tree->buffer = data;
		tree->size = size;
	}
	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	complete = 1;
	while (tree_entry(&desc, &entry)) {
		if (!odb_has_object(the_repository->objects, &entry.oid,
				HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR) ||
		    (S_ISDIR(entry.mode) && !tree_is_complete(&entry.oid))) {
			tree->object.flags |= INCOMPLETE;
			complete = 0;
		}
	}
	free_tree_buffer(tree);

	if (complete)
		tree->object.flags |= SEEN;
	return complete;
}

static int commit_is_complete(struct commit *commit)
{
	struct object_array study;
	struct object_array found;
	int is_incomplete = 0;
	int i;

	/* early return */
	if (commit->object.flags & SEEN)
		return 1;
	if (commit->object.flags & INCOMPLETE)
		return 0;
	/*
	 * Find all commits that are reachable and are not marked as
	 * SEEN.  Then make sure the trees and blobs contained are
	 * complete.  After that, mark these commits also as SEEN.
	 * If some of the objects that are needed to complete this
	 * commit are missing, mark this commit as INCOMPLETE.
	 */
	memset(&study, 0, sizeof(study));
	memset(&found, 0, sizeof(found));
	add_object_array(&commit->object, NULL, &study);
	add_object_array(&commit->object, NULL, &found);
	commit->object.flags |= STUDYING;
	while (study.nr) {
		struct commit *c;
		struct commit_list *parent;

		c = (struct commit *)object_array_pop(&study);
		if (!c->object.parsed && !parse_object(the_repository, &c->object.oid))
			c->object.flags |= INCOMPLETE;

		if (c->object.flags & INCOMPLETE) {
			is_incomplete = 1;
			break;
		}
		else if (c->object.flags & SEEN)
			continue;
		for (parent = c->parents; parent; parent = parent->next) {
			struct commit *p = parent->item;
			if (p->object.flags & STUDYING)
				continue;
			p->object.flags |= STUDYING;
			add_object_array(&p->object, NULL, &study);
			add_object_array(&p->object, NULL, &found);
		}
	}
	if (!is_incomplete) {
		/*
		 * make sure all commits in "found" array have all the
		 * necessary objects.
		 */
		for (i = 0; i < found.nr; i++) {
			struct commit *c =
				(struct commit *)found.objects[i].item;
			if (!tree_is_complete(get_commit_tree_oid(c))) {
				is_incomplete = 1;
				c->object.flags |= INCOMPLETE;
			}
		}
		if (!is_incomplete) {
			/* mark all found commits as complete, iow SEEN */
			for (i = 0; i < found.nr; i++)
				found.objects[i].item->flags |= SEEN;
		}
	}
	/* clear flags from the objects we traversed */
	for (i = 0; i < found.nr; i++)
		found.objects[i].item->flags &= ~STUDYING;
	if (is_incomplete)
		commit->object.flags |= INCOMPLETE;
	else {
		/*
		 * If we come here, we have (1) traversed the ancestry chain
		 * from the "commit" until we reach SEEN commits (which are
		 * known to be complete), and (2) made sure that the commits
		 * encountered during the above traversal refer to trees that
		 * are complete.  Which means that we know *all* the commits
		 * we have seen during this process are complete.
		 */
		for (i = 0; i < found.nr; i++)
			found.objects[i].item->flags |= SEEN;
	}
	/* free object arrays */
	object_array_clear(&study);
	object_array_clear(&found);
	return !is_incomplete;
}

static int keep_entry(struct commit **it, struct object_id *oid)
{
	struct commit *commit;

	if (is_null_oid(oid))
		return 1;
	commit = lookup_commit_reference_gently(the_repository, oid, 1);
	if (!commit)
		return 0;

	/*
	 * Make sure everything in this commit exists.
	 *
	 * We have walked all the objects reachable from the refs
	 * and cache earlier.  The commits reachable by this commit
	 * must meet SEEN commits -- and then we should mark them as
	 * SEEN as well.
	 */
	if (!commit_is_complete(commit))
		return 0;
	*it = commit;
	return 1;
}

/*
 * Starting from commits in the cb->mark_list, mark commits that are
 * reachable from them.  Stop the traversal at commits older than
 * the expire_limit and queue them back, so that the caller can call
 * us again to restart the traversal with longer expire_limit.
 */
static void mark_reachable(struct expire_reflog_policy_cb *cb)
{
	struct commit_list *pending;
	timestamp_t expire_limit = cb->mark_limit;
	struct commit_list *leftover = NULL;

	for (pending = cb->mark_list; pending; pending = pending->next)
		pending->item->object.flags &= ~REACHABLE;

	pending = cb->mark_list;
	while (pending) {
		struct commit_list *parent;
		struct commit *commit = pop_commit(&pending);
		if (commit->object.flags & REACHABLE)
			continue;
		if (repo_parse_commit(the_repository, commit))
			continue;
		commit->object.flags |= REACHABLE;
		if (commit->date < expire_limit) {
			commit_list_insert(commit, &leftover);
			continue;
		}
		parent = commit->parents;
		while (parent) {
			commit = parent->item;
			parent = parent->next;
			if (commit->object.flags & REACHABLE)
				continue;
			commit_list_insert(commit, &pending);
		}
	}
	cb->mark_list = leftover;
}

static int is_unreachable(struct expire_reflog_policy_cb *cb, struct commit *commit, struct object_id *oid)
{
	/*
	 * We may or may not have the commit yet - if not, look it
	 * up using the supplied sha1.
	 */
	if (!commit) {
		if (is_null_oid(oid))
			return 0;

		commit = lookup_commit_reference_gently(the_repository, oid,
							1);

		/* Not a commit -- keep it */
		if (!commit)
			return 0;
	}

	/* Reachable from the current ref?  Don't prune. */
	if (commit->object.flags & REACHABLE)
		return 0;

	if (cb->mark_list && cb->mark_limit) {
		cb->mark_limit = 0; /* dig down to the root */
		mark_reachable(cb);
	}

	return !(commit->object.flags & REACHABLE);
}

/*
 * Return true iff the specified reflog entry should be expired.
 */
int should_expire_reflog_ent(struct object_id *ooid, struct object_id *noid,
			     const char *email UNUSED,
			     timestamp_t timestamp, int tz UNUSED,
			     const char *message UNUSED, void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct commit *old_commit, *new_commit;

	if (timestamp < cb->opts.expire_total)
		return 1;

	old_commit = new_commit = NULL;
	if (cb->opts.stalefix &&
	    (!keep_entry(&old_commit, ooid) || !keep_entry(&new_commit, noid)))
		return 1;

	if (timestamp < cb->opts.expire_unreachable) {
		switch (cb->unreachable_expire_kind) {
		case UE_ALWAYS:
			return 1;
		case UE_NORMAL:
		case UE_HEAD:
			if (is_unreachable(cb, old_commit, ooid) || is_unreachable(cb, new_commit, noid))
				return 1;
			break;
		}
	}

	if (cb->opts.recno && --(cb->opts.recno) == 0)
		return 1;

	return 0;
}

int should_expire_reflog_ent_verbose(struct object_id *ooid,
				     struct object_id *noid,
				     const char *email,
				     timestamp_t timestamp, int tz,
				     const char *message, void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	int expire;

	expire = should_expire_reflog_ent(ooid, noid, email, timestamp, tz,
					  message, cb);

	if (!expire)
		printf("keep %s", message);
	else if (cb->dry_run)
		printf("would prune %s", message);
	else
		printf("prune %s", message);

	return expire;
}

static int push_tip_to_list(const char *refname UNUSED,
			    const char *referent UNUSED,
			    const struct object_id *oid,
			    int flags, void *cb_data)
{
	struct commit_list **list = cb_data;
	struct commit *tip_commit;
	if (flags & REF_ISSYMREF)
		return 0;
	tip_commit = lookup_commit_reference_gently(the_repository, oid, 1);
	if (!tip_commit)
		return 0;
	commit_list_insert(tip_commit, list);
	return 0;
}

static int is_head(const char *refname)
{
	const char *stripped_refname;
	parse_worktree_ref(refname, NULL, NULL, &stripped_refname);
	return !strcmp(stripped_refname, "HEAD");
}

void reflog_expiry_prepare(const char *refname,
			   const struct object_id *oid,
			   void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct commit_list *elem;
	struct commit *commit = NULL;

	if (!cb->opts.expire_unreachable || is_head(refname)) {
		cb->unreachable_expire_kind = UE_HEAD;
	} else {
		commit = lookup_commit_reference_gently(the_repository,
							oid, 1);
		if (commit && is_null_oid(&commit->object.oid))
			commit = NULL;
		cb->unreachable_expire_kind = commit ? UE_NORMAL : UE_ALWAYS;
	}

	if (cb->opts.expire_unreachable <= cb->opts.expire_total)
		cb->unreachable_expire_kind = UE_ALWAYS;

	switch (cb->unreachable_expire_kind) {
	case UE_ALWAYS:
		return;
	case UE_HEAD:
		refs_for_each_ref(get_main_ref_store(the_repository),
				  push_tip_to_list, &cb->tips);
		for (elem = cb->tips; elem; elem = elem->next)
			commit_list_insert(elem->item, &cb->mark_list);
		break;
	case UE_NORMAL:
		commit_list_insert(commit, &cb->mark_list);
		/* For reflog_expiry_cleanup() below */
		cb->tip_commit = commit;
	}
	cb->mark_limit = cb->opts.expire_total;
	mark_reachable(cb);
}

void reflog_expiry_cleanup(void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct commit_list *elem;

	switch (cb->unreachable_expire_kind) {
	case UE_ALWAYS:
		return;
	case UE_HEAD:
		for (elem = cb->tips; elem; elem = elem->next)
			clear_commit_marks(elem->item, REACHABLE);
		free_commit_list(cb->tips);
		break;
	case UE_NORMAL:
		clear_commit_marks(cb->tip_commit, REACHABLE);
		break;
	}
	for (elem = cb->mark_list; elem; elem = elem->next)
		clear_commit_marks(elem->item, REACHABLE);
	free_commit_list(cb->mark_list);
}

int count_reflog_ent(struct object_id *ooid UNUSED,
		     struct object_id *noid UNUSED,
		     const char *email UNUSED,
		     timestamp_t timestamp, int tz UNUSED,
		     const char *message UNUSED, void *cb_data)
{
	struct reflog_expire_options *cb = cb_data;
	if (!cb->expire_total || timestamp < cb->expire_total)
		cb->recno++;
	return 0;
}

int reflog_delete(const char *rev, enum expire_reflog_flags flags, int verbose)
{
	struct reflog_expire_options opts = { 0 };
	int status = 0;
	reflog_expiry_should_prune_fn *should_prune_fn = should_expire_reflog_ent;
	const char *spec = strstr(rev, "@{");
	char *ep, *ref;
	int recno;
	struct expire_reflog_policy_cb cb = {
		.dry_run = !!(flags & EXPIRE_REFLOGS_DRY_RUN),
	};

	if (verbose)
		should_prune_fn = should_expire_reflog_ent_verbose;

	if (!spec)
		return error(_("not a reflog: %s"), rev);

	if (!repo_dwim_log(the_repository, rev, spec - rev, NULL, &ref)) {
		status |= error(_("no reflog for '%s'"), rev);
		goto cleanup;
	}

	recno = strtoul(spec + 2, &ep, 10);
	if (*ep == '}') {
		opts.recno = -recno;
		refs_for_each_reflog_ent(get_main_ref_store(the_repository),
					 ref, count_reflog_ent, &opts);
	} else {
		opts.expire_total = approxidate(spec + 2);
		refs_for_each_reflog_ent(get_main_ref_store(the_repository),
					 ref, count_reflog_ent, &opts);
		opts.expire_total = 0;
	}

	cb.opts = opts;
	status |= refs_reflog_expire(get_main_ref_store(the_repository), ref,
				     flags,
				     reflog_expiry_prepare,
				     should_prune_fn,
				     reflog_expiry_cleanup,
				     &cb);

 cleanup:
	free(ref);
	return status;
}
