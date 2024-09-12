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
	enum fetch_negotiation_setting fetch_negotiation_algorithm;

	int core_multi_pack_index;
};
#define REPO_SETTINGS_INIT { \
	.index_version = -1, \
	.core_untracked_cache = UNTRACKED_CACHE_KEEP, \
	.fetch_negotiation_algorithm = FETCH_NEGOTIATION_CONSECUTIVE, \
}

void prepare_repo_settings(struct repository *r);

#endif /* REPO_SETTINGS_H */
