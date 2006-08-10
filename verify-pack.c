#include "cache.h"
#include "pack.h"

static int verify_one_pack(char *arg, int verbose)
{
	int len = strlen(arg);
	struct packed_git *g;
	
	while (1) {
		/* Should name foo.idx, but foo.pack may be named;
		 * convert it to foo.idx
		 */
		if (has_extension(arg, len, ".pack")) {
			strcpy(arg + len - 5, ".idx");
			len--;
		}
		/* Should name foo.idx now */
		if ((g = add_packed_git(arg, len, 1)))
			break;
		/* No?  did you name just foo? */
		strcpy(arg + len, ".idx");
		len += 4;
		if ((g = add_packed_git(arg, len, 1)))
			break;
		return error("packfile %s not found.", arg);
	}
	return verify_pack(g, verbose);
}

static const char verify_pack_usage[] = "git-verify-pack [-v] <pack>...";

int main(int ac, char **av)
{
	int errs = 0;
	int verbose = 0;
	int no_more_options = 0;

	while (1 < ac) {
		char path[PATH_MAX];

		if (!no_more_options && av[1][0] == '-') {
			if (!strcmp("-v", av[1]))
				verbose = 1;
			else if (!strcmp("--", av[1]))
				no_more_options = 1;
			else
				usage(verify_pack_usage);
		}
		else {
			strcpy(path, av[1]);
			if (verify_one_pack(path, verbose))
				errs++;
		}
		ac--; av++;
	}
	return !!errs;
}
