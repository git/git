// Shim to publicly export Git symbols. These must be renamed so that the
// original symbols can be hidden. Renaming these with a "libgit_" prefix also
// avoids conflicts with other libraries such as libgit2.

#include "git-compat-util.h"
#include "config.h"
#include "contrib/libgit-rs/libgit-sys/public_symbol_export.h"
#include "version.h"

#pragma GCC visibility push(default)

struct libgit_config_set *libgit_configset_alloc(void)
{
	struct config_set *cs = xmalloc(sizeof(struct config_set));
	git_configset_init(cs);
	return (struct libgit_config_set *) cs;
}

void libgit_configset_free(struct libgit_config_set *cs)
{
	git_configset_clear((struct config_set *) cs);
	free((struct config_set *) cs);
}

int libgit_configset_add_file(struct libgit_config_set *cs, const char *filename)
{
	return git_configset_add_file((struct config_set *) cs, filename);
}

int libgit_configset_get_int(struct libgit_config_set *cs, const char *key, int *dest)
{
	return git_configset_get_int((struct config_set *) cs, key, dest);
}

int libgit_configset_get_string(struct libgit_config_set *cs, const char *key, char **dest)
{
	return git_configset_get_string((struct config_set *) cs, key, dest);
}

const char *libgit_user_agent(void)
{
	return git_user_agent();
}

const char *libgit_user_agent_sanitized(void)
{
	return git_user_agent_sanitized();
}

#pragma GCC visibility pop
