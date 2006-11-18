#include "wt-status.h"
#include "cache.h"

extern int wt_status_use_color;

static const char runstatus_usage[] =
"git-runstatus [--color|--nocolor] [--amend] [--verbose] [--untracked]";

int cmd_runstatus(int argc, const char **argv, const char *prefix)
{
	struct wt_status s;
	int i;

	git_config(git_status_config);
	wt_status_prepare(&s);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--color"))
			wt_status_use_color = 1;
		else if (!strcmp(argv[i], "--nocolor"))
			wt_status_use_color = 0;
		else if (!strcmp(argv[i], "--amend")) {
			s.amend = 1;
			s.reference = "HEAD^1";
		}
		else if (!strcmp(argv[i], "--verbose"))
			s.verbose = 1;
		else if (!strcmp(argv[i], "--untracked"))
			s.untracked = 1;
		else
			usage(runstatus_usage);
	}

	wt_status_print(&s);
	return s.commitable ? 0 : 1;
}
