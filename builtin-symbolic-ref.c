#include "builtin.h"
#include "cache.h"
#include "refs.h"

static const char git_symbolic_ref_usage[] =
"git-symbolic-ref [-q] name [ref]";

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

	git_config(git_default_config);

	while (1 < argc) {
		const char *arg = argv[1];
		if (arg[0] != '-')
			break;
		else if (!strcmp("-q", arg))
			quiet = 1;
		else if (!strcmp("--", arg)) {
			argc--;
			argv++;
			break;
		}
		else
			die("unknown option %s", arg);
		argc--;
		argv++;
	}

	switch (argc) {
	case 2:
		check_symref(argv[1], quiet);
		break;
	case 3:
		create_symref(argv[1], argv[2]);
		break;
	default:
		usage(git_symbolic_ref_usage);
	}
	return 0;
}
