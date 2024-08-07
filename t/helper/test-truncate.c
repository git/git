#include "test-tool.h"
#include "git-compat-util.h"

/*
 * Truncate a file to the given size.
 */
int cmd__truncate(int argc, const char **argv)
{
	char *p = NULL;
	uintmax_t sz = 0;
	int fd = -1;

	if (argc != 3)
		die("expected filename and size");

	sz = strtoumax(argv[2], &p, 0);
	if (*p)
		die("invalid size");

	fd = xopen(argv[1], O_WRONLY | O_CREAT, 0600);

	if (ftruncate(fd, (off_t) sz) < 0)
		die_errno("failed to truncate file");
	return 0;
}
