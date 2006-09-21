#include "builtin.h"
#include "cache.h"
#include "refs.h"

static const char git_symbolic_ref_usage[] =
"git-symbolic-ref name [ref]";

static void check_symref(const char *HEAD)
{
	unsigned char sha1[20];
	int flag;
	const char *refs_heads_master = resolve_ref(HEAD, sha1, 0, &flag);

	if (!refs_heads_master)
		die("No such ref: %s", HEAD);
	else if (!(flag & REF_ISSYMREF))
		die("ref %s is not a symbolic ref", HEAD);
	puts(refs_heads_master);
}

int cmd_symbolic_ref(int argc, const char **argv, const char *prefix)
{
	git_config(git_default_config);
	switch (argc) {
	case 2:
		check_symref(argv[1]);
		break;
	case 3:
		create_symref(argv[1], argv[2]);
		break;
	default:
		usage(git_symbolic_ref_usage);
	}
	return 0;
}
