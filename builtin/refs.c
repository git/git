#include "builtin.h"
#include "config.h"
#include "fsck.h"
#include "pack-refs.h"
#include "parse-options.h"
#include "refs.h"
#include "strbuf.h"
#include "worktree.h"
#include "for-each-ref.h"
#include "refs/refs-internal.h"

#define REFS_MIGRATE_USAGE \
	N_("git refs migrate --ref-format=<format> [--no-reflog] [--dry-run]")

#define REFS_VERIFY_USAGE \
	N_("git refs verify [--strict] [--verbose]")

#define REFS_EXISTS_USAGE \
	N_("git refs exists <ref>")

#define REFS_OPTIMIZE_USAGE \
	N_("git refs optimize " PACK_REFS_OPTS)

#define REFS_CREATE_USAGE \
	N_("git refs create [--message=<reason>] [--no-deref] [--create-reflog] <ref> <new-value>")

#define REFS_DELETE_USAGE \
	N_("git refs delete [--message=<reason>] [--no-deref] <ref> [<old-value>]")

#define REFS_UPDATE_USAGE \
	N_("git refs update [--message=<reason>] [--no-deref] [--create-reflog] <ref> <new-value> [<old-value>]")

#define REFS_RENAME_USAGE \
	N_("git refs rename [--message=<reason>] <old-ref> <new-ref>")

static int cmd_refs_migrate(int argc, const char **argv, const char *prefix,
			    struct repository *repo)
{
	const char * const migrate_usage[] = {
		REFS_MIGRATE_USAGE,
		NULL,
	};
	const char *format_str = NULL;
	enum ref_storage_format format;
	unsigned int flags = 0;
	struct option options[] = {
		OPT_STRING_F(0, "ref-format", &format_str, N_("format"),
			N_("specify the reference format to convert to"),
			PARSE_OPT_NONEG),
		OPT_BIT(0, "dry-run", &flags,
			N_("perform a non-destructive dry-run"),
			REPO_MIGRATE_REF_STORAGE_FORMAT_DRYRUN),
		OPT_BIT(0, "no-reflog", &flags,
			N_("drop reflogs entirely during the migration"),
			REPO_MIGRATE_REF_STORAGE_FORMAT_SKIP_REFLOG),
		OPT_END(),
	};
	struct strbuf errbuf = STRBUF_INIT;
	int err;

	argc = parse_options(argc, argv, prefix, options, migrate_usage, 0);
	if (argc)
		usage(_("too many arguments"));
	if (!format_str)
		usage(_("missing --ref-format=<format>"));

	format = ref_storage_format_by_name(format_str);
	if (format == REF_STORAGE_FORMAT_UNKNOWN) {
		err = error(_("unknown ref storage format '%s'"), format_str);
		goto out;
	}

	if (repo->ref_storage_format == format) {
		err = error(_("repository already uses '%s' format"),
			    ref_storage_format_to_name(format));
		goto out;
	}

	if (repo_migrate_ref_storage_format(repo, format, flags, &errbuf) < 0) {
		err = error("%s", errbuf.buf);
		goto out;
	}

	err = 0;

out:
	strbuf_release(&errbuf);
	return err;
}

static int cmd_refs_verify(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	struct fsck_options fsck_refs_options;
	struct worktree **worktrees;
	const char * const verify_usage[] = {
		REFS_VERIFY_USAGE,
		NULL,
	};
	struct option options[] = {
		OPT_BOOL(0, "verbose", &fsck_refs_options.verbose, N_("be verbose")),
		OPT_BOOL(0, "strict", &fsck_refs_options.strict, N_("enable strict checking")),
		OPT_END(),
	};
	int ret = 0;

	fsck_options_init(&fsck_refs_options, repo, FSCK_OPTIONS_REFS);

	argc = parse_options(argc, argv, prefix, options, verify_usage, 0);
	if (argc)
		usage(_("'git refs verify' takes no arguments"));

	repo_config(repo, git_fsck_config, &fsck_refs_options);
	prepare_repo_settings(repo);

	worktrees = get_worktrees_without_reading_head();
	for (size_t i = 0; worktrees[i]; i++)
		ret |= refs_fsck(get_worktree_ref_store(worktrees[i]),
				 &fsck_refs_options, worktrees[i]);

	fsck_options_clear(&fsck_refs_options);
	free_worktrees(worktrees);
	return ret;
}

static int cmd_refs_list(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	static char const * const refs_list_usage[] = {
		N_("git refs list " COMMON_USAGE_FOR_EACH_REF),
		NULL
	};

	return for_each_ref_core(argc, argv, prefix, repo, refs_list_usage);
}

static int cmd_refs_exists(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	struct strbuf unused_referent = STRBUF_INIT;
	struct object_id unused_oid;
	unsigned int unused_type;
	int failure_errno = 0;
	const char *ref;
	int ret = 0;
	const char * const exists_usage[] = {
		REFS_EXISTS_USAGE,
		NULL,
	};
	struct option options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, exists_usage, 0);
	if (argc != 1)
		die(_("'git refs exists' requires a reference"));

	ref = *argv++;
	if (refs_read_raw_ref(get_main_ref_store(repo), ref,
			      &unused_oid, &unused_referent, &unused_type,
			      &failure_errno)) {
		if (failure_errno == ENOENT || failure_errno == EISDIR) {
			error(_("reference does not exist"));
			ret = 2;
		} else {
			errno = failure_errno;
			error_errno(_("failed to look up reference"));
			ret = 1;
		}

		goto out;
	}

out:
	strbuf_release(&unused_referent);
	return ret;
}

static int cmd_refs_optimize(int argc, const char **argv, const char *prefix,
			     struct repository *repo)
{
	static char const * const refs_optimize_usage[] = {
		REFS_OPTIMIZE_USAGE,
		NULL
	};

	return pack_refs_core(argc, argv, prefix, repo, refs_optimize_usage);
}

static int cmd_refs_create(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	static char const * const refs_create_usage[] = {
		REFS_CREATE_USAGE,
		NULL
	};
	const char *message = NULL;
	unsigned flags = 0;
	struct option opts[] = {
		OPT_STRING(0, "message", &message, N_("reason"),
			   N_("reason of the update")),
		OPT_BIT(0 ,"no-deref", &flags,
			N_("update <refname> not the one it points to"),
			REF_NO_DEREF),
		OPT_BIT(0, "create-reflog", &flags, N_("create a reflog"),
			REF_FORCE_CREATE_REFLOG),
		OPT_END(),
	};
	struct object_id newoid;
	const char *refname;
	int ret;

	argc = parse_options(argc, argv, prefix, opts, refs_create_usage, 0);
	if (argc != 2)
		usage(_("create requires reference name and an object ID"));

	if (message && !*message)
		die(_("refusing to perform update with empty message"));

	repo_config(repo, git_default_config, NULL);

	refname = argv[0];
	if (repo_get_oid_with_flags(repo, argv[1], &newoid, GET_OID_SKIP_AMBIGUITY_CHECK))
		die(_("invalid object ID: '%s'"), argv[1]);
	if (is_null_oid(&newoid))
		die(_("cannot create reference with null old object ID"));

	ret = refs_update_ref(get_main_ref_store(repo), message, refname,
			      &newoid, null_oid(repo->hash_algo), flags,
			      UPDATE_REFS_MSG_ON_ERR);

	if (ret < 0)
		ret = 1;
	return ret;
}

static int cmd_refs_delete(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	static char const * const refs_delete_usage[] = {
		REFS_DELETE_USAGE,
		NULL
	};
	const char *message = NULL;
	unsigned flags = 0;
	struct option opts[] = {
		OPT_STRING(0, "message", &message, N_("reason"),
			   N_("reason of the update")),
		OPT_BIT(0 ,"no-deref", &flags,
			N_("update <refname> not the one it points to"),
			REF_NO_DEREF),
		OPT_END(),
	};
	struct object_id oldoid;
	const char *refname;
	int ret;

	argc = parse_options(argc, argv, prefix, opts, refs_delete_usage, 0);
	if (argc < 1 || argc > 2)
		usage(_("delete requires reference name and an optional old object ID"));

	if (message && !*message)
		die(_("refusing to perform update with empty message"));

	repo_config(repo, git_default_config, NULL);

	refname = argv[0];
	if (argc == 2) {
		if (repo_get_oid_with_flags(repo, argv[1], &oldoid, GET_OID_SKIP_AMBIGUITY_CHECK))
			die(_("invalid old object ID: '%s'"), argv[1]);
		if (is_null_oid(&oldoid))
			die(_("cannot delete reference with null old object ID"));
	}

	ret = refs_delete_ref(get_main_ref_store(repo), message, refname,
			      argc == 2 ? &oldoid : NULL, flags);

	if (ret < 0)
		ret = 1;
	return ret;
}

static int cmd_refs_update(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	static char const * const refs_update_usage[] = {
		REFS_UPDATE_USAGE,
		NULL
	};
	const char *message = NULL;
	unsigned flags = 0;
	struct option opts[] = {
		OPT_STRING(0, "message", &message, N_("reason"),
			   N_("reason of the update")),
		OPT_BIT(0 ,"no-deref", &flags,
			N_("update <refname> not the one it points to"),
			REF_NO_DEREF),
		OPT_BIT(0, "create-reflog", &flags, N_("create a reflog"),
			REF_FORCE_CREATE_REFLOG),
		OPT_END(),
	};
	struct object_id newoid, oldoid;
	const char *refname;
	int ret;

	argc = parse_options(argc, argv, prefix, opts, refs_update_usage, 0);
	if (argc < 2 || argc > 3)
		usage(_("update requires reference name, new value and an optional old value"));

	if (message && !*message)
		die(_("refusing to perform update with empty message"));

	repo_config(repo, git_default_config, NULL);

	refname = argv[0];
	if (repo_get_oid_with_flags(repo, argv[1], &newoid,
				    GET_OID_SKIP_AMBIGUITY_CHECK))
		die(_("invalid new object ID: '%s'"), argv[1]);
	if (argc == 3 &&
	    repo_get_oid_with_flags(repo, argv[2], &oldoid,
				    GET_OID_SKIP_AMBIGUITY_CHECK))
		die(_("invalid old object ID: '%s'"), argv[2]);

	ret = refs_update_ref(get_main_ref_store(repo), message, refname,
			      &newoid, argc == 3 ? &oldoid : NULL, flags,
			      UPDATE_REFS_MSG_ON_ERR);

	if (ret < 0)
		ret = 1;
	return ret;
}

static int cmd_refs_rename(int argc, const char **argv, const char *prefix,
			   struct repository *repo)
{
	static char const * const refs_rename_usage[] = {
		REFS_RENAME_USAGE,
		NULL
	};
	const char *message = NULL;
	struct option opts[] = {
		OPT_STRING(0, "message", &message, N_("reason"),
			   N_("reason of the update")),
		OPT_END(),
	};
	const char *oldref, *newref;
	int ret;

	argc = parse_options(argc, argv, prefix, opts, refs_rename_usage, 0);
	if (argc != 2)
		usage(_("rename requires old and new reference name"));
	if (message && !*message)
		die(_("refusing to perform update with empty message"));

	repo_config(repo, git_default_config, NULL);

	oldref = argv[0];
	newref = argv[1];

	if (check_refname_format(oldref, 0))
		die(_("invalid ref format: '%s'"), oldref);
	if (check_refname_format(newref, 0))
		die(_("invalid ref format: '%s'"), newref);

	if (!refs_ref_exists(get_main_ref_store(repo), oldref))
		die(_("reference does not exist: '%s'"), oldref);
	if (refs_ref_exists(get_main_ref_store(repo), newref))
		die(_("reference already exists: '%s'"), newref);

	ret = refs_rename_ref(get_main_ref_store(repo), oldref, newref, message);

	if (ret < 0)
		ret = 1;
	return ret;
}

int cmd_refs(int argc,
	     const char **argv,
	     const char *prefix,
	     struct repository *repo)
{
	const char * const refs_usage[] = {
		REFS_MIGRATE_USAGE,
		REFS_VERIFY_USAGE,
		"git refs list " COMMON_USAGE_FOR_EACH_REF,
		REFS_EXISTS_USAGE,
		REFS_OPTIMIZE_USAGE,
		REFS_CREATE_USAGE,
		REFS_DELETE_USAGE,
		REFS_UPDATE_USAGE,
		REFS_RENAME_USAGE,
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option opts[] = {
		OPT_SUBCOMMAND("migrate", &fn, cmd_refs_migrate),
		OPT_SUBCOMMAND("verify", &fn, cmd_refs_verify),
		OPT_SUBCOMMAND("list", &fn, cmd_refs_list),
		OPT_SUBCOMMAND("exists", &fn, cmd_refs_exists),
		OPT_SUBCOMMAND("optimize", &fn, cmd_refs_optimize),
		OPT_SUBCOMMAND("create", &fn, cmd_refs_create),
		OPT_SUBCOMMAND("delete", &fn, cmd_refs_delete),
		OPT_SUBCOMMAND("update", &fn, cmd_refs_update),
		OPT_SUBCOMMAND("rename", &fn, cmd_refs_rename),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, opts, refs_usage, 0);
	return fn(argc, argv, prefix, repo);
}
