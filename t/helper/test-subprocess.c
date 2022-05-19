#include "test-tool.h"
#include "cache.h"
#include "run-command.h"

int cmd__subprocess(int argc, const char **argv)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int nobut = 0;

	setup_but_directory_gently(&nobut);
	if (nobut)
		die("No but repo found");
	if (argc > 1 && !strcmp(argv[1], "--setup-work-tree")) {
		setup_work_tree();
		argv++;
	}
	cp.but_cmd = 1;
	strvec_pushv(&cp.args, (const char **)argv + 1);
	return run_command(&cp);
}
