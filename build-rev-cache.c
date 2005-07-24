#include "refs.h"
#include "cache.h"
#include "commit.h"
#include "rev-cache.h"

static void process_head_list(int verbose)
{
	char buf[512];

	while (fgets(buf, sizeof(buf), stdin)) {
		unsigned char sha1[20];
		struct commit *commit;

		if (get_sha1_hex(buf, sha1)) {
			error("ignoring: %s", buf);
			continue;
		}
		if (!(commit = lookup_commit_reference(sha1))) {
			error("not a commit: %s", sha1_to_hex(sha1));
			continue;
		}
		record_rev_cache(commit->object.sha1, verbose ? stderr : NULL);
	}
}


static const char *build_rev_cache_usage =
"git-build-rev-cache <rev-cache-file> < list-of-heads";

int main(int ac, char **av)
{
	int verbose = 0;
	const char *path;

	while (1 < ac && av[1][0] == '-') {
		if (!strcmp(av[1], "-v"))
			verbose = 1;
		else
			usage(build_rev_cache_usage);
		ac--; av++;
	}

	if (ac != 2)
		usage(build_rev_cache_usage);

	path = av[1];

	/* read existing rev-cache */
	read_rev_cache(path, NULL, 0);

	process_head_list(verbose);

	/* update the rev-cache database by appending newly found one to it */
	write_rev_cache(path, path);
	return 0;
}
