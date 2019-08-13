#include "cache.h"
#include "config.h"
#include "repository.h"

void prepare_repo_settings(struct repository *r)
{
	int value;

	if (r->settings.initialized)
		return;

	/* Defaults */
	memset(&r->settings, -1, sizeof(r->settings));

	if (!repo_config_get_bool(r, "core.commitgraph", &value))
		r->settings.core_commit_graph = value;
	if (!repo_config_get_bool(r, "gc.writecommitgraph", &value))
		r->settings.gc_write_commit_graph = value;

	if (!repo_config_get_bool(r, "index.version", &value))
		r->settings.index_version = value;

	if (!repo_config_get_bool(r, "pack.usesparse", &value))
		r->settings.pack_use_sparse = value;
}
