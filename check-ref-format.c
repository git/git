/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"

#include <stdio.h>

int main(int ac, char **av)
{
	if (ac != 2)
		usage("git-check-ref-format refname");
	if (check_ref_format(av[1]))
		exit(1);
	return 0;
}
