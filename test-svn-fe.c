/*
 * test-svn-fe: Code to exercise the svn import lib
 */

#include "git-compat-util.h"
#include "vcs-svn/svndump.h"

int main(int argc, char *argv[])
{
	if (argc != 2)
		usage("test-svn-fe <file>");
	if (svndump_init(argv[1]))
		return 1;
	svndump_read(NULL);
	svndump_deinit();
	svndump_reset();
	return 0;
}
