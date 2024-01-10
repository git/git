#include "test-tool.h"
#include "git-compat-util.h"
#include "strbuf.h"
#include "iterator.h"
#include "dir-iterator.h"

static const char *error_name(int error_number)
{
	switch (error_number) {
	case ENOENT: return "ENOENT";
	case ENOTDIR: return "ENOTDIR";
	default: return "ESOMETHINGELSE";
	}
}

/*
 * usage:
 * tool-test dir-iterator [--pedantic] directory_path
 */
int cmd__dir_iterator(int argc, const char **argv)
{
	struct dir_iterator *diter;
	unsigned int flags = 0;
	int iter_status;

	for (++argv, --argc; *argv && starts_with(*argv, "--"); ++argv, --argc) {
		if (strcmp(*argv, "--pedantic") == 0)
			flags |= DIR_ITERATOR_PEDANTIC;
		else
			die("invalid option '%s'", *argv);
	}

	if (!*argv || argc != 1)
		die("dir-iterator needs exactly one non-option argument");

	diter = dir_iterator_begin(*argv, flags);

	if (!diter) {
		printf("dir_iterator_begin failure: %s\n", error_name(errno));
		exit(EXIT_FAILURE);
	}

	while ((iter_status = dir_iterator_advance(diter)) == ITER_OK) {
		if (S_ISDIR(diter->st.st_mode))
			printf("[d] ");
		else if (S_ISREG(diter->st.st_mode))
			printf("[f] ");
		else if (S_ISLNK(diter->st.st_mode))
			printf("[s] ");
		else
			printf("[?] ");

		printf("(%s) [%s] %s\n", diter->relative_path, diter->basename,
		       diter->path.buf);
	}

	if (iter_status != ITER_DONE) {
		printf("dir_iterator_advance failure\n");
		return 1;
	}

	return 0;
}
