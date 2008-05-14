#include "cache.h"

static const char *alias_key;
static char *alias_val;

static int alias_lookup_cb(const char *k, const char *v, void *cb)
{
	if (!prefixcmp(k, "alias.") && !strcmp(k+6, alias_key)) {
		if (!v)
			return config_error_nonbool(k);
		alias_val = xstrdup(v);
		return 0;
	}
	return 0;
}

char *alias_lookup(const char *alias)
{
	alias_key = alias;
	alias_val = NULL;
	git_config(alias_lookup_cb, NULL);
	return alias_val;
}
