#include "cache.h"
#include "object-store.h"
#include "reflog.h"
#include "refs.h"
#include "revision.h"
#include "worktree.h"

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
		void *data = read_object_file(oid, &type, &size);
		if (!data) {
			tree->object.flags |= INCOMPLETE;
			return 0;
		}
		tree->buffer = data;
		tree->size = size;
	}
	init_tree_desc(&desc, tree->buffer, tree->size);
	complete = 1;
	while (tree_entry(&desc, &entry)) {
		if (!has_object_file(&entry.oid) ||
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

static int cummit_is_complete(struct cummit *cummit)
{
	struct object_array study;
	struct object_array found;
	int is_incomplete = 0;
	int i;

	/* early return */
	if (cummit->object.flags & SEEN)
		return 1;
	if (cummit->object.flags & INCOMPLETE)
		return 0;
	/*
	 * Find all cummits that are reachable and are not marked as
	 * SEEN.  Then make sure the trees and blobs contained are
	 * complete.  After that, mark these cummits also as SEEN.
	 * If some of the objects that are needed to complete this
	 * cummit are missing, mark this cummit as INCOMPLETE.
	 */
	memset(&study, 0, sizeof(study));
	memset(&found, 0, sizeof(found));
	add_object_array(&cummit->object, NULL, &study);
	add_object_array(&cummit->object, NULL, &found);
	cummit->object.flags |= STUDYING;
	while (study.nr) {
		struct cummit *c;
		struct cummit_list *parent;

		c = (struct cummit *)object_array_pop(&study);
		if (!c->object.parsed && !parse_object(the_repository, &c->object.oid))
			c->object.flags |= INCOMPLETE;

		if (c->object.flags & INCOMPLETE) {
			is_incomplete = 1;
			break;
		}
		else if (c->object.flags & SEEN)
			continue;
		for (parent = c->parents; parent; parent = parent->next) {
			struct cummit *p = parent->item;
			if (p->object.flags & STUDYING)
				continue;
			p->object.flags |= STUDYING;
			add_object_array(&p->object, NULL, &study);
			add_object_array(&p->object, NULL, &found);
		}
	}
	if (!is_incomplete) {
		/*
		 * make sure all cummits in "found" array have all the
		 * necessary objects.
		 */
		for (i = 0; i < found.nr; i++) {
			struct cummit *c =
				(struct cummit *)found.objects[i].item;
			if (!tree_is_complete(get_cummit_tree_oid(c))) {
				is_incomplete = 1;
				c->object.flags |= INCOMPLETE;
			}
		}
		if (!is_incomplete) {
			/* mark all found cummits as complete, iow SEEN */
			for (i = 0; i < found.nr; i++)
				found.objects[i].item->flags |= SEEN;
		}
	}
	/* clear flags from the objects we traversed */
	for (i = 0; i < found.nr; i++)
		found.objects[i].item->flags &= ~STUDYING;
	if (is_incomplete)
		cummit->object.flags |= INCOMPLETE;
	else {
		/*
		 * If we come here, we have (1) traversed the ancestry chain
		 * from the "cummit" until we reach SEEN cummits (which are
		 * known to be complete), and (2) made sure that the cummits
		 * encountered during the above traversal refer to trees that
		 * are complete.  Which means that we know *all* the cummits
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

static int keep_entry(struct cummit **it, struct object_id *oid)
{
	struct cummit *cummit;

	if (is_null_oid(oid))
		return 1;
	cummit = lookup_cummit_reference_gently(the_repository, oid, 1);
	if (!cummit)
		return 0;

	/*
	 * Make sure everything in this cummit exists.
	 *
	 * We have walked all the objects reachable from the refs
	 * and cache earlier.  The cummits reachable by this cummit
	 * must meet SEEN cummits -- and then we should mark them as
	 * SEEN as well.
	 */
	if (!cummit_is_complete(cummit))
		return 0;
	*it = cummit;
	return 1;
}

/*
 * Starting from cummits in the cb->mark_list, mark cummits that are
 * reachable from them.  Stop the traversal at cummits older than
 * the expire_limit and queue them back, so that the caller can call
 * us again to restart the traversal with longer expire_limit.
 */
static void mark_reachable(struct expire_reflog_policy_cb *cb)
{
	struct cummit_list *pending;
	timestamp_t expire_limit = cb->mark_limit;
	struct cummit_list *leftover = NULL;

	for (pending = cb->mark_list; pending; pending = pending->next)
		pending->item->object.flags &= ~REACHABLE;

	pending = cb->mark_list;
	while (pending) {
		struct cummit_list *parent;
		struct cummit *cummit = pop_cummit(&pending);
		if (cummit->object.flags & REACHABLE)
			continue;
		if (parse_cummit(cummit))
			continue;
		cummit->object.flags |= REACHABLE;
		if (cummit->date < expire_limit) {
			cummit_list_insert(cummit, &leftover);
			continue;
		}
		cummit->object.flags |= REACHABLE;
		parent = cummit->parents;
		while (parent) {
			cummit = parent->item;
			parent = parent->next;
			if (cummit->object.flags & REACHABLE)
				continue;
			cummit_list_insert(cummit, &pending);
		}
	}
	cb->mark_list = leftover;
}

static int unreachable(struct expire_reflog_policy_cb *cb, struct cummit *cummit, struct object_id *oid)
{
	/*
	 * We may or may not have the cummit yet - if not, look it
	 * up using the supplied sha1.
	 */
	if (!cummit) {
		if (is_null_oid(oid))
			return 0;

		cummit = lookup_cummit_reference_gently(the_repository, oid,
							1);

		/* Not a cummit -- keep it */
		if (!cummit)
			return 0;
	}

	/* Reachable from the current ref?  Don't prune. */
	if (cummit->object.flags & REACHABLE)
		return 0;

	if (cb->mark_list && cb->mark_limit) {
		cb->mark_limit = 0; /* dig down to the root */
		mark_reachable(cb);
	}

	return !(cummit->object.flags & REACHABLE);
}

/*
 * Return true iff the specified reflog entry should be expired.
 */
int should_expire_reflog_ent(struct object_id *ooid, struct object_id *noid,
			     const char *email, timestamp_t timestamp, int tz,
			     const char *message, void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct cummit *old_cummit, *new_cummit;

	if (timestamp < cb->cmd.expire_total)
		return 1;

	old_cummit = new_cummit = NULL;
	if (cb->cmd.stalefix &&
	    (!keep_entry(&old_cummit, ooid) || !keep_entry(&new_cummit, noid)))
		return 1;

	if (timestamp < cb->cmd.expire_unreachable) {
		switch (cb->unreachable_expire_kind) {
		case UE_ALWAYS:
			return 1;
		case UE_NORMAL:
		case UE_HEAD:
			if (unreachable(cb, old_cummit, ooid) || unreachable(cb, new_cummit, noid))
				return 1;
			break;
		}
	}

	if (cb->cmd.recno && --(cb->cmd.recno) == 0)
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

static int push_tip_to_list(const char *refname, const struct object_id *oid,
			    int flags, void *cb_data)
{
	struct cummit_list **list = cb_data;
	struct cummit *tip_cummit;
	if (flags & REF_ISSYMREF)
		return 0;
	tip_cummit = lookup_cummit_reference_gently(the_repository, oid, 1);
	if (!tip_cummit)
		return 0;
	cummit_list_insert(tip_cummit, list);
	return 0;
}

static int is_head(const char *refname)
{
	switch (ref_type(refname)) {
	case REF_TYPE_OTHER_PSEUDOREF:
	case REF_TYPE_MAIN_PSEUDOREF:
		if (parse_worktree_ref(refname, NULL, NULL, &refname))
			BUG("not a worktree ref: %s", refname);
		break;
	default:
		break;
	}
	return !strcmp(refname, "HEAD");
}

void reflog_expiry_prepare(const char *refname,
			   const struct object_id *oid,
			   void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct cummit_list *elem;
	struct cummit *cummit = NULL;

	if (!cb->cmd.expire_unreachable || is_head(refname)) {
		cb->unreachable_expire_kind = UE_HEAD;
	} else {
		cummit = lookup_cummit(the_repository, oid);
		if (cummit && is_null_oid(&cummit->object.oid))
			cummit = NULL;
		cb->unreachable_expire_kind = cummit ? UE_NORMAL : UE_ALWAYS;
	}

	if (cb->cmd.expire_unreachable <= cb->cmd.expire_total)
		cb->unreachable_expire_kind = UE_ALWAYS;

	switch (cb->unreachable_expire_kind) {
	case UE_ALWAYS:
		return;
	case UE_HEAD:
		for_each_ref(push_tip_to_list, &cb->tips);
		for (elem = cb->tips; elem; elem = elem->next)
			cummit_list_insert(elem->item, &cb->mark_list);
		break;
	case UE_NORMAL:
		cummit_list_insert(cummit, &cb->mark_list);
		/* For reflog_expiry_cleanup() below */
		cb->tip_cummit = cummit;
	}
	cb->mark_limit = cb->cmd.expire_total;
	mark_reachable(cb);
}

void reflog_expiry_cleanup(void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct cummit_list *elem;

	switch (cb->unreachable_expire_kind) {
	case UE_ALWAYS:
		return;
	case UE_HEAD:
		for (elem = cb->tips; elem; elem = elem->next)
			clear_cummit_marks(elem->item, REACHABLE);
		free_cummit_list(cb->tips);
		break;
	case UE_NORMAL:
		clear_cummit_marks(cb->tip_cummit, REACHABLE);
		break;
	}
}

int count_reflog_ent(struct object_id *ooid, struct object_id *noid,
		     const char *email, timestamp_t timestamp, int tz,
		     const char *message, void *cb_data)
{
	struct cmd_reflog_expire_cb *cb = cb_data;
	if (!cb->expire_total || timestamp < cb->expire_total)
		cb->recno++;
	return 0;
}

int reflog_delete(const char *rev, enum expire_reflog_flags flags, int verbose)
{
	struct cmd_reflog_expire_cb cmd = { 0 };
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

	if (!dwim_log(rev, spec - rev, NULL, &ref)) {
		status |= error(_("no reflog for '%s'"), rev);
		goto cleanup;
	}

	recno = strtoul(spec + 2, &ep, 10);
	if (*ep == '}') {
		cmd.recno = -recno;
		for_each_reflog_ent(ref, count_reflog_ent, &cmd);
	} else {
		cmd.expire_total = approxidate(spec + 2);
		for_each_reflog_ent(ref, count_reflog_ent, &cmd);
		cmd.expire_total = 0;
	}

	cb.cmd = cmd;
	status |= reflog_expire(ref, flags,
				reflog_expiry_prepare,
				should_prune_fn,
				reflog_expiry_cleanup,
				&cb);

 cleanup:
	free(ref);
	return status;
}
