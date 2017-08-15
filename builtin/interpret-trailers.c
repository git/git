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

static int parse_opt_parse(const struct option *opt, const char *arg,
			   int unset)
{
	struct process_trailer_options *v = opt->value;
	v->only_trailers = 1;
	v->only_input = 1;
	v->unfold = 1;
	return 0;
}

int cmd_interpret_trailers(int argc, const char **argv, const char *prefix)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	struct string_list trailers = STRING_LIST_INIT_NODUP;

	struct option options[] = {
		OPT_BOOL(0, "in-place", &opts.in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &opts.trim_empty, N_("trim empty trailers")),
		OPT_BOOL(0, "only-trailers", &opts.only_trailers, N_("output only the trailers")),
		OPT_BOOL(0, "only-input", &opts.only_input, N_("do not apply config rules")),
		OPT_BOOL(0, "unfold", &opts.unfold, N_("join whitespace-continued values")),
		{ OPTION_CALLBACK, 0, "parse", &opts, NULL, N_("set parsing options"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, parse_opt_parse },
		OPT_STRING_LIST(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (opts.only_input && trailers.nr)
		usage_msg_opt(
			_("--trailer with --only-input does not make sense"),
			git_interpret_trailers_usage,
			options);

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
