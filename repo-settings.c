#include "git-compat-util.h"
#include "config.h"
#include "repo-settings.h"
#include "repository.h"
#include "midx.h"
#include "pack-objects.h"
#include "setup.h"

static void repo_cfg_bool(struct repository *r, const char *key, int *dest,
			  int def)
{
	if (repo_config_get_bool(r, key, dest))
		*dest = def;
}

static void repo_cfg_int(struct repository *r, const char *key, int *dest,
			 int def)
{
	if (repo_config_get_int(r, key, dest))
		*dest = def;
}

static void repo_cfg_ulong(struct repository *r, const char *key, unsigned long *dest,
			   unsigned long def)
{
	if (repo_config_get_ulong(r, key, dest))
		*dest = def;
}

void prepare_repo_settings(struct repository *r)
{
	int experimental;
	int value;
	const char *strval;
	int manyfiles;
	int read_changed_paths;
	unsigned long ulongval;

	if (!r->gitdir)
		BUG("Cannot add settings for uninitialized repository");

	if (r->settings.initialized)
		return;

	repo_settings_clear(r);
	r->settings.initialized++;

	/* Booleans config or default, cascades to other settings */
	repo_cfg_bool(r, "feature.manyfiles", &manyfiles, 0);
	repo_cfg_bool(r, "feature.experimental", &experimental, 0);

	/* Defaults modified by feature.* */
	if (experimental) {
		r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_SKIPPING;
		r->settings.pack_use_bitmap_boundary_traversal = 1;
		r->settings.pack_use_multi_pack_reuse = 1;
		r->settings.pack_use_path_walk = 1;
	}
	if (manyfiles) {
		r->settings.index_version = 4;
		r->settings.index_skip_hash = 1;
		r->settings.core_untracked_cache = UNTRACKED_CACHE_WRITE;
		r->settings.pack_use_path_walk = 1;
	}

	/* Commit graph config or default, does not cascade (simple) */
	repo_cfg_bool(r, "core.commitgraph", &r->settings.core_commit_graph, 1);
	repo_cfg_int(r, "commitgraph.generationversion", &r->settings.commit_graph_generation_version, 2);
	repo_cfg_bool(r, "commitgraph.readchangedpaths", &read_changed_paths, 1);
	repo_cfg_int(r, "commitgraph.changedpathsversion",
		     &r->settings.commit_graph_changed_paths_version,
		     read_changed_paths ? -1 : 0);
	repo_cfg_bool(r, "gc.writecommitgraph", &r->settings.gc_write_commit_graph, 1);
	repo_cfg_bool(r, "fetch.writecommitgraph", &r->settings.fetch_write_commit_graph, 0);

	/* Boolean config or default, does not cascade (simple)  */
	repo_cfg_bool(r, "pack.usesparse", &r->settings.pack_use_sparse, 1);
	repo_cfg_bool(r, "pack.usepathwalk", &r->settings.pack_use_path_walk, 0);
	repo_cfg_bool(r, "core.multipackindex", &r->settings.core_multi_pack_index, 1);
	repo_cfg_bool(r, "index.sparse", &r->settings.sparse_index, 0);
	repo_cfg_bool(r, "index.skiphash", &r->settings.index_skip_hash, r->settings.index_skip_hash);
	repo_cfg_bool(r, "pack.readreverseindex", &r->settings.pack_read_reverse_index, 1);
	repo_cfg_bool(r, "pack.usebitmapboundarytraversal",
		      &r->settings.pack_use_bitmap_boundary_traversal,
		      r->settings.pack_use_bitmap_boundary_traversal);
	repo_cfg_bool(r, "core.usereplacerefs", &r->settings.read_replace_refs, 1);

	/*
	 * The GIT_TEST_MULTI_PACK_INDEX variable is special in that
	 * either it *or* the config sets
	 * r->settings.core_multi_pack_index if true. We don't take
	 * the environment variable if it exists (even if false) over
	 * any config, as in most other cases.
	 */
	if (git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0))
		r->settings.core_multi_pack_index = 1;

	/*
	 * Non-boolean config
	 */
	if (!repo_config_get_int(r, "index.version", &value))
		r->settings.index_version = value;

	if (!repo_config_get_string_tmp(r, "core.untrackedcache", &strval)) {
		int v = git_parse_maybe_bool(strval);

		/*
		 * If it's set to "keep", or some other non-boolean
		 * value then "v < 0". Then we do nothing and keep it
		 * at the default of UNTRACKED_CACHE_KEEP.
		 */
		if (v >= 0)
			r->settings.core_untracked_cache = v ?
				UNTRACKED_CACHE_WRITE : UNTRACKED_CACHE_REMOVE;
	}

	if (!repo_config_get_string_tmp(r, "fetch.negotiationalgorithm", &strval)) {
		int fetch_default = r->settings.fetch_negotiation_algorithm;
		if (!strcasecmp(strval, "skipping"))
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_SKIPPING;
		else if (!strcasecmp(strval, "noop"))
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_NOOP;
		else if (!strcasecmp(strval, "consecutive"))
			r->settings.fetch_negotiation_algorithm = FETCH_NEGOTIATION_CONSECUTIVE;
		else if (!strcasecmp(strval, "default"))
			r->settings.fetch_negotiation_algorithm = fetch_default;
		else
			die("unknown fetch negotiation algorithm '%s'", strval);
	}

	/*
	 * This setting guards all index reads to require a full index
	 * over a sparse index. After suitable guards are placed in the
	 * codebase around uses of the index, this setting will be
	 * removed.
	 */
	r->settings.command_requires_full_index = 1;

	if (!repo_config_get_ulong(r, "core.deltabasecachelimit", &ulongval))
		r->settings.delta_base_cache_limit = ulongval;

	if (!repo_config_get_ulong(r, "core.packedgitwindowsize", &ulongval)) {
		int pgsz_x2 = getpagesize() * 2;

		/* This value must be multiple of (pagesize * 2) */
		ulongval /= pgsz_x2;
		if (ulongval < 1)
			ulongval = 1;
		r->settings.packed_git_window_size = ulongval * pgsz_x2;
	}

	if (!repo_config_get_ulong(r, "core.packedgitlimit", &ulongval))
		r->settings.packed_git_limit = ulongval;
}

void repo_settings_clear(struct repository *r)
{
	struct repo_settings empty = REPO_SETTINGS_INIT;
	FREE_AND_NULL(r->settings.fsmonitor);
	FREE_AND_NULL(r->settings.hooks_path);
	r->settings = empty;
}

unsigned long repo_settings_get_big_file_threshold(struct repository *repo)
{
	if (!repo->settings.big_file_threshold)
		repo_cfg_ulong(repo, "core.bigfilethreshold",
			       &repo->settings.big_file_threshold, 512 * 1024 * 1024);
	return repo->settings.big_file_threshold;
}

void repo_settings_set_big_file_threshold(struct repository *repo, unsigned long value)
{
	repo->settings.big_file_threshold = value;
}

enum log_refs_config repo_settings_get_log_all_ref_updates(struct repository *repo)
{
	const char *value;

	if (!repo_config_get_string_tmp(repo, "core.logallrefupdates", &value)) {
		if (value && !strcasecmp(value, "always"))
			return LOG_REFS_ALWAYS;
		else if (git_config_bool("core.logallrefupdates", value))
			return LOG_REFS_NORMAL;
		else
			return LOG_REFS_NONE;
	}

	return LOG_REFS_UNSET;
}

int repo_settings_get_warn_ambiguous_refs(struct repository *repo)
{
	prepare_repo_settings(repo);
	if (repo->settings.warn_ambiguous_refs < 0)
		repo_cfg_bool(repo, "core.warnambiguousrefs",
			      &repo->settings.warn_ambiguous_refs, 1);
	return repo->settings.warn_ambiguous_refs;
}

const char *repo_settings_get_hooks_path(struct repository *repo)
{
	if (!repo->settings.hooks_path)
		repo_config_get_pathname(repo, "core.hookspath", &repo->settings.hooks_path);
	return repo->settings.hooks_path;
}

int repo_settings_get_shared_repository(struct repository *repo)
{
	if (!repo->settings.shared_repository_initialized) {
		const char *var = "core.sharedrepository";
		const char *value;
		if (!repo_config_get_value(repo, var, &value))
			repo->settings.shared_repository = git_config_perm(var, value);
		else
			repo->settings.shared_repository = PERM_UMASK;
		repo->settings.shared_repository_initialized = 1;
	}
	return repo->settings.shared_repository;
}

void repo_settings_set_shared_repository(struct repository *repo, int value)
{
	repo->settings.shared_repository = value;
	repo->settings.shared_repository_initialized = 1;
}

void repo_settings_reset_shared_repository(struct repository *repo)
{
	repo->settings.shared_repository_initialized = 0;
}
