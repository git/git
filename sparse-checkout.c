#include "cache.h"
#include "config.h"
#include "sparse-checkout.h"

int opt_restrict_to_sparse_paths = -1;

int restrict_to_sparse_paths(struct repository *repo)
{
	int ret;

	if (opt_restrict_to_sparse_paths >= 0)
		return opt_restrict_to_sparse_paths;

	if (repo_config_get_bool(repo, "sparse.restrictcmds", &ret))
		ret = 1;

	return ret;
}
