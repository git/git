#include "test-tool.h"
#include "commit-graph.h"
#include "repository.h"
#include "object-store-ll.h"
#include "bloom.h"
#include "setup.h"

int cmd__read_graph(int argc UNUSED, const char **argv UNUSED)
{
	struct commit_graph *graph = NULL;
	struct object_directory *odb;

	setup_git_directory();
	odb = the_repository->objects->odb;

	prepare_repo_settings(the_repository);

	graph = read_commit_graph_one(the_repository, odb);
	if (!graph)
		return 1;

	printf("header: %08x %d %d %d %d\n",
		ntohl(*(uint32_t*)graph->data),
		*(unsigned char*)(graph->data + 4),
		*(unsigned char*)(graph->data + 5),
		*(unsigned char*)(graph->data + 6),
		*(unsigned char*)(graph->data + 7));
	printf("num_commits: %u\n", graph->num_commits);
	printf("chunks:");

	if (graph->chunk_oid_fanout)
		printf(" oid_fanout");
	if (graph->chunk_oid_lookup)
		printf(" oid_lookup");
	if (graph->chunk_commit_data)
		printf(" commit_metadata");
	if (graph->chunk_generation_data)
		printf(" generation_data");
	if (graph->chunk_generation_data_overflow)
		printf(" generation_data_overflow");
	if (graph->chunk_extra_edges)
		printf(" extra_edges");
	if (graph->chunk_bloom_indexes)
		printf(" bloom_indexes");
	if (graph->chunk_bloom_data)
		printf(" bloom_data");
	printf("\n");

	printf("options:");
	if (graph->bloom_filter_settings)
		printf(" bloom(%"PRIu32",%"PRIu32",%"PRIu32")",
		       graph->bloom_filter_settings->hash_version,
		       graph->bloom_filter_settings->bits_per_entry,
		       graph->bloom_filter_settings->num_hashes);
	if (graph->read_generation_data)
		printf(" read_generation_data");
	if (graph->topo_levels)
		printf(" topo_levels");
	printf("\n");

	UNLEAK(graph);

	return 0;
}
