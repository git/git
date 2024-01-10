#include "test-tool.h"
#include "hex.h"
#include "refs.h"
#include "setup.h"
#include "worktree.h"
#include "object-store-ll.h"
#include "path.h"
#include "repository.h"
#include "strbuf.h"
#include "revision.h"

struct flag_definition {
	const char *name;
	uint64_t mask;
};

#define FLAG_DEF(x)     \
	{               \
#x, (x) \
	}

static unsigned int parse_flags(const char *str, struct flag_definition *defs)
{
	struct string_list masks = STRING_LIST_INIT_DUP;
	int i = 0;
	unsigned int result = 0;

	if (!strcmp(str, "0"))
		return 0;

	string_list_split(&masks, str, ',', 64);
	for (; i < masks.nr; i++) {
		const char *name = masks.items[i].string;
		struct flag_definition *def = defs;
		int found = 0;
		while (def->name) {
			if (!strcmp(def->name, name)) {
				result |= def->mask;
				found = 1;
				break;
			}
			def++;
		}
		if (!found)
			die("unknown flag \"%s\"", name);
	}

	string_list_clear(&masks, 0);
	return result;
}

static struct flag_definition empty_flags[] = { { NULL, 0 } };

static const char *notnull(const char *arg, const char *name)
{
	if (!arg)
		die("%s required", name);
	return arg;
}

static unsigned int arg_flags(const char *arg, const char *name,
			      struct flag_definition *defs)
{
	return parse_flags(notnull(arg, name), defs);
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
		free_worktrees(worktrees);
	} else
		die("unknown backend %s", argv[0]);

	if (!*refs)
		die("no ref store");

	/* consume store-specific optional arguments if needed */

	return argv + 1;
}

static struct flag_definition pack_flags[] = { FLAG_DEF(PACK_REFS_PRUNE),
					       FLAG_DEF(PACK_REFS_ALL),
					       { NULL, 0 } };

static int cmd_pack_refs(struct ref_store *refs, const char **argv)
{
	unsigned int flags = arg_flags(*argv++, "flags", pack_flags);
	static struct ref_exclusions exclusions = REF_EXCLUSIONS_INIT;
	static struct string_list included_refs = STRING_LIST_INIT_NODUP;
	struct pack_refs_opts pack_opts = { .flags = flags,
					    .exclusions = &exclusions,
					    .includes = &included_refs };

	if (pack_opts.flags & PACK_REFS_ALL)
		string_list_append(pack_opts.includes, "*");

	return refs_pack_refs(refs, &pack_opts);
}

static int cmd_create_symref(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");
	const char *target = notnull(*argv++, "target");
	const char *logmsg = *argv++;

	return refs_create_symref(refs, refname, target, logmsg);
}

static struct flag_definition transaction_flags[] = {
	FLAG_DEF(REF_NO_DEREF),
	FLAG_DEF(REF_FORCE_CREATE_REFLOG),
	FLAG_DEF(REF_SKIP_OID_VERIFICATION),
	FLAG_DEF(REF_SKIP_REFNAME_VERIFICATION),
	{ NULL, 0 }
};

static int cmd_delete_refs(struct ref_store *refs, const char **argv)
{
	unsigned int flags = arg_flags(*argv++, "flags", transaction_flags);
	const char *msg = *argv++;
	struct string_list refnames = STRING_LIST_INIT_NODUP;
	int result;

	while (*argv)
		string_list_append(&refnames, *argv++);

	result = refs_delete_refs(refs, msg, &refnames, flags);
	string_list_clear(&refnames, 0);
	return result;
}

static int cmd_rename_ref(struct ref_store *refs, const char **argv)
{
	const char *oldref = notnull(*argv++, "oldref");
	const char *newref = notnull(*argv++, "newref");
	const char *logmsg = *argv++;

	return refs_rename_ref(refs, oldref, newref, logmsg);
}

static int each_ref(const char *refname, const struct object_id *oid,
		    int flags, void *cb_data UNUSED)
{
	printf("%s %s 0x%x\n", oid_to_hex(oid), refname, flags);
	return 0;
}

static int cmd_for_each_ref(struct ref_store *refs, const char **argv)
{
	const char *prefix = notnull(*argv++, "prefix");

	return refs_for_each_ref_in(refs, prefix, each_ref, NULL);
}

static int cmd_for_each_ref__exclude(struct ref_store *refs, const char **argv)
{
	const char *prefix = notnull(*argv++, "prefix");
	const char **exclude_patterns = argv;

	return refs_for_each_fullref_in(refs, prefix, exclude_patterns, each_ref,
					NULL);
}

static int cmd_resolve_ref(struct ref_store *refs, const char **argv)
{
	struct object_id oid = *null_oid();
	const char *refname = notnull(*argv++, "refname");
	int resolve_flags = arg_flags(*argv++, "resolve-flags", empty_flags);
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

static int cmd_for_each_reflog(struct ref_store *refs,
			       const char **argv UNUSED)
{
	return refs_for_each_reflog(refs, each_ref, NULL);
}

static int each_reflog(struct object_id *old_oid, struct object_id *new_oid,
		       const char *committer, timestamp_t timestamp,
		       int tz, const char *msg, void *cb_data UNUSED)
{
	printf("%s %s %s %" PRItime " %+05d%s%s", oid_to_hex(old_oid),
	       oid_to_hex(new_oid), committer, timestamp, tz,
	       *msg == '\n' ? "" : "\t", msg);
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
	struct strbuf err = STRBUF_INIT;
	int ret;

	ret = refs_create_reflog(refs, refname, &err);
	if (err.len)
		puts(err.buf);
	return ret;
}

static int cmd_delete_reflog(struct ref_store *refs, const char **argv)
{
	const char *refname = notnull(*argv++, "refname");

	return refs_delete_reflog(refs, refname);
}

static int cmd_delete_ref(struct ref_store *refs, const char **argv)
{
	const char *msg = notnull(*argv++, "msg");
	const char *refname = notnull(*argv++, "refname");
	const char *sha1_buf = notnull(*argv++, "old-sha1");
	unsigned int flags = arg_flags(*argv++, "flags", transaction_flags);
	struct object_id old_oid;

	if (get_oid_hex(sha1_buf, &old_oid))
		die("cannot parse %s as %s", sha1_buf, the_hash_algo->name);

	return refs_delete_ref(refs, msg, refname, &old_oid, flags);
}

static int cmd_update_ref(struct ref_store *refs, const char **argv)
{
	const char *msg = notnull(*argv++, "msg");
	const char *refname = notnull(*argv++, "refname");
	const char *new_sha1_buf = notnull(*argv++, "new-sha1");
	const char *old_sha1_buf = notnull(*argv++, "old-sha1");
	unsigned int flags = arg_flags(*argv++, "flags", transaction_flags);
	struct object_id old_oid, *old_oid_ptr = NULL;
	struct object_id new_oid;

	if (*old_sha1_buf) {
		if (get_oid_hex(old_sha1_buf, &old_oid))
			die("cannot parse %s as %s", old_sha1_buf, the_hash_algo->name);
		old_oid_ptr = &old_oid;
	}
	if (get_oid_hex(new_sha1_buf, &new_oid))
		die("cannot parse %s as %s", new_sha1_buf, the_hash_algo->name);

	return refs_update_ref(refs, msg, refname,
			       &new_oid, old_oid_ptr,
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
	{ "for-each-ref--exclude", cmd_for_each_ref__exclude },
	{ "resolve-ref", cmd_resolve_ref },
	{ "verify-ref", cmd_verify_ref },
	{ "for-each-reflog", cmd_for_each_reflog },
	{ "for-each-reflog-ent", cmd_for_each_reflog_ent },
	{ "for-each-reflog-ent-reverse", cmd_for_each_reflog_ent_reverse },
	{ "reflog-exists", cmd_reflog_exists },
	{ "create-reflog", cmd_create_reflog },
	{ "delete-reflog", cmd_delete_reflog },
	/*
	 * backend transaction functions can't be tested separately
	 */
	{ "delete-ref", cmd_delete_ref },
	{ "update-ref", cmd_update_ref },
	{ NULL, NULL }
};

int cmd__ref_store(int argc UNUSED, const char **argv)
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
