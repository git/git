#include "cummit-graph.h"
#include "repository.h"

struct cummit_graph *parse_cummit_graph(struct repository *r,
					void *graph_map, size_t graph_size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct cummit_graph *g;

	initialize_the_repository();
	g = parse_cummit_graph(the_repository, (void *)data, size);
	repo_clear(the_repository);
	free_cummit_graph(g);

	return 0;
}
