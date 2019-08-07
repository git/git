#include "builtin.h"
#include "cache.h"
#include "bundle.h"

/*
 * Basic handler for bundle files to connect repositories via sneakernet.
 * Invocation must include action.
 * This function can create a bundle or provide information on an existing
 * bundle supporting "fetch", "pull", and "ls-remote".
 */

static const char builtin_bundle_usage[] =
  "git bundle create <file> <git-rev-list args>\n"
  "   or: git bundle verify <file>\n"
  "   or: git bundle list-heads <file> [<refname>...]\n"
  "   or: git bundle unbundle <file> [<refname>...]";

int cmd_bundle(int argc, const char **argv, const char *prefix)
{
	struct bundle_header header;
	const char *cmd, *bundle_file;
	int bundle_fd = -1;

	if (argc < 3)
		usage(builtin_bundle_usage);

	cmd = argv[1];
	bundle_file = prefix_filename(prefix, argv[2]);
	argc -= 2;
	argv += 2;

	memset(&header, 0, sizeof(header));
	if (strcmp(cmd, "create") && (bundle_fd =
				read_bundle_header(bundle_file, &header)) < 0)
		return 1;

	if (!strcmp(cmd, "verify")) {
		close(bundle_fd);
		if (argc != 1) {
			usage(builtin_bundle_usage);
			return 1;
		}
		if (verify_bundle(the_repository, &header, 1))
			return 1;
		fprintf(stderr, _("%s is okay\n"), bundle_file);
		return 0;
	}
	if (!strcmp(cmd, "list-heads")) {
		close(bundle_fd);
		return !!list_bundle_refs(&header, argc, argv);
	}
	if (!strcmp(cmd, "create")) {
		if (argc < 2) {
			usage(builtin_bundle_usage);
			return 1;
		}
		if (!startup_info->have_repository)
			die(_("Need a repository to create a bundle."));
		return !!create_bundle(the_repository, bundle_file, argc, argv);
	} else if (!strcmp(cmd, "unbundle")) {
		if (!startup_info->have_repository)
			die(_("Need a repository to unbundle."));
		return !!unbundle(the_repository, &header, bundle_fd, 0) ||
			list_bundle_refs(&header, argc, argv);
	} else
		usage(builtin_bundle_usage);
}
