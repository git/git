#include "cache.h"
#include "builtin.h"
#include "commit.h"
#include "refs.h"
#include "dir.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "reachable.h"

/*
 * reflog expire
 */

static const char reflog_expire_usage[] =
"git reflog expire [--verbose] [--dry-run] [--stale-fix] [--expire=<time>] [--expire-unreachable=<time>] [--all] <refs>...";
static const char reflog_delete_usage[] =
"git reflog delete [--verbose] [--dry-run] [--rewrite] [--updateref] <refs>...";

static unsigned long default_reflog_expire;
static unsigned long default_reflog_expire_unreachable;

struct cmd_reflog_expire_cb {
	struct rev_info revs;
	int dry_run;
	int stalefix;
	int rewrite;
	int updateref;
	int verbose;
	unsigned long expire_total;
	unsigned long expire_unreachable;
	int recno;
};

struct expire_reflog_cb {
	FILE *newlog;
	enum {
		UE_NORMAL,
		UE_ALWAYS,
		UE_HEAD
	} unreachable_expire_kind;
	struct commit_list *mark_list;
	unsigned long mark_limit;
	struct cmd_reflog_expire_cb *cmd;
	unsigned char last_kept_sha1[20];
};

struct collected_reflog {
	unsigned char sha1[20];
	char reflog[FLEX_ARRAY];
};
struct collect_reflog_cb {
	struct collected_reflog **e;
	int alloc;
	int nr;
};

#define INCOMPLETE	(1u<<10)
#define STUDYING	(1u<<11)
#define REACHABLE	(1u<<12)

static int tree_is_complete(const unsigned char *sha1)
{
	struct tree_desc desc;
	struct name_entry entry;
	int complete;
	struct tree *tree;

	tree = lookup_tree(sha1);
	if (!tree)
		return 0;
	if (tree->object.flags & SEEN)
		return 1;
	if (tree->object.flags & INCOMPLETE)
		return 0;

	if (!tree->buffer) {
		enum object_type type;
		unsigned long size;
		void *data = read_sha1_file(sha1, &type, &size);
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
		if (!has_sha1_file(entry.sha1) ||
		    (S_ISDIR(entry.mode) && !tree_is_complete(entry.sha1))) {
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

		c = (struct commit *)study.objects[--study.nr].item;
		if (!c->object.parsed && !parse_object(c->object.sha1))
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
			if (!tree_is_complete(c->tree->object.sha1)) {
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
	free(study.objects);
	free(found.objects);
	return !is_incomplete;
}

static int keep_entry(struct commit **it, unsigned char *sha1)
{
	struct commit *commit;

	if (is_null_sha1(sha1))
		return 1;
	commit = lookup_commit_reference_gently(sha1, 1);
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
static void mark_reachable(struct expire_reflog_cb *cb)
{
	struct commit *commit;
	struct commit_list *pending;
	unsigned long expire_limit = cb->mark_limit;
	struct commit_list *leftover = NULL;

	for (pending = cb->mark_list; pending; pending = pending->next)
		pending->item->object.flags &= ~REACHABLE;

	pending = cb->mark_list;
	while (pending) {
		struct commit_list *entry = pending;
		struct commit_list *parent;
		pending = entry->next;
		commit = entry->item;
		free(entry);
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

static int unreachable(struct expire_reflog_cb *cb, struct commit *commit, unsigned char *sha1)
{
	/*
	 * We may or may not have the commit yet - if not, look it
	 * up using the supplied sha1.
	 */
	if (!commit) {
		if (is_null_sha1(sha1))
			return 0;

		commit = lookup_commit_reference_gently(sha1, 1);

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

static int expire_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct expire_reflog_cb *cb = cb_data;
	struct commit *old, *new;

	if (timestamp < cb->cmd->expire_total)
		goto prune;

	if (cb->cmd->rewrite)
		osha1 = cb->last_kept_sha1;

	old = new = NULL;
	if (cb->cmd->stalefix &&
	    (!keep_entry(&old, osha1) || !keep_entry(&new, nsha1)))
		goto prune;

	if (timestamp < cb->cmd->expire_unreachable) {
		if (cb->unreachable_expire_kind == UE_ALWAYS)
			goto prune;
		if (unreachable(cb, old, osha1) || unreachable(cb, new, nsha1))
			goto prune;
	}

	if (cb->cmd->recno && --(cb->cmd->recno) == 0)
		goto prune;

	if (cb->newlog) {
		char sign = (tz < 0) ? '-' : '+';
		int zone = (tz < 0) ? (-tz) : tz;
		fprintf(cb->newlog, "%s %s %s %lu %c%04d\t%s",
			sha1_to_hex(osha1), sha1_to_hex(nsha1),
			email, timestamp, sign, zone,
			message);
		hashcpy(cb->last_kept_sha1, nsha1);
	}
	if (cb->cmd->verbose)
		printf("keep %s", message);
	return 0;
 prune:
	if (!cb->newlog)
		printf("would prune %s", message);
	else if (cb->cmd->verbose)
		printf("prune %s", message);
	return 0;
}

static int push_tip_to_list(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct commit_list **list = cb_data;
	struct commit *tip_commit;
	if (flags & REF_ISSYMREF)
		return 0;
	tip_commit = lookup_commit_reference_gently(sha1, 1);
	if (!tip_commit)
		return 0;
	commit_list_insert(tip_commit, list);
	return 0;
}

static int expire_reflog(const char *ref, const unsigned char *sha1, int unused, void *cb_data)
{
	struct cmd_reflog_expire_cb *cmd = cb_data;
	struct expire_reflog_cb cb;
	struct ref_lock *lock;
	char *log_file, *newlog_path = NULL;
	struct commit *tip_commit;
	struct commit_list *tips;
	int status = 0;

	memset(&cb, 0, sizeof(cb));

	/*
	 * we take the lock for the ref itself to prevent it from
	 * getting updated.
	 */
	lock = lock_any_ref_for_update(ref, sha1, 0, NULL);
	if (!lock)
		return error("cannot lock ref '%s'", ref);
	log_file = git_pathdup("logs/%s", ref);
	if (!reflog_exists(ref))
		goto finish;
	if (!cmd->dry_run) {
		newlog_path = git_pathdup("logs/%s.lock", ref);
		cb.newlog = fopen(newlog_path, "w");
	}

	cb.cmd = cmd;

	if (!cmd->expire_unreachable || !strcmp(ref, "HEAD")) {
		tip_commit = NULL;
		cb.unreachable_expire_kind = UE_HEAD;
	} else {
		tip_commit = lookup_commit_reference_gently(sha1, 1);
		if (!tip_commit)
			cb.unreachable_expire_kind = UE_ALWAYS;
		else
			cb.unreachable_expire_kind = UE_NORMAL;
	}

	if (cmd->expire_unreachable <= cmd->expire_total)
		cb.unreachable_expire_kind = UE_ALWAYS;

	cb.mark_list = NULL;
	tips = NULL;
	if (cb.unreachable_expire_kind != UE_ALWAYS) {
		if (cb.unreachable_expire_kind == UE_HEAD) {
			struct commit_list *elem;
			for_each_ref(push_tip_to_list, &tips);
			for (elem = tips; elem; elem = elem->next)
				commit_list_insert(elem->item, &cb.mark_list);
		} else {
			commit_list_insert(tip_commit, &cb.mark_list);
		}
		cb.mark_limit = cmd->expire_total;
		mark_reachable(&cb);
	}

	for_each_reflog_ent(ref, expire_reflog_ent, &cb);

	if (cb.unreachable_expire_kind != UE_ALWAYS) {
		if (cb.unreachable_expire_kind == UE_HEAD) {
			struct commit_list *elem;
			for (elem = tips; elem; elem = elem->next)
				clear_commit_marks(elem->item, REACHABLE);
			free_commit_list(tips);
		} else {
			clear_commit_marks(tip_commit, REACHABLE);
		}
	}
 finish:
	if (cb.newlog) {
		if (fclose(cb.newlog)) {
			status |= error("%s: %s", strerror(errno),
					newlog_path);
			unlink(newlog_path);
		} else if (cmd->updateref &&
			(write_in_full(lock->lock_fd,
				sha1_to_hex(cb.last_kept_sha1), 40) != 40 ||
			 write_str_in_full(lock->lock_fd, "\n") != 1 ||
			 close_ref(lock) < 0)) {
			status |= error("Couldn't write %s",
				lock->lk->filename);
			unlink(newlog_path);
		} else if (rename(newlog_path, log_file)) {
			status |= error("cannot rename %s to %s",
					newlog_path, log_file);
			unlink(newlog_path);
		} else if (cmd->updateref && commit_ref(lock)) {
			status |= error("Couldn't set %s", lock->ref_name);
		} else {
			adjust_shared_perm(log_file);
		}
	}
	free(newlog_path);
	free(log_file);
	unlock_ref(lock);
	return status;
}

static int collect_reflog(const char *ref, const unsigned char *sha1, int unused, void *cb_data)
{
	struct collected_reflog *e;
	struct collect_reflog_cb *cb = cb_data;
	size_t namelen = strlen(ref);

	e = xmalloc(sizeof(*e) + namelen + 1);
	hashcpy(e->sha1, sha1);
	memcpy(e->reflog, ref, namelen + 1);
	ALLOC_GROW(cb->e, cb->nr + 1, cb->alloc);
	cb->e[cb->nr++] = e;
	return 0;
}

static struct reflog_expire_cfg {
	struct reflog_expire_cfg *next;
	unsigned long expire_total;
	unsigned long expire_unreachable;
	size_t len;
	char pattern[FLEX_ARRAY];
} *reflog_expire_cfg, **reflog_expire_cfg_tail;

static struct reflog_expire_cfg *find_cfg_ent(const char *pattern, size_t len)
{
	struct reflog_expire_cfg *ent;

	if (!reflog_expire_cfg_tail)
		reflog_expire_cfg_tail = &reflog_expire_cfg;

	for (ent = reflog_expire_cfg; ent; ent = ent->next)
		if (ent->len == len &&
		    !memcmp(ent->pattern, pattern, len))
			return ent;

	ent = xcalloc(1, (sizeof(*ent) + len));
	memcpy(ent->pattern, pattern, len);
	ent->len = len;
	*reflog_expire_cfg_tail = ent;
	reflog_expire_cfg_tail = &(ent->next);
	return ent;
}

static int parse_expire_cfg_value(const char *var, const char *value, unsigned long *expire)
{
	if (!value)
		return config_error_nonbool(var);
	if (parse_expiry_date(value, expire))
		return error(_("%s' for '%s' is not a valid timestamp"),
			     value, var);
	return 0;
}

/* expiry timer slot */
#define EXPIRE_TOTAL   01
#define EXPIRE_UNREACH 02

static int reflog_expire_config(const char *var, const char *value, void *cb)
{
	const char *pattern, *key;
	int pattern_len;
	unsigned long expire;
	int slot;
	struct reflog_expire_cfg *ent;

	if (parse_config_key(var, "gc", &pattern, &pattern_len, &key) < 0)
		return git_default_config(var, value, cb);

	if (!strcmp(key, "reflogexpire")) {
		slot = EXPIRE_TOTAL;
		if (parse_expire_cfg_value(var, value, &expire))
			return -1;
	} else if (!strcmp(key, "reflogexpireunreachable")) {
		slot = EXPIRE_UNREACH;
		if (parse_expire_cfg_value(var, value, &expire))
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
		if (!wildmatch(ent->pattern, ref, 0, NULL)) {
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
	struct cmd_reflog_expire_cb cb;
	unsigned long now = time(NULL);
	int i, status, do_all;
	int explicit_expiry = 0;

	default_reflog_expire_unreachable = now - 30 * 24 * 3600;
	default_reflog_expire = now - 90 * 24 * 3600;
	git_config(reflog_expire_config, NULL);

	save_commit_buffer = 0;
	do_all = status = 0;
	memset(&cb, 0, sizeof(cb));

	cb.expire_total = default_reflog_expire;
	cb.expire_unreachable = default_reflog_expire_unreachable;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			cb.dry_run = 1;
		else if (starts_with(arg, "--expire=")) {
			if (parse_expiry_date(arg + 9, &cb.expire_total))
				die(_("'%s' is not a valid timestamp"), arg);
			explicit_expiry |= EXPIRE_TOTAL;
		}
		else if (starts_with(arg, "--expire-unreachable=")) {
			if (parse_expiry_date(arg + 21, &cb.expire_unreachable))
				die(_("'%s' is not a valid timestamp"), arg);
			explicit_expiry |= EXPIRE_UNREACH;
		}
		else if (!strcmp(arg, "--stale-fix"))
			cb.stalefix = 1;
		else if (!strcmp(arg, "--rewrite"))
			cb.rewrite = 1;
		else if (!strcmp(arg, "--updateref"))
			cb.updateref = 1;
		else if (!strcmp(arg, "--all"))
			do_all = 1;
		else if (!strcmp(arg, "--verbose"))
			cb.verbose = 1;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(reflog_expire_usage);
		else
			break;
	}

	/*
	 * We can trust the commits and objects reachable from refs
	 * even in older repository.  We cannot trust what's reachable
	 * from reflog if the repository was pruned with older git.
	 */
	if (cb.stalefix) {
		init_revisions(&cb.revs, prefix);
		if (cb.verbose)
			printf("Marking reachable objects...");
		mark_reachable_objects(&cb.revs, 0, NULL);
		if (cb.verbose)
			putchar('\n');
	}

	if (do_all) {
		struct collect_reflog_cb collected;
		int i;

		memset(&collected, 0, sizeof(collected));
		for_each_reflog(collect_reflog, &collected);
		for (i = 0; i < collected.nr; i++) {
			struct collected_reflog *e = collected.e[i];
			set_reflog_expiry_param(&cb, explicit_expiry, e->reflog);
			status |= expire_reflog(e->reflog, e->sha1, 0, &cb);
			free(e);
		}
		free(collected.e);
	}

	for (; i < argc; i++) {
		char *ref;
		unsigned char sha1[20];
		if (!dwim_log(argv[i], strlen(argv[i]), sha1, &ref)) {
			status |= error("%s points nowhere!", argv[i]);
			continue;
		}
		set_reflog_expiry_param(&cb, explicit_expiry, ref);
		status |= expire_reflog(ref, sha1, 0, &cb);
	}
	return status;
}

static int count_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct cmd_reflog_expire_cb *cb = cb_data;
	if (!cb->expire_total || timestamp < cb->expire_total)
		cb->recno++;
	return 0;
}

static int cmd_reflog_delete(int argc, const char **argv, const char *prefix)
{
	struct cmd_reflog_expire_cb cb;
	int i, status = 0;

	memset(&cb, 0, sizeof(cb));

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			cb.dry_run = 1;
		else if (!strcmp(arg, "--rewrite"))
			cb.rewrite = 1;
		else if (!strcmp(arg, "--updateref"))
			cb.updateref = 1;
		else if (!strcmp(arg, "--verbose"))
			cb.verbose = 1;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(reflog_delete_usage);
		else
			break;
	}

	if (argc - i < 1)
		return error("Nothing to delete?");

	for ( ; i < argc; i++) {
		const char *spec = strstr(argv[i], "@{");
		unsigned char sha1[20];
		char *ep, *ref;
		int recno;

		if (!spec) {
			status |= error("Not a reflog: %s", argv[i]);
			continue;
		}

		if (!dwim_log(argv[i], spec - argv[i], sha1, &ref)) {
			status |= error("no reflog for '%s'", argv[i]);
			continue;
		}

		recno = strtoul(spec + 2, &ep, 10);
		if (*ep == '}') {
			cb.recno = -recno;
			for_each_reflog_ent(ref, count_reflog_ent, &cb);
		} else {
			cb.expire_total = approxidate(spec + 2);
			for_each_reflog_ent(ref, count_reflog_ent, &cb);
			cb.expire_total = 0;
		}

		status |= expire_reflog(ref, sha1, 0, &cb);
		free(ref);
	}
	return status;
}

/*
 * main "reflog"
 */

static const char reflog_usage[] =
"git reflog [ show | expire | delete ]";

int cmd_reflog(int argc, const char **argv, const char *prefix)
{
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage(reflog_usage);

	/* With no command, we default to showing it. */
	if (argc < 2 || *argv[1] == '-')
		return cmd_log_reflog(argc, argv, prefix);

	if (!strcmp(argv[1], "show"))
		return cmd_log_reflog(argc - 1, argv + 1, prefix);

	if (!strcmp(argv[1], "expire"))
		return cmd_reflog_expire(argc - 1, argv + 1, prefix);

	if (!strcmp(argv[1], "delete"))
		return cmd_reflog_delete(argc - 1, argv + 1, prefix);

	return cmd_log_reflog(argc, argv, prefix);
}
