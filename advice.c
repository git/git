#include "cache.h"

int advice_push_nonfastforward = 1;
int advice_status_hints = 1;
int advice_commit_before_merge = 1;

static struct {
	const char *name;
	int *preference;
} advice_config[] = {
	{ "pushnonfastforward", &advice_push_nonfastforward },
	{ "statushints", &advice_status_hints },
	{ "commitbeforemerge", &advice_commit_before_merge },
};

int git_default_advice_config(const char *var, const char *value)
{
	const char *k = skip_prefix(var, "advice.");
	int i;

	for (i = 0; i < ARRAY_SIZE(advice_config); i++) {
		if (strcmp(k, advice_config[i].name))
			continue;
		*advice_config[i].preference = git_config_bool(var, value);
		return 0;
	}

	return 0;
}
