#include "test-tool.h"
#include "git-compat-util.h"
#include "strbuf.h"
#include "iterator.h"
#include "dir-iterator.h"

/* Argument is a directory path to iterate over */
int cmd__dir_iterator(int argc, const char **argv)
{
	struct strbuf path = STRBUF_INIT;
	struct dir_iterator *diter;

	if (argc < 2)
		die("BUG: test-dir-iterator needs one argument");

	strbuf_add(&path, argv[1], strlen(argv[1]));

	diter = dir_iterator_begin(path.buf);

	while (dir_iterator_advance(diter) == ITER_OK) {
		if (S_ISDIR(diter->st.st_mode))
			printf("[d] ");
		else if (S_ISREG(diter->st.st_mode))
			printf("[f] ");
		else
			printf("[?] ");

		printf("(%s) [%s] %s\n", diter->relative_path, diter->basename,
		       diter->path.buf);
	}

	return 0;
}
