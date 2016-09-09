#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "parse-options.h"

static const char * const git_symbolic_ref_usage[] = {
	N_("git symbolic-ref [<options>] <name> [<ref>]"),
	N_("git symbolic-ref -d [-q] <name>"),
	NULL
};

static int check_symref(const char *HEAD, int quiet, int shorten, int print)
{
	unsigned char sha1[20];
	int flag;
	const char *refname = resolve_ref_unsafe(HEAD, 0, sha1, &flag);

	if (!refname)
		die("No such ref: %s", HEAD);
	else if (!(flag & REF_ISSYMREF)) {
		if (!quiet)
			die("ref %s is not a symbolic ref", HEAD);
		else
			return 1;
	}
	if (print) {
		if (shorten)
			refname = shorten_unambiguous_ref(refname, 0);
		puts(refname);
	}
	return 0;
}

int cmd_symbolic_ref(int argc, const char **argv, const char *prefix)
{
	int quiet = 0, delete = 0, shorten = 0, ret = 0;
	const char *msg = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet,
			N_("suppress error message for non-symbolic (detached) refs")),
		OPT_BOOL('d', "delete", &delete, N_("delete symbolic ref")),
		OPT_BOOL(0, "short", &shorten, N_("shorten ref output")),
		OPT_STRING('m', NULL, &msg, N_("reason"), N_("reason of the update")),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options,
			     git_symbolic_ref_usage, 0);
	if (msg && !*msg)
		die("Refusing to perform update with empty message");

	if (delete) {
		if (argc != 1)
			usage_with_options(git_symbolic_ref_usage, options);
		ret = check_symref(argv[0], 1, 0, 0);
		if (ret)
			die("Cannot delete %s, not a symbolic ref", argv[0]);
		if (!strcmp(argv[0], "HEAD"))
			die("deleting '%s' is not allowed", argv[0]);
		return delete_ref(argv[0], NULL, REF_NODEREF);
	}

	switch (argc) {
	case 1:
		ret = check_symref(argv[0], quiet, shorten, 1);
		break;
	case 2:
		if (!strcmp(argv[0], "HEAD") &&
		    !starts_with(argv[1], "refs/"))
			die("Refusing to point HEAD outside of refs/");
		ret = !!create_symref(argv[0], argv[1], msg);
		break;
	default:
		usage_with_options(git_symbolic_ref_usage, options);
	}
	return ret;
}
