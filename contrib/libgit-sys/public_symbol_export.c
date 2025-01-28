/*
 * Shim to publicly export Git symbols. These must be renamed so that the
 * original symbols can be hidden. Renaming these with a "libgit_" prefix also
 * avoids conflicts with other libraries such as libgit2.
 */

#include "git-compat-util.h"
#include "contrib/libgit-sys/public_symbol_export.h"
#include "version.h"

#pragma GCC visibility push(default)

const char *libgit_user_agent(void)
{
	return git_user_agent();
}

const char *libgit_user_agent_sanitized(void)
{
	return git_user_agent_sanitized();
}

#pragma GCC visibility pop
