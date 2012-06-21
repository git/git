#include "git-compat-util.h"
#include "version.h"

const char git_version_string[] = GIT_VERSION;

const char *git_user_agent(void)
{
	static const char *agent = NULL;

	if (!agent) {
		agent = getenv("GIT_USER_AGENT");
		if (!agent)
			agent = GIT_USER_AGENT;
	}

	return agent;
}
