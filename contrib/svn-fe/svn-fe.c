/*
 * This file is in the public domain.
 * You may freely use, modify, distribute, and relicense it.
 */

#include <stdlib.h>
#include "svndump.h"

int main(int argc, char **argv)
{
	svndump_init(NULL);
	svndump_read((argc > 1) ? argv[1] : NULL);
	svndump_reset();
	return 0;
}
