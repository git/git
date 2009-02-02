/*
 * Builtin "git replace"
 *
 * Copyright (c) 2008 Christian Couder <chriscool@tuxfamily.org>
 *
 * Based on builtin-tag.c by Kristian Høgsberg <krh@redhat.com>
 * and Carlos Rica <jasampler@gmail.com> that was itself based on
 * git-tag.sh and mktag.c by Linus Torvalds.
 */

#include "cache.h"
#include "builtin.h"
#include "refs.h"
#include "parse-options.h"

static const char * const git_replace_usage[] = {
	"git replace -d <object>...",
	"git replace -l [<pattern>]",
	NULL
};

static int show_reference(const char *refname, const unsigned char *sha1,
			  int flag, void *cb_data)
{
	const char *pattern = cb_data;

	if (!fnmatch(pattern, refname, 0))
		printf("%s\n", refname);

	return 0;
}

static int list_replace_refs(const char *pattern)
{
	if (pattern == NULL)
		pattern = "*";

	for_each_replace_ref(show_reference, (void *) pattern);

	return 0;
}

typedef int (*each_replace_name_fn)(const char *name, const char *ref,
				    const unsigned char *sha1);

static int for_each_replace_name(const char **argv, each_replace_name_fn fn)
{
	const char **p;
	char ref[PATH_MAX];
	int had_error = 0;
	unsigned char sha1[20];

	for (p = argv; *p; p++) {
		if (snprintf(ref, sizeof(ref), "refs/replace/%s", *p)
					>= sizeof(ref)) {
			error("replace ref name too long: %.*s...", 50, *p);
			had_error = 1;
			continue;
		}
		if (!resolve_ref(ref, sha1, 1, NULL)) {
			error("replace ref '%s' not found.", *p);
			had_error = 1;
			continue;
		}
		if (fn(*p, ref, sha1))
			had_error = 1;
	}
	return had_error;
}

static int delete_replace_ref(const char *name, const char *ref,
			      const unsigned char *sha1)
{
	if (delete_ref(ref, sha1, 0))
		return 1;
	printf("Deleted replace ref '%s'\n", name);
	return 0;
}

int cmd_replace(int argc, const char **argv, const char *prefix)
{
	int list = 0, delete = 0;
	struct option options[] = {
		OPT_BOOLEAN('l', NULL, &list, "list replace refs"),
		OPT_BOOLEAN('d', NULL, &delete, "delete replace refs"),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, git_replace_usage, 0);

	if (list && delete)
		usage_with_options(git_replace_usage, options);

	if (delete) {
		if (argc < 1)
			usage_with_options(git_replace_usage, options);
		return for_each_replace_name(argv, delete_replace_ref);
	}

	/* List refs, even if "list" is not set */
	if (argc > 1)
		usage_with_options(git_replace_usage, options);

	return list_replace_refs(argv[0]);
}
