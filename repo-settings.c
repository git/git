#include "cache.h"
#include "config.h"
#include "repository.h"
#include "midx.h"
#include "fsmonitor-ipc.h"
#include "fsmonitor-settings.h"

#define UPDATE_DEFAULT_BOOL(s,v) do { if (s == -1) { s = v; } } while(0)

void prepare_repo_settings(struct repository *r)
{
	int value, feature_many_files = 0;
	char *strval;

	if (r->settings.initialized)
		return;

	/* Defaults */
	memset(&r->settings, -1, sizeof(r->settings));

	if (!repo_config_get_bool(r, "core.commitgraph", &value))
		r->settings.core_commit_graph = value;
	if (!repo_config_get_bool(r, "commitgraph.readchangedpaths", &value))
		r->settings.commit_graph_read_changed_paths = value;
	if (!repo_config_get_bool(r, "gc.writecommitgraph", &value))
		r->settings.gc_write_commit_graph = value;
	UPDATE_DEFAULT_BOOL(r->settings.core_commit_graph, 1);
	UPDATE_DEFAULT_BOOL(r->settings.commit_graph_read_changed_paths, 1);
	UPDATE_DEFAULT_BOOL(r->settings.gc_write_commit_graph, 1);

	r->settings.fsmonitor = NULL; /* lazy loaded */

	if (!repo_config_get_int(r, "index.version", &value))
		r->settings.index_version = value;
	if (!repo_config_get_maybe_bool(r, "core.untrackedcache", &value)) {
		if (value == 0)
			r->settings.core_untracked_cache = UNTRACKED_CACHE_REMOVE;
		else
			r->settings.core_untracked_cache = UNTRACKED_CACHE_WRITE;
	} else if (!repo_config_get_string(r, "core.untrackedcache", &strval)) {
		if (!strcasecmp(strval, "keep"))
			r->settings.core_untracked_cache = UNTRACKED_CACHE_KEEP;

		free(strval);
	}

	if (!repo_config_get_string(r, "fetch.negotiationalgorithm", &strval)) {
		if (!strcasecmp(strval, "skipping"))
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_SKIPPING;
		else if (!strcasecmp(strval, "noop"))
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_NOOP;
		else
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_DEFAULT;
	}

	if (!repo_config_get_bool(r, "pack.usesparse", &value))
		r->settings.pack_use_sparse = value;
	UPDATE_DEFAULT_BOOL(r->settings.pack_use_sparse, 1);

	value = git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0);
	if (value || !repo_config_get_bool(r, "core.multipackindex", &value))
		r->settings.core_multi_pack_index = value;
	UPDATE_DEFAULT_BOOL(r->settings.core_multi_pack_index, 1);

	if (!repo_config_get_bool(r, "feature.manyfiles", &value) && value) {
		feature_many_files = 1;
		UPDATE_DEFAULT_BOOL(r->settings.index_version, 4);
		UPDATE_DEFAULT_BOOL(r->settings.core_untracked_cache, UNTRACKED_CACHE_WRITE);
	}

	if (!repo_config_get_bool(r, "fetch.writecommitgraph", &value))
		r->settings.fetch_write_commit_graph = value;
	UPDATE_DEFAULT_BOOL(r->settings.fetch_write_commit_graph, 0);

	if (!repo_config_get_bool(r, "feature.experimental", &value) && value) {
		UPDATE_DEFAULT_BOOL(r->settings.fetch_negotiation_algorithm, FETCH_NEGOTIATION_SKIPPING);

		/*
		 * Force enable the builtin FSMonitor (unless the repo
		 * is incompatible or they've already selected it or
		 * the hook version).  But only if they haven't
		 * explicitly turned it off -- so only if our config
		 * value is UNSET.
		 *
		 * lookup_fsmonitor_settings() and check_for_ipc() do
		 * not distinguish between explicitly set FALSE and
		 * UNSET, so we re-test for an UNSET config key here.
		 *
		 * I'm not sure I want to fix fsmonitor-settings.c to
		 * have more than one _DISABLED state since our usage
		 * here is only to support this experimental period
		 * (and I don't want to overload the _reason field
		 * because it describes incompabilities).
		 */
		if (feature_many_files &&
		    fsmonitor_ipc__is_supported()  &&
		    fsm_settings__get_mode(r) == FSMONITOR_MODE_DISABLED &&
		    repo_config_get_bool(r, "core.usebuiltinfsmonitor", &value))
			fsm_settings__set_ipc(r);
	}

	/* Hack for test programs like test-dump-untracked-cache */
	if (ignore_untracked_cache_config)
		r->settings.core_untracked_cache = UNTRACKED_CACHE_KEEP;
	else
		UPDATE_DEFAULT_BOOL(r->settings.core_untracked_cache, UNTRACKED_CACHE_KEEP);

	UPDATE_DEFAULT_BOOL(r->settings.fetch_negotiation_algorithm, FETCH_NEGOTIATION_DEFAULT);

	/*
	 * This setting guards all index reads to require a full index
	 * over a sparse index. After suitable guards are placed in the
	 * codebase around uses of the index, this setting will be
	 * removed.
	 */
	r->settings.command_requires_full_index = 1;

	/*
	 * Initialize this as off.
	 */
	r->settings.sparse_index = 0;
	if (!repo_config_get_bool(r, "index.sparse", &value) && value)
		r->settings.sparse_index = 1;
}
