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
	N_("git replace [-f] <object> <replacement>"),
	N_("git replace -d <object>..."),
	N_("git replace -l [<pattern>]"),
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
		if (read_ref(ref, sha1)) {
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

static int replace_object(const char *object_ref, const char *replace_ref,
			  int force)
{
	unsigned char object[20], prev[20], repl[20];
	char ref[PATH_MAX];
	struct ref_lock *lock;

	if (get_sha1(object_ref, object))
		die("Failed to resolve '%s' as a valid ref.", object_ref);
	if (get_sha1(replace_ref, repl))
		die("Failed to resolve '%s' as a valid ref.", replace_ref);

	if (snprintf(ref, sizeof(ref),
		     "refs/replace/%s",
		     sha1_to_hex(object)) > sizeof(ref) - 1)
		die("replace ref name too long: %.*s...", 50, ref);
	if (check_refname_format(ref, 0))
		die("'%s' is not a valid ref name.", ref);

	if (read_ref(ref, prev))
		hashclr(prev);
	else if (!force)
		die("replace ref '%s' already exists", ref);

	lock = lock_any_ref_for_update(ref, prev, 0);
	if (!lock)
		die("%s: cannot lock the ref", ref);
	if (write_ref_sha1(lock, repl, NULL) < 0)
		die("%s: cannot update the ref", ref);

	return 0;
}

int cmd_replace(int argc, const char **argv, const char *prefix)
{
	int list = 0, delete = 0, force = 0;
	struct option options[] = {
		OPT_BOOLEAN('l', NULL, &list, N_("list replace refs")),
		OPT_BOOLEAN('d', NULL, &delete, N_("delete replace refs")),
		OPT_BOOLEAN('f', NULL, &force, N_("replace the ref if it exists")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_replace_usage, 0);

	if (list && delete)
		usage_msg_opt("-l and -d cannot be used together",
			      git_replace_usage, options);

	if (force && (list || delete))
		usage_msg_opt("-f cannot be used with -d or -l",
			      git_replace_usage, options);

	/* Delete refs */
	if (delete) {
		if (argc < 1)
			usage_msg_opt("-d needs at least one argument",
				      git_replace_usage, options);
		return for_each_replace_name(argv, delete_replace_ref);
	}

	/* Replace object */
	if (!list && argc) {
		if (argc != 2)
			usage_msg_opt("bad number of arguments",
				      git_replace_usage, options);
		return replace_object(argv[0], argv[1], force);
	}

	/* List refs, even if "list" is not set */
	if (argc > 1)
		usage_msg_opt("only one pattern can be given with -l",
			      git_replace_usage, options);
	if (force)
		usage_msg_opt("-f needs some arguments",
			      git_replace_usage, options);

	return list_replace_refs(argv[0]);
}
