#include "cache.h"
#include "builtin.h"
#include "commit.h"
#include "refs.h"
#include "dir.h"
#include <time.h>

struct expire_reflog_cb {
	FILE *newlog;
	const char *ref;
	struct commit *ref_commit;
	unsigned long expire_total;
	unsigned long expire_unreachable;
};

static int keep_entry(struct commit **it, unsigned char *sha1)
{
	*it = NULL;
	if (is_null_sha1(sha1))
		return 1;
	*it = lookup_commit_reference_gently(sha1, 1);
	return (*it != NULL);
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
	    ((old && !in_merge_bases(old, cb->ref_commit)) ||
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
	if (!cb.ref_commit) {
		status = error("ref '%s' does not point at a commit", ref);
		goto finish;
	}
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

static const char reflog_expire_usage[] =
"git-reflog expire [--dry-run] [--expire=<time>] [--expire-unreachable=<time>] [--all] <refs>...";

static int cmd_reflog_expire(int argc, const char **argv, const char *prefix)
{
	struct cmd_reflog_expire_cb cb;
	unsigned long now = time(NULL);
	int i, status, do_all;

	save_commit_buffer = 0;
	do_all = status = 0;
	memset(&cb, 0, sizeof(cb));
	cb.expire_total = now - 90 * 24 * 3600;
	cb.expire_unreachable = now - 30 * 24 * 3600;

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
