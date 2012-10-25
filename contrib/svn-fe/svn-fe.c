/*
 * This file is in the public domain.
 * You may freely use, modify, distribute, and relicense it.
 */

#include <stdlib.h>
#include "svndump.h"

int main(int argc, char **argv)
{
	if (svndump_init(NULL))
		return 1;
	svndump_read((argc > 1) ? argv[1] : NULL, "refs/heads/master",
			"refs/notes/svn/revs");
	svndump_deinit();
	svndump_reset();
	return 0;
}
