#include "builtin.h"
#include "cache.h"

static const char git_symbolic_ref_usage[] =
"git-symbolic-ref name [ref]";

static void check_symref(const char *HEAD)
{
	unsigned char sha1[20];
	const char *git_HEAD = xstrdup(git_path("%s", HEAD));
	const char *git_refs_heads_master = resolve_ref(git_HEAD, sha1, 0);
	if (git_refs_heads_master) {
		/* we want to strip the .git/ part */
		int pfxlen = strlen(git_HEAD) - strlen(HEAD);
		puts(git_refs_heads_master + pfxlen);
	}
	else
		die("No such ref: %s", HEAD);
}

int cmd_symbolic_ref(int argc, const char **argv, const char *prefix)
{
	git_config(git_default_config);
	switch (argc) {
	case 2:
		check_symref(argv[1]);
		break;
	case 3:
		create_symref(xstrdup(git_path("%s", argv[1])), argv[2]);
		break;
	default:
		usage(git_symbolic_ref_usage);
	}
	return 0;
}
