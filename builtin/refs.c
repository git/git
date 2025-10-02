#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "fsck.h"
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

static int cmd_refs_migrate(int argc, const char **argv, const char *prefix,
			    struct repository *repo UNUSED)
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

	if (the_repository->ref_storage_format == format) {
		err = error(_("repository already uses '%s' format"),
			    ref_storage_format_to_name(format));
		goto out;
	}

	if (repo_migrate_ref_storage_format(the_repository, format, flags, &errbuf) < 0) {
		err = error("%s", errbuf.buf);
		goto out;
	}

	err = 0;

out:
	strbuf_release(&errbuf);
	return err;
}

static int cmd_refs_verify(int argc, const char **argv, const char *prefix,
			   struct repository *repo UNUSED)
{
	struct fsck_options fsck_refs_options = FSCK_REFS_OPTIONS_DEFAULT;
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

	argc = parse_options(argc, argv, prefix, options, verify_usage, 0);
	if (argc)
		usage(_("'git refs verify' takes no arguments"));

	repo_config(the_repository, git_fsck_config, &fsck_refs_options);
	prepare_repo_settings(the_repository);

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
			   struct repository *repo UNUSED)
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
	if (refs_read_raw_ref(get_main_ref_store(the_repository), ref,
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
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option opts[] = {
		OPT_SUBCOMMAND("migrate", &fn, cmd_refs_migrate),
		OPT_SUBCOMMAND("verify", &fn, cmd_refs_verify),
		OPT_SUBCOMMAND("list", &fn, cmd_refs_list),
		OPT_SUBCOMMAND("exists", &fn, cmd_refs_exists),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, opts, refs_usage, 0);
	return fn(argc, argv, prefix, repo);
}
