#include "cache.h"
#include "rev-cache.h"

static char *show_rev_cache_usage =
"git-show-rev-cache <rev-cache-file>";

int main(int ac, char **av)
{
	while (1 < ac && av[0][1] == '-') {
		/* do flags here */
		break;
		ac--; av++;
	}
	if (ac != 2)
		usage(show_rev_cache_usage);

	return read_rev_cache(av[1], stdout, 1);
}
