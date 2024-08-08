#include "builtin.h"
#include "config.h"
#include "fsck.h"
#include "parse-options.h"
#include "refs.h"
#include "repository.h"
#include "strbuf.h"

#define REFS_MIGRATE_USAGE \
	N_("git refs migrate --ref-format=<format> [--dry-run]")

#define REFS_VERIFY_USAGE \
	N_("git refs verify [--strict] [--verbose]")

static int cmd_refs_migrate(int argc, const char **argv, const char *prefix)
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
		OPT_END(),
	};
	struct strbuf errbuf = STRBUF_INIT;
	int err;

	argc = parse_options(argc, argv, prefix, options, migrate_usage, 0);
	if (argc)
		die(_("'git refs migrate' takes no arguments"));
	if (!format_str)
		die(_("'git refs migrate' needs '--ref-format=<format>'"));

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

static int cmd_refs_verify(int argc, const char **argv, const char *prefix)
{
	struct fsck_options fsck_refs_options = FSCK_REFS_OPTIONS_DEFAULT;
	const char * const verify_usage[] = {
		REFS_VERIFY_USAGE,
		NULL,
	};
	struct option options[] = {
		OPT_BOOL(0, "verbose", &fsck_refs_options.verbose, N_("be verbose")),
		OPT_BOOL(0, "strict", &fsck_refs_options.strict, N_("enable strict checking")),
		OPT_END(),
	};
	int ret;

	argc = parse_options(argc, argv, prefix, options, verify_usage, 0);
	if (argc)
		usage(_("'git refs verify' takes no arguments"));

	git_config(git_fsck_config, &fsck_refs_options);
	prepare_repo_settings(the_repository);

	ret = refs_fsck(get_main_ref_store(the_repository), &fsck_refs_options);

	fsck_options_clear(&fsck_refs_options);
	return ret;
}

int cmd_refs(int argc, const char **argv, const char *prefix)
{
	const char * const refs_usage[] = {
		REFS_MIGRATE_USAGE,
		REFS_VERIFY_USAGE,
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option opts[] = {
		OPT_SUBCOMMAND("migrate", &fn, cmd_refs_migrate),
		OPT_SUBCOMMAND("verify", &fn, cmd_refs_verify),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, opts, refs_usage, 0);
	return fn(argc, argv, prefix);
}
