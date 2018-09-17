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

static enum trailer_where where;
static enum trailer_if_exists if_exists;
static enum trailer_if_missing if_missing;

static int option_parse_where(const struct option *opt,
			      const char *arg, int unset)
{
	return trailer_set_where(&where, arg);
}

static int option_parse_if_exists(const struct option *opt,
				  const char *arg, int unset)
{
	return trailer_set_if_exists(&if_exists, arg);
}

static int option_parse_if_missing(const struct option *opt,
				   const char *arg, int unset)
{
	return trailer_set_if_missing(&if_missing, arg);
}

static void new_trailers_clear(struct list_head *trailers)
{
	struct list_head *pos, *tmp;
	struct new_trailer_item *item;

	list_for_each_safe(pos, tmp, trailers) {
		item = list_entry(pos, struct new_trailer_item, list);
		list_del(pos);
		free(item);
	}
}

static int option_parse_trailer(const struct option *opt,
				   const char *arg, int unset)
{
	struct list_head *trailers = opt->value;
	struct new_trailer_item *item;

	if (unset) {
		new_trailers_clear(trailers);
		return 0;
	}

	if (!arg)
		return -1;

	item = xmalloc(sizeof(*item));
	item->text = arg;
	item->where = where;
	item->if_exists = if_exists;
	item->if_missing = if_missing;
	list_add_tail(&item->list, trailers);
	return 0;
}

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
	LIST_HEAD(trailers);

	struct option options[] = {
		OPT_BOOL(0, "in-place", &opts.in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &opts.trim_empty, N_("trim empty trailers")),

		OPT_CALLBACK(0, "where", NULL, N_("action"),
			     N_("where to place the new trailer"), option_parse_where),
		OPT_CALLBACK(0, "if-exists", NULL, N_("action"),
			     N_("action if trailer already exists"), option_parse_if_exists),
		OPT_CALLBACK(0, "if-missing", NULL, N_("action"),
			     N_("action if trailer is missing"), option_parse_if_missing),

		OPT_BOOL(0, "only-trailers", &opts.only_trailers, N_("output only the trailers")),
		OPT_BOOL(0, "only-input", &opts.only_input, N_("do not apply config rules")),
		OPT_BOOL(0, "unfold", &opts.unfold, N_("join whitespace-continued values")),
		{ OPTION_CALLBACK, 0, "parse", &opts, NULL, N_("set parsing options"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, parse_opt_parse },
		OPT_BOOL(0, "no-divider", &opts.no_divider, N_("do not treat --- specially")),
		OPT_CALLBACK(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add"), option_parse_trailer),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (opts.only_input && !list_empty(&trailers))
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

	new_trailers_clear(&trailers);

	return 0;
}
