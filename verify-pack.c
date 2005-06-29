#include "cache.h"
#include "pack.h"

static int verify_one_pack(char *arg)
{
	struct packed_git *g = add_packed_git(arg, strlen(arg));
	if (!g)
		return -1;
	return verify_pack(g);
}

int main(int ac, char **av)
{
	int errs = 0;

	while (1 < ac) {
		char path[PATH_MAX];
		strcpy(path, av[1]);
		if (verify_one_pack(path))
			errs++;
		else
			printf("%s: OK\n", av[1]);
		ac--; av++;
	}
	return !!errs;
}
