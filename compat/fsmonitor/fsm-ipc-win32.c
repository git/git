#include "git-compat-util.h"
#include "config.h"
#include "fsmonitor-ipc.h"
#include "path.h"

const char *fsmonitor_ipc__get_path(struct repository *r) {
	static char *ret;
	if (!ret)
		ret = repo_git_path(r, "fsmonitor--daemon.ipc");
	return ret;
}
