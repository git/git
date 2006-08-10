#include "builtin.h"
#include "cache.h"
#include "pack.h"

static int verify_one_pack(const char *path, int verbose)
{
	char arg[PATH_MAX];
	int len;
	struct packed_git *pack;
	int err;

	len = strlcpy(arg, path, PATH_MAX);
	if (len >= PATH_MAX)
		return error("name too long: %s", path);

	/*
	 * In addition to "foo.idx" we accept "foo.pack" and "foo";
	 * normalize these forms to "foo.idx" for add_packed_git().
	 */
	if (has_extension(arg, len, ".pack")) {
		strcpy(arg + len - 5, ".idx");
		len--;
	} else if (!has_extension(arg, len, ".idx")) {
		if (len + 4 >= PATH_MAX)
			return error("name too long: %s.idx", arg);
		strcpy(arg + len, ".idx");
		len += 4;
	}

	/*
	 * add_packed_git() uses our buffer (containing "foo.idx") to
	 * build the pack filename ("foo.pack").  Make sure it fits.
	 */
	if (len + 1 >= PATH_MAX) {
		arg[len - 4] = '\0';
		return error("name too long: %s.pack", arg);
	}

	pack = add_packed_git(arg, len, 1);
	if (!pack)
		return error("packfile %s not found.", arg);

	err = verify_pack(pack, verbose);
	free(pack);

	return err;
}

static const char verify_pack_usage[] = "git-verify-pack [-v] <pack>...";

int cmd_verify_pack(int argc, const char **argv, const char *prefix)
{
	int err = 0;
	int verbose = 0;
	int no_more_options = 0;
	int nothing_done = 1;

	while (1 < argc) {
		if (!no_more_options && argv[1][0] == '-') {
			if (!strcmp("-v", argv[1]))
				verbose = 1;
			else if (!strcmp("--", argv[1]))
				no_more_options = 1;
			else
				usage(verify_pack_usage);
		}
		else {
			if (verify_one_pack(argv[1], verbose))
				err = 1;
			nothing_done = 0;
		}
		argc--; argv++;
	}

	if (nothing_done)
		usage(verify_pack_usage);

	return err;
}
