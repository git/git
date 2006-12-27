#include "cache.h"
#include "builtin.h"
#include "commit.h"
#include "refs.h"
#include "dir.h"
#include "tree-walk.h"

static unsigned long default_reflog_expire;
static unsigned long default_reflog_expire_unreachable;

struct expire_reflog_cb {
	FILE *newlog;
	const char *ref;
	struct commit *ref_commit;
	unsigned long expire_total;
	unsigned long expire_unreachable;
};

static int tree_is_complete(const unsigned char *sha1)
{
	struct tree_desc desc;
	void *buf;
	char type[20];

	buf = read_sha1_file(sha1, type, &desc.size);
	if (!buf)
		return 0;
	desc.buf = buf;
	while (desc.size) {
		const unsigned char *elem;
		const char *name;
		unsigned mode;

		elem = tree_entry_extract(&desc, &name, &mode);
		if (!has_sha1_file(elem) ||
		    (S_ISDIR(mode) && !tree_is_complete(elem))) {
			free(buf);
			return 0;
		}
		update_tree_entry(&desc);
	}
	free(buf);
	return 1;
}

static int keep_entry(struct commit **it, unsigned char *sha1)
{
	struct commit *commit;

	*it = NULL;
	if (is_null_sha1(sha1))
		return 1;
	commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return 0;

	/* Make sure everything in this commit exists. */
	parse_object(commit->object.sha1);
	if (!tree_is_complete(commit->tree->object.sha1))
		return 0;
	*it = commit;
	return 1;
}

static int expire_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
			     char *data, void *cb_data)
{
	struct expire_reflog_cb *cb = cb_data;
	unsigned long timestamp;
	char *cp, *ep;
	struct commit *old, *new;

	cp = strchr(data, '>');
	if (!cp || *++cp != ' ')
		goto prune;
	timestamp = strtoul(cp, &ep, 10);
	if (*ep != ' ')
		goto prune;
	if (timestamp < cb->expire_total)
		goto prune;

	if (!keep_entry(&old, osha1) || !keep_entry(&new, nsha1))
		goto prune;

	if ((timestamp < cb->expire_unreachable) &&
	    (!cb->ref_commit ||
	     (old && !in_merge_bases(old, cb->ref_commit)) ||
	     (new && !in_merge_bases(new, cb->ref_commit))))
		goto prune;

	if (cb->newlog)
		fprintf(cb->newlog, "%s %s %s",
			sha1_to_hex(osha1), sha1_to_hex(nsha1), data);
	return 0;
 prune:
	if (!cb->newlog)
		fprintf(stderr, "would prune %s", data);
	return 0;
}

struct cmd_reflog_expire_cb {
	int dry_run;
	unsigned long expire_total;
	unsigned long expire_unreachable;
};

static int expire_reflog(const char *ref, const unsigned char *sha1, int unused, void *cb_data)
{
	struct cmd_reflog_expire_cb *cmd = cb_data;
	struct expire_reflog_cb cb;
	struct ref_lock *lock;
	char *newlog_path = NULL;
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
	if (!file_exists(lock->log_file))
		goto finish;
	if (!cmd->dry_run) {
		newlog_path = xstrdup(git_path("logs/%s.lock", ref));
		cb.newlog = fopen(newlog_path, "w");
	}

	cb.ref_commit = lookup_commit_reference_gently(sha1, 1);
	if (!cb.ref_commit)
		fprintf(stderr,
			"warning: ref '%s' does not point at a commit\n", ref);
	cb.ref = ref;
	cb.expire_total = cmd->expire_total;
	cb.expire_unreachable = cmd->expire_unreachable;
	for_each_reflog_ent(ref, expire_reflog_ent, &cb);
 finish:
	if (cb.newlog) {
		if (fclose(cb.newlog))
			status |= error("%s: %s", strerror(errno),
					newlog_path);
		if (rename(newlog_path, lock->log_file)) {
			status |= error("cannot rename %s to %s",
					newlog_path, lock->log_file);
			unlink(newlog_path);
		}
	}
	free(newlog_path);
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

static const char reflog_expire_usage[] =
"git-reflog expire [--dry-run] [--expire=<time>] [--expire-unreachable=<time>] [--all] <refs>...";

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

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--dry-run") || !strcmp(arg, "-n"))
			cb.dry_run = 1;
		else if (!strncmp(arg, "--expire=", 9))
			cb.expire_total = approxidate(arg + 9);
		else if (!strncmp(arg, "--expire-unreachable=", 21))
			cb.expire_unreachable = approxidate(arg + 21);
		else if (!strcmp(arg, "--all"))
			do_all = 1;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (arg[0] == '-')
			usage(reflog_expire_usage);
		else
			break;
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
