#include "cache.h"
#include "rev-cache.h"

static const char missing_revs_usage[] =
"git-missing-revs <rev-cache-file> <want-sha1>...";

#define REV_WANT 01
#define REV_HAVE 02

static void process(struct rev_cache *head_list)
{
	while (head_list) {
		struct rev_cache *rc = head_list;
		struct rev_list_elem *e;
		head_list = rc->head_list;
		rc->head_list = NULL;
		if (has_sha1_file(rc->sha1)) {
			rc->work |= REV_HAVE;
			continue;
		}
		if (rc->work & (REV_WANT|REV_HAVE))
			continue;
		rc->work |= REV_WANT;
		printf("%s\n", sha1_to_hex(rc->sha1));
		for (e = rc->parents; e; e = e->next) {
			if (e->ri->work & REV_HAVE)
				continue;
			e->ri->head_list = head_list;
			head_list = e->ri;
		}
	}
}

int main(int ac, char **av)
{
	const char *rev_cache_file;
	struct rev_cache *head_list = NULL;
	int i;

	if (ac < 3)
		usage(missing_revs_usage);
	rev_cache_file = av[1];
	read_rev_cache(rev_cache_file, NULL, 0);
	for (i = 2; i < ac; i++) {
		unsigned char sha1[20];
		int pos;
		struct rev_cache *rc;
		if (get_sha1_hex(av[i], sha1))
			die("%s: not an SHA1", av[i]);
		if ((pos = find_rev_cache(sha1)) < 0) {
			/* We could be asked for tags, which would not
			 * appear in the rev-cache.
			 */
			puts(av[i]);
			continue;
		}
		rc = rev_cache[pos];
		rc->head_list = head_list;
		head_list = rc;
	}
	process(head_list);
	return 0;
}
