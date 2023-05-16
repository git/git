/*
 * Builtin "git verify-tag"
 *
 * Copyright (c) 2007 Carlos Rica <jasampler@gmail.com>
 *
 * Based on git-verify-tag.sh
 */
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "gettext.h"
#include "tag.h"
#include "run-command.h"
#include "object-name.h"
#include "parse-options.h"
#include "gpg-interface.h"
#include "ref-filter.h"

static const char * const verify_tag_usage[] = {
		N_("git verify-tag [-v | --verbose] [--format=<format>] [--raw] <tag>..."),
		NULL
};

int cmd_verify_tag(int argc, const char **argv, const char *prefix)
{
	int i = 1, verbose = 0, had_error = 0;
	unsigned flags = 0;
	struct ref_format format = REF_FORMAT_INIT;
	const struct option verify_tag_options[] = {
		OPT__VERBOSE(&verbose, N_("print tag contents")),
		OPT_BIT(0, "raw", &flags, N_("print raw gpg status output"), GPG_VERIFY_RAW),
		OPT_STRING(0, "format", &format.format, N_("format"), N_("format to use for the output")),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, verify_tag_options,
			     verify_tag_usage, PARSE_OPT_KEEP_ARGV0);
	if (argc <= i)
		usage_with_options(verify_tag_usage, verify_tag_options);

	if (verbose)
		flags |= GPG_VERIFY_VERBOSE;

	if (format.format) {
		if (verify_ref_format(&format))
			usage_with_options(verify_tag_usage,
					   verify_tag_options);
		flags |= GPG_VERIFY_OMIT_STATUS;
	}

	while (i < argc) {
		struct object_id oid;
		const char *name = argv[i++];

		if (repo_get_oid(the_repository, name, &oid)) {
			had_error = !!error("tag '%s' not found.", name);
			continue;
		}

		if (gpg_verify_tag(&oid, name, flags)) {
			had_error = 1;
			continue;
		}

		if (format.format)
			pretty_print_ref(name, &oid, &format);
	}
	return had_error;
}
