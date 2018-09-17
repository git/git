#ifndef COMMIT_GRAPH_H
#define COMMIT_GRAPH_H

#include "git-compat-util.h"
#include "repository.h"
#include "string-list.h"
#include "cache.h"

#define GIT_TEST_COMMIT_GRAPH "GIT_TEST_COMMIT_GRAPH"

struct commit;

char *get_commit_graph_filename(const char *obj_dir);

/*
 * Given a commit struct, try to fill the commit struct info, including:
 *  1. tree object
 *  2. date
 *  3. parents.
 *
 * Returns 1 if and only if the commit was found in the packed graph.
 *
 * See parse_commit_buffer() for the fallback after this call.
 */
int parse_commit_in_graph(struct repository *r, struct commit *item);

/*
 * It is possible that we loaded commit contents from the commit buffer,
 * but we also want to ensure the commit-graph content is correctly
 * checked and filled. Fill the graph_pos and generation members of
 * the given commit.
 */
void load_commit_graph_info(struct repository *r, struct commit *item);

struct tree *get_commit_tree_in_graph(struct repository *r,
				      const struct commit *c);

struct commit_graph {
	int graph_fd;

	const unsigned char *data;
	size_t data_len;

	unsigned char hash_len;
	unsigned char num_chunks;
	uint32_t num_commits;
	struct object_id oid;

	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_commit_data;
	const unsigned char *chunk_large_edges;
};

struct commit_graph *load_commit_graph_one(const char *graph_file);

/*
 * Return 1 if and only if the repository has a commit-graph
 * file and generation numbers are computed in that file.
 */
int generation_numbers_enabled(struct repository *r);

void write_commit_graph_reachable(const char *obj_dir, int append);
void write_commit_graph(const char *obj_dir,
			struct string_list *pack_indexes,
			struct string_list *commit_hex,
			int append);

int verify_commit_graph(struct repository *r, struct commit_graph *g);

void free_commit_graph(struct commit_graph *);

#endif
