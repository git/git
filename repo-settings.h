#ifndef REPO_SETTINGS_H
#define REPO_SETTINGS_H

struct fsmonitor_settings;
struct repository;

enum untracked_cache_setting {
	UNTRACKED_CACHE_KEEP,
	UNTRACKED_CACHE_REMOVE,
	UNTRACKED_CACHE_WRITE,
};

enum fetch_negotiation_setting {
	FETCH_NEGOTIATION_CONSECUTIVE,
	FETCH_NEGOTIATION_SKIPPING,
	FETCH_NEGOTIATION_NOOP,
};

enum log_refs_config {
	LOG_REFS_UNSET = -1,
	LOG_REFS_NONE = 0,
	LOG_REFS_NORMAL,
	LOG_REFS_ALWAYS
};

struct repo_settings {
	int initialized;

	int core_commit_graph;
	int commit_graph_generation_version;
	int commit_graph_changed_paths_version;
	int gc_write_commit_graph;
	int fetch_write_commit_graph;
	int command_requires_full_index;
	int sparse_index;
	int pack_read_reverse_index;
	int pack_use_bitmap_boundary_traversal;
	int pack_use_multi_pack_reuse;

	int shared_repository;
	int shared_repository_initialized;

	/*
	 * Does this repository have core.useReplaceRefs=true (on by
	 * default)? This provides a repository-scoped version of this
	 * config, though it could be disabled process-wide via some Git
	 * builtins or the --no-replace-objects option. See
	 * replace_refs_enabled() for more details.
	 */
	int read_replace_refs;

	struct fsmonitor_settings *fsmonitor; /* lazily loaded */

	int index_version;
	int index_skip_hash;
	enum untracked_cache_setting core_untracked_cache;

	int pack_use_sparse;
	int pack_use_path_walk;
	enum fetch_negotiation_setting fetch_negotiation_algorithm;

	int core_multi_pack_index;
	int warn_ambiguous_refs; /* lazily loaded via accessor */

	size_t delta_base_cache_limit;
	size_t packed_git_window_size;
	size_t packed_git_limit;
	unsigned long big_file_threshold;

	char *hooks_path;
};
#define REPO_SETTINGS_INIT { \
	.shared_repository = -1, \
	.index_version = -1, \
	.core_untracked_cache = UNTRACKED_CACHE_KEEP, \
	.fetch_negotiation_algorithm = FETCH_NEGOTIATION_CONSECUTIVE, \
	.warn_ambiguous_refs = -1, \
	.delta_base_cache_limit = DEFAULT_DELTA_BASE_CACHE_LIMIT, \
	.packed_git_window_size = DEFAULT_PACKED_GIT_WINDOW_SIZE, \
	.packed_git_limit = DEFAULT_PACKED_GIT_LIMIT, \
}

void prepare_repo_settings(struct repository *r);
void repo_settings_clear(struct repository *r);

/* Read the value for "core.logAllRefUpdates". */
enum log_refs_config repo_settings_get_log_all_ref_updates(struct repository *repo);
/* Read the value for "core.warnAmbiguousRefs". */
int repo_settings_get_warn_ambiguous_refs(struct repository *repo);
/* Read the value for "core.hooksPath". */
const char *repo_settings_get_hooks_path(struct repository *repo);

/* Read and set the value for "core.bigFileThreshold". */
unsigned long repo_settings_get_big_file_threshold(struct repository *repo);
void repo_settings_set_big_file_threshold(struct repository *repo, unsigned long value);

/* Read, set or reset the value for "core.sharedRepository". */
int repo_settings_get_shared_repository(struct repository *repo);
void repo_settings_set_shared_repository(struct repository *repo, int value);
void repo_settings_reset_shared_repository(struct repository *repo);

#endif /* REPO_SETTINGS_H */
