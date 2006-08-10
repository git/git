#include "cache.h"
#include "pack.h"

static int verify_one_pack(const char *path, int verbose)
{
	char arg[PATH_MAX];
	int len;
	struct packed_git *g;

	len = strlcpy(arg, path, PATH_MAX);
	if (len >= PATH_MAX)
		return error("name too long: %s", path);

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
	int nothing_done = 1;

	while (1 < ac) {
		if (!no_more_options && av[1][0] == '-') {
			if (!strcmp("-v", av[1]))
				verbose = 1;
			else if (!strcmp("--", av[1]))
				no_more_options = 1;
			else
				usage(verify_pack_usage);
		}
		else {
			if (verify_one_pack(av[1], verbose))
				errs++;
			nothing_done = 0;
		}
		ac--; av++;
	}

	if (nothing_done)
		usage(verify_pack_usage);

	return !!errs;
}
