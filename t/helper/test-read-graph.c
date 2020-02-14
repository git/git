#include "test-tool.h"
#include "cache.h"
#include "commit-graph.h"
#include "repository.h"
#include "object-store.h"

int cmd__read_graph(int argc, const char **argv)
{
	struct commit_graph *graph = NULL;
	char *graph_name;
	int open_ok;
	int fd;
	struct stat st;
	struct object_directory *odb;

	setup_git_directory();
	odb = the_repository->objects->odb;

	graph_name = get_commit_graph_filename(odb);

	open_ok = open_commit_graph(graph_name, &fd, &st);
	if (!open_ok)
		die_errno(_("Could not open commit-graph '%s'"), graph_name);

	graph = load_commit_graph_one_fd_st(fd, &st, odb);
	if (!graph)
		return 1;

	FREE_AND_NULL(graph_name);

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
	if (graph->chunk_extra_edges)
		printf(" extra_edges");
	printf("\n");

	UNLEAK(graph);

	return 0;
}
