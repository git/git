#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "parse-options.h"

static const char * const git_symbolic_ref_usage[] = {
	"git symbolic-ref [options] name [ref]",
	NULL
};

static void check_symref(const char *HEAD, int quiet)
{
	unsigned char sha1[20];
	int flag;
	const char *refs_heads_master = resolve_ref(HEAD, sha1, 0, &flag);

	if (!refs_heads_master)
		die("No such ref: %s", HEAD);
	else if (!(flag & REF_ISSYMREF)) {
		if (!quiet)
			die("ref %s is not a symbolic ref", HEAD);
		else
			exit(1);
	}
	puts(refs_heads_master);
}

int cmd_symbolic_ref(int argc, const char **argv, const char *prefix)
{
	int quiet = 0;
	const char *msg = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet),
		OPT_STRING('m', NULL, &msg, "reason", "reason of the update"),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options,
			     git_symbolic_ref_usage, 0);
	if (msg &&!*msg)
		die("Refusing to perform update with empty message");
	switch (argc) {
	case 1:
		check_symref(argv[0], quiet);
		break;
	case 2:
		if (!strcmp(argv[0], "HEAD") &&
		    prefixcmp(argv[1], "refs/"))
			die("Refusing to point HEAD outside of refs/");
		create_symref(argv[0], argv[1], msg);
		break;
	default:
		usage_with_options(git_symbolic_ref_usage, options);
	}
	return 0;
}
