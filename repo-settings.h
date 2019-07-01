#ifndef REPO_SETTINGS_H
#define REPO_SETTINGS_H

struct repo_settings {
	int core_commit_graph;
	int gc_write_commit_graph;
};

struct repository;

void prepare_repo_settings(struct repository *r);

#endif /* REPO_SETTINGS_H */
