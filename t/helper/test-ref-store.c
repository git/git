#include "test-tool.h"
#include "cache.h"
#include "refs.h"
#include "worktree.h"
#include "object-store.h"
#include "repository.h"

static const char *notnull(const char *arg, const char *name)
{
	if (!arg)
		die("%s required", name);
	return arg;
}

static unsigned int arg_flags(const char *arg, const char *name)
{
	return atoi(notnull(arg, name));
}

static const char **get_store(const char **argv, struct ref_store **refs)
{
	const char *gitdir;

	if (!argv[0]) {
		die("ref store required");
	} else if (!strcmp(argv[0], "main")) {
		*refs = get_main_ref_store(the_repository);
	} else if (skip_prefix(argv[0], "submodule:", &gitdir)) {
		struct strbuf sb = STRBUF_INIT;
		int ret;

		ret = strbuf_git_path_submodule(&sb, gitdir, "objects/");
		if (ret)
			die("strbuf_git_path_submodule failed: %d", ret);
		add_to_alternates_memory(sb.buf);
		strbuf_release(&sb);

		*refs = get_submodule_ref_store(gitdir);
	} else if (skip_prefix(argv[0], "worktree:", &gitdir)) {
		struct worktree **p, **worktrees = get_worktrees();

		for (p = worktrees; *p; p++) {
			struct worktree *wt = *p;

			if (!wt->id) {
				/* special case for main worktree */
				if (!strcmp(gitdir, "main"))
					break;
			} else if (!strcmp(gitdir, wt->id))
				break;
		}
		if (!*p)
			die("no such worktree: %s", gitdir);

		*refs = get_worktree_ref_store(*p);
	} else
		die("unknown backend %s", argv[0]);

	if (!*refs)
		die("no ref store");

	/* consume store-specific optional arguments if needed */

	return argv + 1;
}


static int cmd_pack_refs(struct ref_store *refs, const char **argv)
{
	unsigned int flags = arg_flags(*argv++, "flags");

	return refs_pack_refs(refs, flags);
}

static int cmd_create_symref(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");
	const char *target = notnull(*argv++, "target");
	const char *logmsg = *argv++;

	return refs_create_symref(refs, refname, target, logmsg);
}

static int cmd_delete_refs(struct ref_store *refs, const char **argv)
{
	unsigned int flags = arg_flags(*argv++, "flags");
	const char *msg = *argv++;
	struct string_list refnames = STRING_LIST_INIT_NODUP;

	while (*argv)
		string_list_append(&refnames, *argv++);

	return refs_delete_refs(refs, msg, &refnames, flags);
}

static int cmd_rename_ref(struct ref_store *refs, const char **argv)
{
	const char *oldref = notnull(*argv++, "oldref");
	const char *newref = notnull(*argv++, "newref");
	const char *logmsg = *argv++;

	return refs_rename_ref(refs, oldref, newref, logmsg);
}

static int each_ref(const char *refname, const struct object_id *oid,
		    int flags, void *cb_data)
{
	printf("%s %s 0x%x\n", oid_to_hex(oid), refname, flags);
	return 0;
}

static int cmd_for_each_ref(struct ref_store *refs, const char **argv)
{
	const char *prefix = notnull(*argv++, "prefix");

	return refs_for_each_ref_in(refs, prefix, each_ref, NULL);
}

static int cmd_resolve_ref(struct ref_store *refs, const char **argv)
{
	struct object_id oid = *null_oid();
	const char *refname = notnull(*argv++, "refname");
	int resolve_flags = arg_flags(*argv++, "resolve-flags");
	int flags;
	const char *ref;

	ref = refs_resolve_ref_unsafe(refs, refname, resolve_flags,
				      &oid, &flags);
	printf("%s %s 0x%x\n", oid_to_hex(&oid), ref ? ref : "(null)", flags);
	return ref ? 0 : 1;
}

static int cmd_verify_ref(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");
	struct strbuf err = STRBUF_INIT;
	int ret;

	ret = refs_verify_refname_available(refs, refname, NULL, NULL, &err);
	if (err.len)
		puts(err.buf);
	return ret;
}

static int cmd_for_each_reflog(struct ref_store *refs, const char **argv)
{
	return refs_for_each_reflog(refs, each_ref, NULL);
}

static int each_reflog(struct object_id *old_oid, struct object_id *new_oid,
		       const char *committer, timestamp_t timestamp,
		       int tz, const char *msg, void *cb_data)
{
	printf("%s %s %s %"PRItime" %d %s\n",
	       oid_to_hex(old_oid), oid_to_hex(new_oid),
	       committer, timestamp, tz, msg);
	return 0;
}

static int cmd_for_each_reflog_ent(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");

	return refs_for_each_reflog_ent(refs, refname, each_reflog, refs);
}

static int cmd_for_each_reflog_ent_reverse(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");

	return refs_for_each_reflog_ent_reverse(refs, refname, each_reflog, refs);
}

static int cmd_reflog_exists(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");

	return !refs_reflog_exists(refs, refname);
}

static int cmd_create_reflog(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");
	int force_create = arg_flags(*argv++, "force-create");
	struct strbuf err = STRBUF_INIT;
	int ret;

	ret = refs_create_reflog(refs, refname, force_create, &err);
	if (err.len)
		puts(err.buf);
	return ret;
}

static int cmd_delete_reflog(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");

	return refs_delete_reflog(refs, refname);
}

static int cmd_reflog_expire(struct ref_store *refs, const char **argv)
{
	die("not supported yet");
}

static int cmd_delete_ref(struct ref_store *refs, const char **argv)
{
	const char *msg = notnull(*argv++, "msg");
	const char *refname = notnull(*argv++, "refname");
	const char *sha1_buf = notnull(*argv++, "old-sha1");
	unsigned int flags = arg_flags(*argv++, "flags");
	struct object_id old_oid;

	if (get_oid_hex(sha1_buf, &old_oid))
		die("not sha-1");

	return refs_delete_ref(refs, msg, refname, &old_oid, flags);
}

static int cmd_update_ref(struct ref_store *refs, const char **argv)
{
	const char *msg = notnull(*argv++, "msg");
	const char *refname = notnull(*argv++, "refname");
	const char *new_sha1_buf = notnull(*argv++, "new-sha1");
	const char *old_sha1_buf = notnull(*argv++, "old-sha1");
	unsigned int flags = arg_flags(*argv++, "flags");
	struct object_id old_oid;
	struct object_id new_oid;

	if (get_oid_hex(old_sha1_buf, &old_oid) ||
	    get_oid_hex(new_sha1_buf, &new_oid))
		die("not sha-1");

	return refs_update_ref(refs, msg, refname,
			       &new_oid, &old_oid,
			       flags, UPDATE_REFS_DIE_ON_ERR);
}

struct command {
	const char *name;
	int (*func)(struct ref_store *refs, const char **argv);
};

static struct command commands[] = {
	{ "pack-refs", cmd_pack_refs },
	{ "create-symref", cmd_create_symref },
	{ "delete-refs", cmd_delete_refs },
	{ "rename-ref", cmd_rename_ref },
	{ "for-each-ref", cmd_for_each_ref },
	{ "resolve-ref", cmd_resolve_ref },
	{ "verify-ref", cmd_verify_ref },
	{ "for-each-reflog", cmd_for_each_reflog },
	{ "for-each-reflog-ent", cmd_for_each_reflog_ent },
	{ "for-each-reflog-ent-reverse", cmd_for_each_reflog_ent_reverse },
	{ "reflog-exists", cmd_reflog_exists },
	{ "create-reflog", cmd_create_reflog },
	{ "delete-reflog", cmd_delete_reflog },
	{ "reflog-expire", cmd_reflog_expire },
	/*
	 * backend transaction functions can't be tested separately
	 */
	{ "delete-ref", cmd_delete_ref },
	{ "update-ref", cmd_update_ref },
	{ NULL, NULL }
};

int cmd__ref_store(int argc, const char **argv)
{
	struct ref_store *refs;
	const char *func;
	struct command *cmd;

	setup_git_directory();

	argv = get_store(argv + 1, &refs);

	func = *argv++;
	if (!func)
		die("ref function required");
	for (cmd = commands; cmd->name; cmd++) {
		if (!strcmp(func, cmd->name))
			return cmd->func(refs, argv);
	}
	die("unknown function %s", func);
	return 0;
}
