#include "commit-graph.h"
#include "repository.h"

struct commit_graph *parse_commit_graph(struct repository *r,
					void *graph_map, size_t graph_size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct commit_graph *g;

	initialize_the_repository();
	g = parse_commit_graph(the_repository, (void *)data, size);
	repo_clear(the_repository);
	free_commit_graph(g);

	return 0;
}
