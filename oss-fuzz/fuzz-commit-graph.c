#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "commit-graph.h"
#include "repository.h"

struct commit_graph *parse_commit_graph(struct repo_settings *s,
					void *graph_map, size_t graph_size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct commit_graph *g;

	initialize_repository(the_repository);

	/*
	 * Initialize the_repository with commit-graph settings that would
	 * normally be read from the repository's gitdir. We want to avoid
	 * touching the disk to keep the individual fuzz-test cases as fast as
	 * possible.
	 */
	repo_set_hash_algo(the_repository, GIT_HASH_SHA1);
	the_repository->settings.commit_graph_generation_version = 2;
	the_repository->settings.commit_graph_changed_paths_version = 1;
	g = parse_commit_graph(&the_repository->settings, (void *)data, size);
	repo_clear(the_repository);
	free_commit_graph(g);

	return 0;
}
