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
	list_add_tail(&item->list, trailers);
	return 0;
}

int cmd_interpret_trailers(int argc, const char **argv, const char *prefix)
{
	int in_place = 0;
	int trim_empty = 0;
	LIST_HEAD(trailers);

	struct option options[] = {
		OPT_BOOL(0, "in-place", &in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &trim_empty, N_("trim empty trailers")),

		OPT_CALLBACK(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add"), option_parse_trailer),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (argc) {
		int i;
		for (i = 0; i < argc; i++)
			process_trailers(argv[i], in_place, trim_empty, &trailers);
	} else {
		if (in_place)
			die(_("no input file given for in-place editing"));
		process_trailers(NULL, in_place, trim_empty, &trailers);
	}

	new_trailers_clear(&trailers);

	return 0;
}
