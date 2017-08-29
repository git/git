#include "builtin.h"
#include "argv-array.h"
#include "parse-options.h"
#include "cache.h"
#include "bundle.h"
#include "config.h"

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
		const char **bundle_file) {
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
	int progress = isatty(STDERR_FILENO);
	struct argv_array pack_opts;

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
		OPT_END()
	};
	const char* bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_create_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	argv_array_init(&pack_opts);
	if (progress == 0)
		argv_array_push(&pack_opts, "--quiet");
	else if (progress == 1)
		argv_array_push(&pack_opts, "--progress");
	else if (progress == 2)
		argv_array_push(&pack_opts, "--all-progress");
	if (progress && all_progress_implied)
		argv_array_push(&pack_opts, "--all-progress-implied");

	if (!startup_info->have_repository)
		die(_("Need a repository to create a bundle."));
	return !!create_bundle(the_repository, bundle_file, argc, argv, &pack_opts);
}

static int cmd_bundle_verify(int argc, const char **argv, const char *prefix) {
	struct bundle_header header;
	int bundle_fd = -1;
	int quiet = 0;

	struct option options[] = {
		OPT_BOOL('q', "quiet", &quiet,
			    N_("do not show bundle details")),
		OPT_END()
	};
	const char* bundle_file;

	git_config(git_default_config, NULL);
	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_verify_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	memset(&header, 0, sizeof(header));
	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0)
		return 1;
	close(bundle_fd);
	if (verify_bundle(the_repository, &header, !quiet))
		return 1;
	fprintf(stderr, _("%s is okay\n"), bundle_file);
	return 0;
}

static int cmd_bundle_list_heads(int argc, const char **argv, const char *prefix) {
	struct bundle_header header;
	int bundle_fd = -1;

	struct option options[] = {
		OPT_END()
	};
	const char* bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_list_heads_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	memset(&header, 0, sizeof(header));
	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0)
		return 1;
	close(bundle_fd);
	return !!list_bundle_refs(&header, argc, argv);
}

static int cmd_bundle_unbundle(int argc, const char **argv, const char *prefix) {
	struct bundle_header header;
	int bundle_fd = -1;

	struct option options[] = {
		OPT_END()
	};
	const char* bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_unbundle_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	memset(&header, 0, sizeof(header));
	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0)
		return 1;
	if (!startup_info->have_repository)
		die(_("Need a repository to unbundle."));
	return !!unbundle(the_repository, &header, bundle_fd, 0) ||
		list_bundle_refs(&header, argc, argv);
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
