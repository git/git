#include "cache.h"
#include "config.h"
#include "repository.h"
#include "midx.h"

#define UPDATE_DEFAULT_BOOL(s,v) do { if (s == -1) { s = v; } } while(0)

/*
 * Return 1 if the repo/workdir is incompatible with FSMonitor.
 */
static int is_repo_incompatible_with_fsmonitor(struct repository *r)
{
	const char *const_strval;

	/*
	 * Bare repositories don't have a working directory and
	 * therefore, nothing to watch.
	 */
	if (!r->worktree)
		return 1;

	/*
	 * GVFS (aka VFS for Git) is incompatible with FSMonitor.
	 *
	 * Granted, core Git does not know anything about GVFS and
	 * we shouldn't make assumptions about a downstream feature,
	 * but users can install both versions.  And this can lead
	 * to incorrect results from core Git commands.  So, without
	 * bringing in any of the GVFS code, do a simple config test
	 * for a published config setting.  (We do not look at the
	 * various *_TEST_* environment variables.)
	 */
	if (!repo_config_get_value(r, "core.virtualfilesystem", &const_strval))
		return 1;

	return 0;
}

void prepare_repo_settings(struct repository *r)
{
	int value;
	char *strval;
	const char *const_strval;

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

	r->settings.fsmonitor_hook_path = NULL;
	r->settings.fsmonitor_mode = FSMONITOR_MODE_DISABLED;
	if (is_repo_incompatible_with_fsmonitor(r))
		r->settings.fsmonitor_mode = FSMONITOR_MODE_INCOMPATIBLE;
	else if (!repo_config_get_bool(r, "core.usebuiltinfsmonitor", &value)
		   && value)
		r->settings.fsmonitor_mode = FSMONITOR_MODE_IPC;
	else {
		if (repo_config_get_pathname(r, "core.fsmonitor", &const_strval))
			const_strval = getenv("GIT_TEST_FSMONITOR");
		if (const_strval && *const_strval) {
			r->settings.fsmonitor_hook_path = strdup(const_strval);
			r->settings.fsmonitor_mode = FSMONITOR_MODE_HOOK;
		}
	}

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
		UPDATE_DEFAULT_BOOL(r->settings.index_version, 4);
		UPDATE_DEFAULT_BOOL(r->settings.core_untracked_cache, UNTRACKED_CACHE_WRITE);
	}

	if (!repo_config_get_bool(r, "fetch.writecommitgraph", &value))
		r->settings.fetch_write_commit_graph = value;
	UPDATE_DEFAULT_BOOL(r->settings.fetch_write_commit_graph, 0);

	if (!repo_config_get_bool(r, "feature.experimental", &value) && value)
		UPDATE_DEFAULT_BOOL(r->settings.fetch_negotiation_algorithm, FETCH_NEGOTIATION_SKIPPING);

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
