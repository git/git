/*
 * Builtin "git interpret-trailers"
 *
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 *
 */

#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "string-list.h"
#include "trailer.h"

static const char * const git_interpret_trailers_usage[] = {
	N_("git interpret-trailers [--in-place] [--trim-empty] [(--trailer <token>[(=|:)<value>])...] [<file>...]"),
	NULL
};

int cmd_interpret_trailers(int argc, const char **argv, const char *prefix)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	struct string_list trailers = STRING_LIST_INIT_NODUP;

	struct option options[] = {
		OPT_BOOL(0, "in-place", &opts.in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &opts.trim_empty, N_("trim empty trailers")),
		OPT_STRING_LIST(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (argc) {
		int i;
		for (i = 0; i < argc; i++)
			process_trailers(argv[i], &opts, &trailers);
	} else {
		if (opts.in_place)
			die(_("no input file given for in-place editing"));
		process_trailers(NULL, &opts, &trailers);
	}

	string_list_clear(&trailers, 0);

	return 0;
}
