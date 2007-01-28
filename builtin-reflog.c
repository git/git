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
"git-reflog expire [--verbose] [--dry-run] [--stale-fix] [--expire=<time>] [--expire-unreachable=<time>] [--all] <refs>...";

static unsigned long default_reflog_expire;
static unsigned long default_reflog_expire_unreachable;

struct cmd_reflog_expire_cb {
	struct rev_info revs;
	int dry_run;
	int stalefix;
	int verbose;
	unsigned long expire_total;
	unsigned long expire_unreachable;
};

struct expire_reflog_cb {
	FILE *newlog;
	const char *ref;
	struct commit *ref_commit;
	struct cmd_reflog_expire_cb *cmd;
};

#define INCOMPLETE	(1u<<10)
#define STUDYING	(1u<<11)

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

	desc.buf = tree->buffer;
	desc.size = tree->size;
	if (!desc.buf) {
		char type[20];
		void *data = read_sha1_file(sha1, type, &desc.size);
		if (!data) {
			tree->object.flags |= INCOMPLETE;
			return 0;
		}
		desc.buf = data;
		tree->buffer = data;
	}
	complete = 1;
	while (tree_entry(&desc, &entry)) {
		if (!has_sha1_file(entry.sha1) ||
		    (S_ISDIR(entry.mode) && !tree_is_complete(entry.sha1))) {
			tree->object.flags |= INCOMPLETE;
			complete = 0;
		}
	}
	free(tree->buffer);
	tree->buffer = NULL;

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

static int expire_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct expire_reflog_cb *cb = cb_data;
	struct commit *old, *new;

	if (timestamp < cb->cmd->expire_total)
		goto prune;

	old = new = NULL;
	if (cb->cmd->stalefix &&
	    (!keep_entry(&old, osha1) || !keep_entry(&new, nsha1)))
		goto prune;

	if (timestamp < cb->cmd->expire_unreachable) {
		if (!cb->ref_commit)
			goto prune;
		if (!old && !is_null_sha1(osha1))
			old = lookup_commit_reference_gently(osha1, 1);
		if (!new && !is_null_sha1(nsha1))
			new = lookup_commit_reference_gently(nsha1, 1);
		if ((old && !in_merge_bases(old, &cb->ref_commit, 1)) ||
		    (new && !in_merge_bases(new, &cb->ref_commit, 1)))
			goto prune;
	}

	if (cb->newlog) {
		char sign = (tz < 0) ? '-' : '+';
		int zone = (tz < 0) ? (-tz) : tz;
		fprintf(cb->newlog, "%s %s %s %lu %c%04d\t%s",
			sha1_to_hex(osha1), sha1_to_hex(nsha1),
			email, timestamp, sign, zone,
			message);
	}
	if (cb->cmd->verbose)
		printf("keep %s", message);
	return 0;
 prune:
	if (!cb->newlog || cb->cmd->verbose)
		printf("%sprune %s", cb->newlog ? "" : "would ", message);
	return 0;
}

static int expire_reflog(const char *ref, const unsigned char *sha1, int unused, void *cb_data)
{
	struct cmd_reflog_expire_cb *cmd = cb_data;
	struct expire_reflog_cb cb;
	struct ref_lock *lock;
	char *log_file, *newlog_path = NULL;
	int status = 0;

	if (strncmp(ref, "refs/", 5))
		return error("not a ref '%s'", ref);

	memset(&cb, 0, sizeof(cb));
	/* we take the lock for the ref itself to prevent it from
	 * getting updated.
	 */
	lock = lock_ref_sha1(ref + 5, sha1);
	if (!lock)
		return error("cannot lock ref '%s'", ref);
	log_file = xstrdup(git_path("logs/%s", ref));
	if (!file_exists(log_file))
		goto finish;
	if (!cmd->dry_run) {
		newlog_path = xstrdup(git_path("logs/%s.lock", ref));
		cb.newlog = fopen(newlog_path, "w");
	}

	cb.ref_commit = lookup_commit_reference_gently(sha1, 1);
	cb.ref = ref;
	cb.cmd = cmd;
	for_each_reflog_ent(ref, expire_reflog_ent, &cb);
 finish:
	if (cb.newlog) {
		if (fclose(cb.newlog))
			status |= error("%s: %s", strerror(errno),
					newlog_path);
		if (rename(newlog_path, log_file)) {
			status |= error("cannot rename %s to %s",
					newlog_path, log_file);
			unlink(newlog_path);
		}
	}
	free(newlog_path);
	free(log_file);
	unlock_ref(lock);
	return status;
}

static int reflog_expire_config(const char *var, const char *value)
{
	if (!strcmp(var, "gc.reflogexpire"))
		default_reflog_expire = approxidate(value);
	else if (!strcmp(var, "gc.reflogexpireunreachable"))
		default_reflog_expire_unreachable = approxidate(value);
	else
		return git_default_config(var, value);
	return 0;
}

static int cmd_reflog_expire(int argc, const char **argv, const char *prefix)
{
	struct cmd_reflog_expire_cb cb;
	unsigned long now = time(NULL);
	int i, status, do_all;

	git_config(reflog_expire_config);

	save_commit_buffer = 0;
	do_all = status = 0;
	memset(&cb, 0, sizeof(cb));

	if (!default_reflog_expire_unreachable)
		default_reflog_expire_unreachable = now - 30 * 24 * 3600;
	if (!default_reflog_expire)
		default_reflog_expire = now - 90 * 24 * 3600;
	cb.expire_total = default_reflog_expire;
	cb.expire_unreachable = default_reflog_expire_unreachable;

	/*
	 * We can trust the commits and objects reachable from refs
	 * even in older repository.  We cannot trust what's reachable
	 * from reflog if the repository was pruned with older git.
	 */

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			cb.dry_run = 1;
		else if (!strncmp(arg, "--expire=", 9))
			cb.expire_total = approxidate(arg + 9);
		else if (!strncmp(arg, "--expire-unreachable=", 21))
			cb.expire_unreachable = approxidate(arg + 21);
		else if (!strcmp(arg, "--stale-fix"))
			cb.stalefix = 1;
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
	if (cb.stalefix) {
		init_revisions(&cb.revs, prefix);
		if (cb.verbose)
			printf("Marking reachable objects...");
		mark_reachable_objects(&cb.revs, 0);
		if (cb.verbose)
			putchar('\n');
	}

	if (do_all)
		status |= for_each_ref(expire_reflog, &cb);
	while (i < argc) {
		const char *ref = argv[i++];
		unsigned char sha1[20];
		if (!resolve_ref(ref, sha1, 1, NULL)) {
			status |= error("%s points nowhere!", ref);
			continue;
		}
		status |= expire_reflog(ref, sha1, 0, &cb);
	}
	return status;
}

/*
 * main "reflog"
 */

static const char reflog_usage[] =
"git-reflog (expire | ...)";

int cmd_reflog(int argc, const char **argv, const char *prefix)
{
	if (argc < 2)
		usage(reflog_usage);
	else if (!strcmp(argv[1], "expire"))
		return cmd_reflog_expire(argc - 1, argv + 1, prefix);
	else
		usage(reflog_usage);
}
