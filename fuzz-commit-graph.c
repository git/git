#include "commit-graph.h"
#include "repository.h"

struct commit_graph *parse_commit_graph(void *graph_map, int fd,
					size_t graph_size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct commit_graph *g;

	initialize_the_repository();
	g = parse_commit_graph((void *)data, -1, size);
	repo_clear(the_repository);
	free(g);

	return 0;
}
