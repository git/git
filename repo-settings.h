#ifndef REPO_SETTINGS_H
#define REPO_SETTINGS_H

struct repo_settings {
	char core_commit_graph;
	char gc_write_commit_graph;
	char pack_use_sparse;
	int index_version;
};

struct repository;

void prepare_repo_settings(struct repository *r);

#endif /* REPO_SETTINGS_H */
