#include "builtin.h"
#include "strvec.h"
#include "parse-options.h"
#include "cache.h"
#include "bundle.h"

/*
 * Basic handler for bundle files to connect repositories via sneakernet.
 * Invocation must include action.
 * This function can create a bundle or provide information on an existing
 * bundle supporting "fetch", "pull", and "ls-remote".
 */

static const char * const builtin_bundle_usage[] = {
  N_("git bundle create [<options>] <file> <git-rev-list args>"),
  N_("git bundle verify [<options>] <file>"),
  N_("git bundle list-heads <file> [<refname>...]"),
  N_("git bundle unbundle <file> [<refname>...]"),
  NULL
};

static const char * const builtin_bundle_create_usage[] = {
  N_("git bundle create [<options>] <file> <git-rev-list args>"),
  NULL
};

static const char * const builtin_bundle_verify_usage[] = {
  N_("git bundle verify [<options>] <file>"),
  NULL
};

static const char * const builtin_bundle_list_heads_usage[] = {
  N_("git bundle list-heads <file> [<refname>...]"),
  NULL
};

static const char * const builtin_bundle_unbundle_usage[] = {
  N_("git bundle unbundle <file> [<refname>...]"),
  NULL
};

static int verbose;

static int parse_options_cmd_bundle(int argc,
		const char **argv,
		const char* prefix,
		const char * const usagestr[],
		const struct option options[],
		char **bundle_file) {
	int newargc;
	newargc = parse_options(argc, argv, NULL, options, usagestr,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		usage_with_options(usagestr, options);
	*bundle_file = prefix_filename(prefix, argv[0]);
	return newargc;
}

static int cmd_bundle_create(int argc, const char **argv, const char *prefix) {
	int all_progress_implied = 0;
	int progress = isatty(2);
	struct strvec pack_opts;
	int version = -1;
	int ret;
	struct option options[] = {
		OPT_SET_INT('q', "quiet", &progress,
			    N_("do not show progress meter"), 0),
		OPT_SET_INT(0, "progress", &progress,
			    N_("show progress meter"), 1),
		OPT_SET_INT(0, "all-progress", &progress,
			    N_("show progress meter during object writing phase"), 2),
		OPT_BOOL(0, "all-progress-implied",
			 &all_progress_implied,
			 N_("similar to --all-progress when progress meter is shown")),
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
	free(bundle_file);
	return ret;
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

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_verify_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
		ret = 1;
		goto cleanup;
	}
	close(bundle_fd);
	if (verify_bundle(the_repository, &header, !quiet)) {
		ret = 1;
		goto cleanup;
	}

	fprintf(stderr, _("%s is okay\n"), bundle_file);
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

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
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
	struct option options[] = {
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_unbundle_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
		ret = 1;
		goto cleanup;
	}
	if (!startup_info->have_repository)
		die(_("Need a repository to unbundle."));
	ret = !!unbundle(the_repository, &header, bundle_fd, 0) ||
		list_bundle_refs(&header, argc, argv);
	bundle_header_release(&header);
cleanup:
	free(bundle_file);
	return ret;
}

int cmd_bundle(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose; must be placed before a subcommand")),
		OPT_END()
	};
	int result;

	argc = parse_options(argc, argv, prefix, options, builtin_bundle_usage,
		PARSE_OPT_STOP_AT_NON_OPTION);

	packet_trace_identity("bundle");

	if (argc < 2)
		usage_with_options(builtin_bundle_usage, options);

	else if (!strcmp(argv[0], "create"))
		result = cmd_bundle_create(argc, argv, prefix);
	else if (!strcmp(argv[0], "verify"))
		result = cmd_bundle_verify(argc, argv, prefix);
	else if (!strcmp(argv[0], "list-heads"))
		result = cmd_bundle_list_heads(argc, argv, prefix);
	else if (!strcmp(argv[0], "unbundle"))
		result = cmd_bundle_unbundle(argc, argv, prefix);
	else {
		error(_("Unknown subcommand: %s"), argv[0]);
		usage_with_options(builtin_bundle_usage, options);
	}
	return result ? 1 : 0;
}
