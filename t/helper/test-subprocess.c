#include "test-tool.h"
#include "run-command.h"
#include "setup.h"

int cmd__subprocess(int argc, const char **argv)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int nogit = 0;

	setup_git_directory_gently(&nogit);
	if (nogit)
		die("No git repo found");
	if (argc > 1 && !strcmp(argv[1], "--setup-work-tree")) {
		setup_work_tree();
		argv++;
	}
	cp.git_cmd = 1;
	strvec_pushv(&cp.args, (const char **)argv + 1);
	return run_command(&cp);
}
