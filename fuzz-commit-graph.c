#include "commit-graph.h"

struct commit_graph *parse_commit_graph(void *graph_map, int fd,
					size_t graph_size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct commit_graph *g;

	g = parse_commit_graph((void *)data, -1, size);
	free(g);

	return 0;
}
