#include "cache.h"
#include "repository.h"
#include "config.h"
#include "repo-settings.h"

#define UPDATE_DEFAULT(s,v) do { if (s == -1) { s = v; } } while(0)

static int git_repo_config(const char *key, const char *value, void *cb)
{
	struct repo_settings *rs = (struct repo_settings *)cb;

	if (!strcmp(key, "core.featureadoptionrate")) {
		int rate = git_config_int(key, value);
		if (rate >= 3) {
			UPDATE_DEFAULT(rs->core_commit_graph, 1);
			UPDATE_DEFAULT(rs->gc_write_commit_graph, 1);
			UPDATE_DEFAULT(rs->index_version, 4);
		}
		if (rate >= 5) {
			UPDATE_DEFAULT(rs->pack_use_sparse, 1);
		}
		return 0;
	}
	if (!strcmp(key, "core.commitgraph")) {
		rs->core_commit_graph = git_config_bool(key, value);
		return 0;
	}
	if (!strcmp(key, "gc.writecommitgraph")) {
		rs->gc_write_commit_graph = git_config_bool(key, value);
		return 0;
	}
	if (!strcmp(key, "pack.usesparse")) {
		rs->pack_use_sparse = git_config_bool(key, value);
		return 0;
	}
	if (!strcmp(key, "index.version")) {
		rs->index_version = git_config_int(key, value);
		return 0;
	}

	return 1;
}

void prepare_repo_settings(struct repository *r)
{
	if (r->settings)
		return;

	r->settings = xmalloc(sizeof(*r->settings));

	/* Defaults */
	r->settings->core_commit_graph = -1;
	r->settings->gc_write_commit_graph = -1;
	r->settings->pack_use_sparse = -1;
	r->settings->index_version = -1;

	repo_config(r, git_repo_config, r->settings);
}
