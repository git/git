/*
 * test-svn-fe: Code to exercise the svn import lib
 */

#include "git-compat-util.h"
#include "vcs-svn/svndump.h"
#include "vcs-svn/svndiff.h"
#include "vcs-svn/sliding_window.h"
#include "vcs-svn/line_buffer.h"

static const char test_svnfe_usage[] =
	"test-svn-fe (<dumpfile> | [-d] <preimage> <delta> <len>)";

static int apply_delta(int argc, char *argv[])
{
	struct line_buffer preimage = LINE_BUFFER_INIT;
	struct line_buffer delta = LINE_BUFFER_INIT;
	struct sliding_view preimage_view = SLIDING_VIEW_INIT(&preimage, -1);

	if (argc != 5)
		usage(test_svnfe_usage);

	if (buffer_init(&preimage, argv[2]))
		die_errno("cannot open preimage");
	if (buffer_init(&delta, argv[3]))
		die_errno("cannot open delta");
	if (svndiff0_apply(&delta, (off_t) strtoull(argv[4], NULL, 0),
					&preimage_view, stdout))
		return 1;
	if (buffer_deinit(&preimage))
		die_errno("cannot close preimage");
	if (buffer_deinit(&delta))
		die_errno("cannot close delta");
	strbuf_release(&preimage_view.buf);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc == 2) {
		if (svndump_init(argv[1]))
			return 1;
		svndump_read(NULL, "refs/heads/master", "refs/notes/svn/revs");
		svndump_deinit();
		svndump_reset();
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "-d"))
		return apply_delta(argc, argv);
	usage(test_svnfe_usage);
}
