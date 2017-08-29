#include "builtin.h"
#include "config.h"
#include "lockfile.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "refs.h"
#include "dir.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "reachable.h"
#include "worktree.h"

/* NEEDSWORK: switch to using parse_options */
static const char reflog_expire_usage[] =
N_("git reflog expire [--expire=<time>] "
   "[--expire-unreachable=<time>] "
   "[--rewrite] [--updateref] [--stale-fix] [--dry-run | -n] "
   "[--verbose] [--all] <refs>...");
static const char reflog_delete_usage[] =
N_("git reflog delete [--rewrite] [--updateref] "
   "[--dry-run | -n] [--verbose] <refs>...");
static const char reflog_exists_usage[] =
N_("git reflog exists <ref>");

static timestamp_t default_reflog_expire;
static timestamp_t default_reflog_expire_unreachable;

struct cmd_reflog_expire_cb {
	struct rev_info revs;
	int stalefix;
	timestamp_t expire_total;
	timestamp_t expire_unreachable;
	int recno;
};

struct expire_reflog_policy_cb {
	enum {
		UE_NORMAL,
		UE_ALWAYS,
		UE_HEAD
	} unreachable_expire_kind;
	struct commit_list *mark_list;
	unsigned long mark_limit;
	struct cmd_reflog_expire_cb cmd;
	struct commit *tip_commit;
	struct commit_list *tips;
};

struct collected_reflog {
	struct object_id oid;
	char reflog[FLEX_ARRAY];
};

struct collect_reflog_cb {
	struct collected_reflog **e;
	int alloc;
	int nr;
	struct worktree *wt;
};

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
		if (parse_commit(commit))
			continue;
		commit->object.flags |= REACHABLE;
		if (commit->date < expire_limit) {
			commit_list_insert(commit, &leftover);
			continue;
		}
		commit->object.flags |= REACHABLE;
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

static int unreachable(struct expire_reflog_policy_cb *cb, struct commit *commit, struct object_id *oid)
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
static int should_expire_reflog_ent(struct object_id *ooid, struct object_id *noid,
				    const char *email, timestamp_t timestamp, int tz,
				    const char *message, void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	struct commit *old_commit, *new_commit;

	if (timestamp < cb->cmd.expire_total)
		return 1;

	old_commit = new_commit = NULL;
	if (cb->cmd.stalefix &&
	    (!keep_entry(&old_commit, ooid) || !keep_entry(&new_commit, noid)))
		return 1;

	if (timestamp < cb->cmd.expire_unreachable) {
		if (cb->unreachable_expire_kind == UE_ALWAYS)
			return 1;
		if (unreachable(cb, old_commit, ooid) || unreachable(cb, new_commit, noid))
			return 1;
	}

	if (cb->cmd.recno && --(cb->cmd.recno) == 0)
		return 1;

	return 0;
}

static int push_tip_to_list(const char *refname, const struct object_id *oid,
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

static void reflog_expiry_prepare(const char *refname,
				  const struct object_id *oid,
				  void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;

	if (!cb->cmd.expire_unreachable || is_head(refname)) {
		cb->tip_commit = NULL;
		cb->unreachable_expire_kind = UE_HEAD;
	} else {
		cb->tip_commit = lookup_commit_reference_gently(the_repository,
								oid, 1);
		if (!cb->tip_commit)
			cb->unreachable_expire_kind = UE_ALWAYS;
		else
			cb->unreachable_expire_kind = UE_NORMAL;
	}

	if (cb->cmd.expire_unreachable <= cb->cmd.expire_total)
		cb->unreachable_expire_kind = UE_ALWAYS;

	cb->mark_list = NULL;
	cb->tips = NULL;
	if (cb->unreachable_expire_kind != UE_ALWAYS) {
		if (cb->unreachable_expire_kind == UE_HEAD) {
			struct commit_list *elem;

			for_each_ref(push_tip_to_list, &cb->tips);
			for (elem = cb->tips; elem; elem = elem->next)
				commit_list_insert(elem->item, &cb->mark_list);
		} else {
			commit_list_insert(cb->tip_commit, &cb->mark_list);
		}
		cb->mark_limit = cb->cmd.expire_total;
		mark_reachable(cb);
	}
}

static void reflog_expiry_cleanup(void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;

	if (cb->unreachable_expire_kind != UE_ALWAYS) {
		if (cb->unreachable_expire_kind == UE_HEAD) {
			struct commit_list *elem;
			for (elem = cb->tips; elem; elem = elem->next)
				clear_commit_marks(elem->item, REACHABLE);
			free_commit_list(cb->tips);
		} else {
			clear_commit_marks(cb->tip_commit, REACHABLE);
		}
	}
}

static int collect_reflog(const char *ref, const struct object_id *oid, int unused, void *cb_data)
{
	struct collected_reflog *e;
	struct collect_reflog_cb *cb = cb_data;
	struct strbuf newref = STRBUF_INIT;

	/*
	 * Avoid collecting the same shared ref multiple times because
	 * they are available via all worktrees.
	 */
	if (!cb->wt->is_current && ref_type(ref) == REF_TYPE_NORMAL)
		return 0;

	strbuf_worktree_ref(cb->wt, &newref, ref);
	FLEX_ALLOC_STR(e, reflog, newref.buf);
	strbuf_release(&newref);

	oidcpy(&e->oid, oid);
	ALLOC_GROW(cb->e, cb->nr + 1, cb->alloc);
	cb->e[cb->nr++] = e;
	return 0;
}

static struct reflog_expire_cfg {
	struct reflog_expire_cfg *next;
	timestamp_t expire_total;
	timestamp_t expire_unreachable;
	char pattern[FLEX_ARRAY];
} *reflog_expire_cfg, **reflog_expire_cfg_tail;

static struct reflog_expire_cfg *find_cfg_ent(const char *pattern, size_t len)
{
	struct reflog_expire_cfg *ent;

	if (!reflog_expire_cfg_tail)
		reflog_expire_cfg_tail = &reflog_expire_cfg;

	for (ent = reflog_expire_cfg; ent; ent = ent->next)
		if (!strncmp(ent->pattern, pattern, len) &&
		    ent->pattern[len] == '\0')
			return ent;

	FLEX_ALLOC_MEM(ent, pattern, pattern, len);
	*reflog_expire_cfg_tail = ent;
	reflog_expire_cfg_tail = &(ent->next);
	return ent;
}

/* expiry timer slot */
#define EXPIRE_TOTAL   01
#define EXPIRE_UNREACH 02

static int reflog_expire_config(const char *var, const char *value, void *cb)
{
	const char *pattern, *key;
	size_t pattern_len;
	timestamp_t expire;
	int slot;
	struct reflog_expire_cfg *ent;

	if (parse_config_key(var, "gc", &pattern, &pattern_len, &key) < 0)
		return git_default_config(var, value, cb);

	if (!strcmp(key, "reflogexpire")) {
		slot = EXPIRE_TOTAL;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else if (!strcmp(key, "reflogexpireunreachable")) {
		slot = EXPIRE_UNREACH;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else
		return git_default_config(var, value, cb);

	if (!pattern) {
		switch (slot) {
		case EXPIRE_TOTAL:
			default_reflog_expire = expire;
			break;
		case EXPIRE_UNREACH:
			default_reflog_expire_unreachable = expire;
			break;
		}
		return 0;
	}

	ent = find_cfg_ent(pattern, pattern_len);
	if (!ent)
		return -1;
	switch (slot) {
	case EXPIRE_TOTAL:
		ent->expire_total = expire;
		break;
	case EXPIRE_UNREACH:
		ent->expire_unreachable = expire;
		break;
	}
	return 0;
}

static void set_reflog_expiry_param(struct cmd_reflog_expire_cb *cb, int slot, const char *ref)
{
	struct reflog_expire_cfg *ent;

	if (slot == (EXPIRE_TOTAL|EXPIRE_UNREACH))
		return; /* both given explicitly -- nothing to tweak */

	for (ent = reflog_expire_cfg; ent; ent = ent->next) {
		if (!wildmatch(ent->pattern, ref, 0)) {
			if (!(slot & EXPIRE_TOTAL))
				cb->expire_total = ent->expire_total;
			if (!(slot & EXPIRE_UNREACH))
				cb->expire_unreachable = ent->expire_unreachable;
			return;
		}
	}

	/*
	 * If unconfigured, make stash never expire
	 */
	if (!strcmp(ref, "refs/stash")) {
		if (!(slot & EXPIRE_TOTAL))
			cb->expire_total = 0;
		if (!(slot & EXPIRE_UNREACH))
			cb->expire_unreachable = 0;
		return;
	}

	/* Nothing matched -- use the default value */
	if (!(slot & EXPIRE_TOTAL))
		cb->expire_total = default_reflog_expire;
	if (!(slot & EXPIRE_UNREACH))
		cb->expire_unreachable = default_reflog_expire_unreachable;
}

static int cmd_reflog_expire(int argc, const char **argv, const char *prefix)
{
	struct expire_reflog_policy_cb cb;
	timestamp_t now = time(NULL);
	int i, status, do_all, all_worktrees = 1;
	int explicit_expiry = 0;
	unsigned int flags = 0;

	default_reflog_expire_unreachable = now - 30 * 24 * 3600;
	default_reflog_expire = now - 90 * 24 * 3600;
	git_config(reflog_expire_config, NULL);

	save_commit_buffer = 0;
	do_all = status = 0;
	memset(&cb, 0, sizeof(cb));

	cb.cmd.expire_total = default_reflog_expire;
	cb.cmd.expire_unreachable = default_reflog_expire_unreachable;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			flags |= EXPIRE_REFLOGS_DRY_RUN;
		else if (skip_prefix(arg, "--expire=", &arg)) {
			if (parse_expiry_date(arg, &cb.cmd.expire_total))
				die(_("'%s' is not a valid timestamp"), arg);
			explicit_expiry |= EXPIRE_TOTAL;
		}
		else if (skip_prefix(arg, "--expire-unreachable=", &arg)) {
			if (parse_expiry_date(arg, &cb.cmd.expire_unreachable))
				die(_("'%s' is not a valid timestamp"), arg);
			explicit_expiry |= EXPIRE_UNREACH;
		}
		else if (!strcmp(arg, "--stale-fix"))
			cb.cmd.stalefix = 1;
		else if (!strcmp(arg, "--rewrite"))
			flags |= EXPIRE_REFLOGS_REWRITE;
		else if (!strcmp(arg, "--updateref"))
			flags |= EXPIRE_REFLOGS_UPDATE_REF;
		else if (!strcmp(arg, "--all"))
			do_all = 1;
		else if (!strcmp(arg, "--single-worktree"))
			all_worktrees = 0;
		else if (!strcmp(arg, "--verbose"))
			flags |= EXPIRE_REFLOGS_VERBOSE;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(_(reflog_expire_usage));
		else
			break;
	}

	/*
	 * We can trust the commits and objects reachable from refs
	 * even in older repository.  We cannot trust what's reachable
	 * from reflog if the repository was pruned with older git.
	 */
	if (cb.cmd.stalefix) {
		repo_init_revisions(the_repository, &cb.cmd.revs, prefix);
		if (flags & EXPIRE_REFLOGS_VERBOSE)
			printf(_("Marking reachable objects..."));
		mark_reachable_objects(&cb.cmd.revs, 0, 0, NULL);
		if (flags & EXPIRE_REFLOGS_VERBOSE)
			putchar('\n');
	}

	if (do_all) {
		struct collect_reflog_cb collected;
		struct worktree **worktrees, **p;
		int i;

		memset(&collected, 0, sizeof(collected));
		worktrees = get_worktrees();
		for (p = worktrees; *p; p++) {
			if (!all_worktrees && !(*p)->is_current)
				continue;
			collected.wt = *p;
			refs_for_each_reflog(get_worktree_ref_store(*p),
					     collect_reflog, &collected);
		}
		free_worktrees(worktrees);
		for (i = 0; i < collected.nr; i++) {
			struct collected_reflog *e = collected.e[i];
			set_reflog_expiry_param(&cb.cmd, explicit_expiry, e->reflog);
			status |= reflog_expire(e->reflog, &e->oid, flags,
						reflog_expiry_prepare,
						should_expire_reflog_ent,
						reflog_expiry_cleanup,
						&cb);
			free(e);
		}
		free(collected.e);
	}

	for (; i < argc; i++) {
		char *ref;
		struct object_id oid;
		if (!dwim_log(argv[i], strlen(argv[i]), &oid, &ref)) {
			status |= error(_("%s points nowhere!"), argv[i]);
			continue;
		}
		set_reflog_expiry_param(&cb.cmd, explicit_expiry, ref);
		status |= reflog_expire(ref, &oid, flags,
					reflog_expiry_prepare,
					should_expire_reflog_ent,
					reflog_expiry_cleanup,
					&cb);
	}
	return status;
}

static int count_reflog_ent(struct object_id *ooid, struct object_id *noid,
		const char *email, timestamp_t timestamp, int tz,
		const char *message, void *cb_data)
{
	struct expire_reflog_policy_cb *cb = cb_data;
	if (!cb->cmd.expire_total || timestamp < cb->cmd.expire_total)
		cb->cmd.recno++;
	return 0;
}

static int cmd_reflog_delete(int argc, const char **argv, const char *prefix)
{
	struct expire_reflog_policy_cb cb;
	int i, status = 0;
	unsigned int flags = 0;

	memset(&cb, 0, sizeof(cb));

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			flags |= EXPIRE_REFLOGS_DRY_RUN;
		else if (!strcmp(arg, "--rewrite"))
			flags |= EXPIRE_REFLOGS_REWRITE;
		else if (!strcmp(arg, "--updateref"))
			flags |= EXPIRE_REFLOGS_UPDATE_REF;
		else if (!strcmp(arg, "--verbose"))
			flags |= EXPIRE_REFLOGS_VERBOSE;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(_(reflog_delete_usage));
		else
			break;
	}

	if (argc - i < 1)
		return error(_("no reflog specified to delete"));

	for ( ; i < argc; i++) {
		const char *spec = strstr(argv[i], "@{");
		struct object_id oid;
		char *ep, *ref;
		int recno;

		if (!spec) {
			status |= error(_("not a reflog: %s"), argv[i]);
			continue;
		}

		if (!dwim_log(argv[i], spec - argv[i], &oid, &ref)) {
			status |= error(_("no reflog for '%s'"), argv[i]);
			continue;
		}

		recno = strtoul(spec + 2, &ep, 10);
		if (*ep == '}') {
			cb.cmd.recno = -recno;
			for_each_reflog_ent(ref, count_reflog_ent, &cb);
		} else {
			cb.cmd.expire_total = approxidate(spec + 2);
			for_each_reflog_ent(ref, count_reflog_ent, &cb);
			cb.cmd.expire_total = 0;
		}

		status |= reflog_expire(ref, &oid, flags,
					reflog_expiry_prepare,
					should_expire_reflog_ent,
					reflog_expiry_cleanup,
					&cb);
		free(ref);
	}
	return status;
}

static int cmd_reflog_exists(int argc, const char **argv, const char *prefix)
{
	int i, start = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(_(reflog_exists_usage));
		else
			break;
	}

	start = i;

	if (argc - start != 1)
		usage(_(reflog_exists_usage));

	if (check_refname_format(argv[start], REFNAME_ALLOW_ONELEVEL))
		die(_("invalid ref format: %s"), argv[start]);
	return !reflog_exists(argv[start]);
}

/*
 * main "reflog"
 */

static const char reflog_usage[] =
N_("git reflog [ show | expire | delete | exists ]");

int cmd_reflog(int argc, const char **argv, const char *prefix)
{
	git_config(git_default_config, NULL);
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage(_(reflog_usage));

	/* With no command, we default to showing it. */
	if (argc < 2 || *argv[1] == '-')
		return cmd_log_reflog(argc, argv, prefix);

	if (!strcmp(argv[1], "show"))
		return cmd_log_reflog(argc - 1, argv + 1, prefix);

	if (!strcmp(argv[1], "expire"))
		return cmd_reflog_expire(argc - 1, argv + 1, prefix);

	if (!strcmp(argv[1], "delete"))
		return cmd_reflog_delete(argc - 1, argv + 1, prefix);

	if (!strcmp(argv[1], "exists"))
		return cmd_reflog_exists(argc - 1, argv + 1, prefix);

	return cmd_log_reflog(argc, argv, prefix);
}
