#include "builtin.h"
#include "abspath.h"
#include "gettext.h"
#include "setup.h"
#include "strvec.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "repository.h"
#include "bundle.h"

/*
 * Basic handler for bundle files to connect repositories via sneakernet.
 * Invocation must include action.
 * This function can create a bundle or provide information on an existing
 * bundle supporting "fetch", "pull", and "ls-remote".
 */

#define BUILTIN_BUNDLE_CREATE_USAGE \
	N_("git bundle create [-q | --quiet | --progress]\n" \
	   "                  [--version=<version>] <file> <git-rev-list-args>")
#define BUILTIN_BUNDLE_VERIFY_USAGE \
	N_("git bundle verify [-q | --quiet] <file>")
#define BUILTIN_BUNDLE_LIST_HEADS_USAGE \
	N_("git bundle list-heads <file> [<refname>...]")
#define BUILTIN_BUNDLE_UNBUNDLE_USAGE \
	N_("git bundle unbundle [--progress] <file> [<refname>...]")

static char const * const builtin_bundle_usage[] = {
	BUILTIN_BUNDLE_CREATE_USAGE,
	BUILTIN_BUNDLE_VERIFY_USAGE,
	BUILTIN_BUNDLE_LIST_HEADS_USAGE,
	BUILTIN_BUNDLE_UNBUNDLE_USAGE,
	NULL,
};

static const char * const builtin_bundle_create_usage[] = {
	BUILTIN_BUNDLE_CREATE_USAGE,
	NULL
};

static const char * const builtin_bundle_verify_usage[] = {
	BUILTIN_BUNDLE_VERIFY_USAGE,
	NULL
};

static const char * const builtin_bundle_list_heads_usage[] = {
	BUILTIN_BUNDLE_LIST_HEADS_USAGE,
	NULL
};

static const char * const builtin_bundle_unbundle_usage[] = {
	BUILTIN_BUNDLE_UNBUNDLE_USAGE,
	NULL
};

static int parse_options_cmd_bundle(int argc,
		const char **argv,
		const char* prefix,
		const char * const usagestr[],
		const struct option options[],
		char **bundle_file) {
	argc = parse_options(argc, argv, NULL, options, usagestr,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_msg_opt(_("need a <file> argument"), usagestr, options);
	*bundle_file = prefix_filename_except_for_dash(prefix, argv[0]);
	return argc;
}

static int cmd_bundle_create(int argc, const char **argv, const char *prefix) {
	int all_progress_implied = 1;
	int progress = isatty(STDERR_FILENO);
	struct strvec pack_opts;
	int version = -1;
	int ret;
	struct option options[] = {
		OPT_SET_INT('q', "quiet", &progress,
			    N_("do not show progress meter"), 0),
		OPT_SET_INT(0, "progress", &progress,
			    N_("show progress meter"), 1),
		OPT_SET_INT_F(0, "all-progress", &progress,
			      N_("historical; same as --progress"), 2,
			      PARSE_OPT_HIDDEN),
		OPT_HIDDEN_BOOL(0, "all-progress-implied",
				&all_progress_implied,
				N_("historical; does nothing")),
		OPT_INTEGER(0, "version", &version,
			    N_("specify bundle format version")),
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_create_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	strvec_init(&pack_opts);
	if (progress == 0)
		strvec_push(&pack_opts, "--quiet");
	else if (progress == 1)
		strvec_push(&pack_opts, "--progress");
	else if (progress == 2)
		strvec_push(&pack_opts, "--all-progress");
	if (progress && all_progress_implied)
		strvec_push(&pack_opts, "--all-progress-implied");

	if (!startup_info->have_repository)
		die(_("Need a repository to create a bundle."));
	ret = !!create_bundle(the_repository, bundle_file, argc, argv, &pack_opts, version);
	strvec_clear(&pack_opts);
	free(bundle_file);
	return ret;
}

/*
 * Similar to read_bundle_header(), but handle "-" as stdin.
 */
static int open_bundle(const char *path, struct bundle_header *header,
		       const char **name)
{
	if (!strcmp(path, "-")) {
		if (name)
			*name = "<stdin>";
		return read_bundle_header_fd(0, header, "<stdin>");
	}

	if (name)
		*name = path;
	return read_bundle_header(path, header);
}

static int cmd_bundle_verify(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int quiet = 0;
	int ret;
	struct option options[] = {
		OPT_BOOL('q', "quiet", &quiet,
			    N_("do not show bundle details")),
		OPT_END()
	};
	char *bundle_file;
	const char *name;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_verify_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = open_bundle(bundle_file, &header, &name)) < 0) {
		ret = 1;
		goto cleanup;
	}
	close(bundle_fd);
	if (verify_bundle(the_repository, &header,
			  quiet ? VERIFY_BUNDLE_QUIET : VERIFY_BUNDLE_VERBOSE)) {
		ret = 1;
		goto cleanup;
	}

	fprintf(stderr, _("%s is okay\n"), name);
	ret = 0;
cleanup:
	free(bundle_file);
	bundle_header_release(&header);
	return ret;
}

static int cmd_bundle_list_heads(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int ret;
	struct option options[] = {
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_list_heads_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = open_bundle(bundle_file, &header, NULL)) < 0) {
		ret = 1;
		goto cleanup;
	}
	close(bundle_fd);
	ret = !!list_bundle_refs(&header, argc, argv);
cleanup:
	free(bundle_file);
	bundle_header_release(&header);
	return ret;
}

static int cmd_bundle_unbundle(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int ret;
	int progress = isatty(2);

	struct option options[] = {
		OPT_BOOL(0, "progress", &progress,
			 N_("show progress meter")),
		OPT_END()
	};
	char *bundle_file;
	struct strvec extra_index_pack_args = STRVEC_INIT;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_unbundle_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = open_bundle(bundle_file, &header, NULL)) < 0) {
		ret = 1;
		goto cleanup;
	}
	if (!startup_info->have_repository)
		die(_("Need a repository to unbundle."));
	if (progress)
		strvec_pushl(&extra_index_pack_args, "-v", "--progress-title",
			     _("Unbundling objects"), NULL);
	ret = !!unbundle(the_repository, &header, bundle_fd,
			 &extra_index_pack_args, 0) ||
		list_bundle_refs(&header, argc, argv);
	bundle_header_release(&header);
cleanup:
	free(bundle_file);
	return ret;
}

int cmd_bundle(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("create", &fn, cmd_bundle_create),
		OPT_SUBCOMMAND("verify", &fn, cmd_bundle_verify),
		OPT_SUBCOMMAND("list-heads", &fn, cmd_bundle_list_heads),
		OPT_SUBCOMMAND("unbundle", &fn, cmd_bundle_unbundle),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, builtin_bundle_usage,
			     0);

	packet_trace_identity("bundle");

	return !!fn(argc, argv, prefix);
}
